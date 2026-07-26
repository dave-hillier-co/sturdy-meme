#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>
#include <functional>
#include <optional>
#include "core/vulkan/VmaImage.h"

struct TerrainTile;

// Manages the base (coarsest) LOD tiles and the combined fallback heightmap.
// Base tiles cover the entire terrain and are never unloaded, providing
// CPU height queries and a GPU fallback texture.
class BaseHeightMap {
public:
    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        vk::Device device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        vk::Queue graphicsQueue = VK_NULL_HANDLE;
        vk::CommandPool commandPool = VK_NULL_HANDLE;
        float terrainSize = 16384.0f;
        float heightScale = 235.0f;
        uint32_t tileResolution = 512;
        uint32_t tilesX = 32;
        uint32_t tilesZ = 32;
        uint32_t numLODLevels = 4;
    };

    BaseHeightMap() = default;
    ~BaseHeightMap();

    BaseHeightMap(const BaseHeightMap&) = delete;
    BaseHeightMap& operator=(const BaseHeightMap&) = delete;
    BaseHeightMap(BaseHeightMap&&) = delete;
    BaseHeightMap& operator=(BaseHeightMap&&) = delete;

    // Initialize base tile tracking (call before loadBaseLODTiles)
    void init(const InitInfo& info);
    void cleanup();

    // Load all tiles at the coarsest LOD level synchronously.
    // loadTileCPUFunc should load a tile's CPU data given (coord, lod) and return a pointer
    // to the tile in the loadedTiles map, or nullptr on failure.
    using LoadTileFunc = std::function<TerrainTile*(int32_t tx, int32_t tz, uint32_t lod)>;
    bool loadBaseLODTiles(const LoadTileFunc& loadTileFunc);

    // Sample height from base LOD tiles (fallback when no high-res tile covers position)
    bool sampleHeight(float worldX, float worldZ, float& outHeight) const;

    // Get the base tile covering a world position (for debug queries)
    const TerrainTile* getTileAt(float worldX, float worldZ) const;

    bool hasBaseTiles() const { return !baseTiles_.empty(); }
    uint32_t getBaseLOD() const { return baseLOD_; }
    const std::vector<TerrainTile*>& getBaseTiles() const { return baseTiles_; }

    // GPU combined heightmap accessors
    vk::ImageView getHeightMapView() const { return heightMapView_ ? static_cast<vk::ImageView>(**heightMapView_) : VK_NULL_HANDLE; }
    const std::vector<float>& getHeightMapData() const { return heightMapCpuData_; }
    uint32_t getHeightMapResolution() const { return heightMapResolution_; }

private:
    bool createCombinedHeightMap();

    const vk::raii::Device* raiiDevice_ = nullptr;
    vk::Device device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::Queue graphicsQueue_ = VK_NULL_HANDLE;
    vk::CommandPool commandPool_ = VK_NULL_HANDLE;
    float terrainSize_ = 16384.0f;
    float heightScale_ = 235.0f;
    uint32_t tileResolution_ = 512;
    uint32_t tilesX_ = 32;
    uint32_t tilesZ_ = 32;
    uint32_t numLODLevels_ = 4;

    // Base LOD tiles (pointers into TerrainTileCache's loadedTiles)
    std::vector<TerrainTile*> baseTiles_;
    uint32_t baseLOD_ = 0;

    // Combined base heightmap
    ManagedImage heightMapImage_;
    std::optional<vk::raii::ImageView> heightMapView_;
    std::vector<float> heightMapCpuData_;
    uint32_t heightMapResolution_ = 512;
};
