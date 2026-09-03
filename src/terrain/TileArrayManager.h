#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "core/vulkan/VmaImage.h"

struct TerrainTile;

// Manages the shared 2D array texture for terrain tile data.
// Handles layer allocation/deallocation and copying tile data into layers.
class TileArrayManager {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TileArrayManager(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        vk::Device device;
        VmaAllocator allocator = nullptr;
        vk::Queue graphicsQueue;
        vk::CommandPool commandPool;
        uint32_t storedTileResolution = 513;
        uint32_t maxLayers = 64;
    };

    // Factory: creates the tile array image and transitions it to shader-read.
    // Returns nullptr on failure. GPU resources are released by the destructor.
    static std::unique_ptr<TileArrayManager> create(const InitInfo& info);

    ~TileArrayManager() = default;

    TileArrayManager(const TileArrayManager&) = delete;
    TileArrayManager& operator=(const TileArrayManager&) = delete;
    TileArrayManager(TileArrayManager&&) = delete;
    TileArrayManager& operator=(TileArrayManager&&) = delete;

    // Allocate a free layer, returns -1 if none available
    int32_t allocateLayer();

    // Free a previously allocated layer
    void freeLayer(int32_t layerIndex);

    // Copy tile CPU data into a specific array layer (GPU upload)
    void copyTileToLayer(const TerrainTile& tile, uint32_t layerIndex);

    vk::ImageView getArrayView() const { return arrayView_ ? static_cast<vk::ImageView>(**arrayView_) : vk::ImageView{}; }
    vk::Image getArrayImage() const { return arrayImage_.get(); }
    uint32_t getMaxLayers() const { return maxLayers_; }

private:
    bool initInternal(const InitInfo& info);

    vk::Device device_;
    VmaAllocator allocator_ = nullptr;
    vk::Queue graphicsQueue_;
    vk::CommandPool commandPool_;
    uint32_t storedTileResolution_ = 513;
    uint32_t maxLayers_ = 64;

    // Declaration order matters: the view is destroyed before the image it references.
    ManagedImage arrayImage_;
    std::optional<vk::raii::ImageView> arrayView_;

    std::array<bool, 64> freeLayers_{};
};
