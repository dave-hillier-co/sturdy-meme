#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include "core/vulkan/VmaImage.h"

struct TerrainTile;

// Manages the base (coarsest) LOD tiles and the combined fallback heightmap.
// Base tiles cover the entire terrain and are never unloaded, providing
// CPU height queries and a GPU fallback texture.
class BaseHeightMap {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit BaseHeightMap(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        vk::Device device;
        VmaAllocator allocator = nullptr;
        vk::Queue graphicsQueue;
        vk::CommandPool commandPool;
        float terrainSize = 16384.0f;
        float heightScale = 235.0f;
        uint32_t tileResolution = 512;
        uint32_t tilesX = 32;
        uint32_t tilesZ = 32;
        uint32_t numLODLevels = 4;
    };

    // loadTileFunc should load a tile's CPU data given (coord, lod) and return a pointer
    // to the tile in the loadedTiles map, or nullptr on failure.
    using LoadTileFunc = std::function<TerrainTile*(int32_t tx, int32_t tz, uint32_t lod)>;

    // Factory: loads all tiles at the coarsest LOD level synchronously and builds
    // the combined GPU fallback heightmap. Returns nullptr if no base tile could be
    // loaded, so a BaseHeightMap that exists is always populated.
    static std::unique_ptr<BaseHeightMap> create(const InitInfo& info, const LoadTileFunc& loadTileFunc);

    ~BaseHeightMap() = default;

    BaseHeightMap(const BaseHeightMap&) = delete;
    BaseHeightMap& operator=(const BaseHeightMap&) = delete;
    BaseHeightMap(BaseHeightMap&&) = delete;
    BaseHeightMap& operator=(BaseHeightMap&&) = delete;

    // Sample height from base LOD tiles (fallback when no high-res tile covers position)
    bool sampleHeight(float worldX, float worldZ, float& outHeight) const;

    // Get the base tile covering a world position (for debug queries)
    const TerrainTile* getTileAt(float worldX, float worldZ) const;

    bool hasBaseTiles() const { return !baseTiles_.empty(); }
    uint32_t getBaseLOD() const { return baseLOD_; }
    const std::vector<TerrainTile*>& getBaseTiles() const { return baseTiles_; }

    // GPU combined heightmap accessors
    vk::ImageView getHeightMapView() const { return heightMapView_ ? static_cast<vk::ImageView>(**heightMapView_) : vk::ImageView{}; }
    const std::vector<float>& getHeightMapData() const { return heightMapCpuData_; }
    uint32_t getHeightMapResolution() const { return heightMapResolution_; }

private:
    bool initInternal(const InitInfo& info, const LoadTileFunc& loadTileFunc);
    bool loadBaseLODTiles(const LoadTileFunc& loadTileFunc);
    bool createCombinedHeightMap();

    const vk::raii::Device* raiiDevice_ = nullptr;
    vk::Device device_;
    VmaAllocator allocator_ = nullptr;
    vk::Queue graphicsQueue_;
    vk::CommandPool commandPool_;
    float terrainSize_ = 16384.0f;
    float heightScale_ = 235.0f;
    uint32_t tileResolution_ = 512;
    uint32_t tilesX_ = 32;
    uint32_t tilesZ_ = 32;
    uint32_t numLODLevels_ = 4;

    // Base LOD tiles (pointers into TerrainTileCache's loadedTiles)
    std::vector<TerrainTile*> baseTiles_;
    uint32_t baseLOD_ = 0;

    // Combined base heightmap (view declared after image so it is destroyed first)
    ManagedImage heightMapImage_;
    std::optional<vk::raii::ImageView> heightMapView_;
    std::vector<float> heightMapCpuData_;
    uint32_t heightMapResolution_ = 512;
};
