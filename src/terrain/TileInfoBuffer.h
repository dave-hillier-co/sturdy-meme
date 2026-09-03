#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <vector>
#include <cstdint>
#include "core/vulkan/VmaBuffer.h"
#include "core/FrameBuffered.h"

struct TerrainTile;

// Tile info for GPU (matches shader buffer layout)
struct TileInfoGPU {
    glm::vec4 worldBounds;  // xy = min corner, zw = max corner
    glm::vec4 uvScaleOffset; // xy = scale, zw = offset (for UV calculation)
    glm::ivec4 layerIndex;  // x = layer index in tile array, yzw = padding (std140 alignment)
};

// Manages the triple-buffered tile info storage buffer used by shaders
// to look up which array layer corresponds to which world region.
class TileInfoBuffer {
public:
    static constexpr uint32_t FRAMES_IN_FLIGHT = TripleBuffered<int>::DEFAULT_FRAME_COUNT;

    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TileInfoBuffer(ConstructToken) {}

    struct InitInfo {
        VmaAllocator allocator = nullptr;
        uint32_t maxActiveTiles = 64;
    };

    // Factory: creates the per-frame buffers, maps them and zeroes the active
    // tile count in every frame. Returns nullptr on failure. Buffers are unmapped
    // and freed by the ManagedBuffer destructors.
    static std::unique_ptr<TileInfoBuffer> create(const InitInfo& info);

    ~TileInfoBuffer() = default;

    TileInfoBuffer(const TileInfoBuffer&) = delete;
    TileInfoBuffer& operator=(const TileInfoBuffer&) = delete;
    TileInfoBuffer(TileInfoBuffer&&) = delete;
    TileInfoBuffer& operator=(TileInfoBuffer&&) = delete;

    // Update the buffer for the current frame with the given active tiles
    void update(uint32_t frameIndex, const std::vector<TerrainTile*>& activeTiles);

    vk::Buffer getBuffer(uint32_t frameIndex) const {
        return buffers_.at(frameIndex).get();
    }

private:
    bool initInternal(const InitInfo& info);

    // Initialize all frame buffers to zero active tiles
    void initializeAllFrames();

    uint32_t maxActiveTiles_ = 64;

    TripleBuffered<ManagedBuffer> buffers_;
    std::array<void*, FRAMES_IN_FLIGHT> mappedPtrs_ = {};
};
