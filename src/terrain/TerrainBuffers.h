#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include "core/vulkan/VmaBuffer.h"
#include <cstdint>
#include <memory>
#include <vector>

class TerrainBuffers {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TerrainBuffers(ConstructToken) {}

    struct InitInfo {
        VmaAllocator allocator = nullptr;
        uint32_t framesInFlight = 0;
        uint32_t maxVisibleTriangles = 0;
    };

    // Factory method - returns nullptr on failure
    static std::unique_ptr<TerrainBuffers> create(const InitInfo& info);

    // All buffers are ManagedBuffer (RAII): mapped buffers are unmapped and every
    // allocation is freed by member destruction.
    ~TerrainBuffers() = default;

    // Non-copyable, non-movable (only ever held by unique_ptr)
    TerrainBuffers(const TerrainBuffers&) = delete;
    TerrainBuffers& operator=(const TerrainBuffers&) = delete;
    TerrainBuffers(TerrainBuffers&&) = delete;
    TerrainBuffers& operator=(TerrainBuffers&&) = delete;

    // Uniform buffer accessors
    vk::Buffer getUniformBuffer(uint32_t frameIndex) const { return uniformBuffers.buffers[frameIndex].get(); }
    void* getUniformMappedPtr(uint32_t frameIndex) const { return uniformBuffers.mapped[frameIndex]; }

    // Indirect buffer accessors
    vk::Buffer getIndirectDispatchBuffer() const { return indirectDispatch.get(); }
    vk::Buffer getIndirectDrawBuffer() const { return indirectDraw.get(); }
    void* getIndirectDrawMappedPtr() const { return indirectDrawMapped; }

    // Visibility buffer accessors (stream compaction)
    vk::Buffer getVisibleIndicesBuffer() const { return visibleIndices.get(); }
    vk::Buffer getCullIndirectDispatchBuffer() const { return cullIndirectDispatch.get(); }

    // Shadow buffer accessors
    vk::Buffer getShadowVisibleBuffer() const { return shadowVisible.get(); }
    vk::Buffer getShadowIndirectDrawBuffer() const { return shadowIndirectDraw.get(); }

    // Caustics UBO accessors
    vk::Buffer getCausticsUniformBuffer(uint32_t frameIndex) const { return causticsUniforms.buffers[frameIndex].get(); }
    void* getCausticsMappedPtr(uint32_t frameIndex) const { return causticsUniforms.mapped[frameIndex]; }

    // Liquid UBO accessors (composable material system - puddles, wet surfaces)
    vk::Buffer getLiquidUniformBuffer(uint32_t frameIndex) const { return liquidUniforms.buffers[frameIndex].get(); }
    void* getLiquidMappedPtr(uint32_t frameIndex) const { return liquidUniforms.mapped[frameIndex]; }

    // Material Layer UBO accessors (composable material system - layer blending)
    vk::Buffer getMaterialLayerUniformBuffer(uint32_t frameIndex) const { return materialLayerUniforms.buffers[frameIndex].get(); }
    void* getMaterialLayerMappedPtr(uint32_t frameIndex) const { return materialLayerUniforms.mapped[frameIndex]; }

private:
    // One host-visible, persistently mapped uniform buffer per frame in flight.
    struct PerFrameUniforms {
        std::vector<ManagedBuffer> buffers;
        std::vector<void*> mapped;
    };

    bool initInternal(const InitInfo& info);
    bool createUniformBuffers(const InitInfo& info);
    bool createIndirectBuffers(const InitInfo& info);

    static bool createPerFrameUniforms(VmaAllocator allocator, uint32_t frameCount,
                                       vk::DeviceSize size, PerFrameUniforms& out);

    // Per-frame uniform buffers
    PerFrameUniforms uniformBuffers;

    // Indirect dispatch/draw buffers
    ManagedBuffer indirectDispatch;
    ManagedBuffer indirectDraw;
    void* indirectDrawMapped = nullptr;

    // Stream compaction buffers
    ManagedBuffer visibleIndices;
    ManagedBuffer cullIndirectDispatch;

    // Shadow culling buffers
    ManagedBuffer shadowVisible;
    ManagedBuffer shadowIndirectDraw;

    // Caustics uniform buffers (per-frame for underwater caustics)
    PerFrameUniforms causticsUniforms;

    // Liquid uniform buffers (composable material system - puddles, wetness)
    PerFrameUniforms liquidUniforms;

    // Material layer uniform buffers (composable material system - layer blending)
    PerFrameUniforms materialLayerUniforms;
};
