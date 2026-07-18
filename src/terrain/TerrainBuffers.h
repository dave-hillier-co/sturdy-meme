#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include "SingleBuffer.h"
#include "PerFrameBuffer.h"
#include <cstdint>
#include <memory>

class TerrainBuffers {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TerrainBuffers(ConstructToken) {}

    struct InitInfo {
        VmaAllocator allocator;
        uint32_t framesInFlight;
        uint32_t maxVisibleTriangles;
    };

    // Factory method - returns nullptr on failure
    static std::unique_ptr<TerrainBuffers> create(const InitInfo& info);


    ~TerrainBuffers();

    // Move-only
    TerrainBuffers(TerrainBuffers&& other) noexcept;
    TerrainBuffers& operator=(TerrainBuffers&& other) noexcept;
    TerrainBuffers(const TerrainBuffers&) = delete;
    TerrainBuffers& operator=(const TerrainBuffers&) = delete;

    // Uniform buffer accessors
    vk::Buffer getUniformBuffer(uint32_t frameIndex) const { return uniformBuffers.buffers[frameIndex]; }
    void* getUniformMappedPtr(uint32_t frameIndex) const { return uniformBuffers.mappedPointers[frameIndex]; }

    // Indirect buffer accessors
    vk::Buffer getIndirectDispatchBuffer() const { return indirectDispatch.buffer; }
    vk::Buffer getIndirectDrawBuffer() const { return indirectDraw.buffer; }
    void* getIndirectDrawMappedPtr() const { return indirectDraw.mappedPointer; }

    // Visibility buffer accessors (stream compaction)
    vk::Buffer getVisibleIndicesBuffer() const { return visibleIndices.buffer; }
    vk::Buffer getCullIndirectDispatchBuffer() const { return cullIndirectDispatch.buffer; }

    // Shadow buffer accessors
    vk::Buffer getShadowVisibleBuffer() const { return shadowVisible.buffer; }
    vk::Buffer getShadowIndirectDrawBuffer() const { return shadowIndirectDraw.buffer; }

    // Caustics UBO accessors
    vk::Buffer getCausticsUniformBuffer(uint32_t frameIndex) const { return causticsUniforms.buffers[frameIndex]; }
    void* getCausticsMappedPtr(uint32_t frameIndex) const { return causticsUniforms.mappedPointers[frameIndex]; }

    // Liquid UBO accessors (composable material system - puddles, wet surfaces)
    vk::Buffer getLiquidUniformBuffer(uint32_t frameIndex) const { return liquidUniforms.buffers[frameIndex]; }
    void* getLiquidMappedPtr(uint32_t frameIndex) const { return liquidUniforms.mappedPointers[frameIndex]; }

    // Material Layer UBO accessors (composable material system - layer blending)
    vk::Buffer getMaterialLayerUniformBuffer(uint32_t frameIndex) const { return materialLayerUniforms.buffers[frameIndex]; }
    void* getMaterialLayerMappedPtr(uint32_t frameIndex) const { return materialLayerUniforms.mappedPointers[frameIndex]; }

private:
    bool initInternal(const InitInfo& info);
    bool createUniformBuffers(const InitInfo& info);
    bool createIndirectBuffers(const InitInfo& info);

    // Stored for cleanup
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // Per-frame uniform buffers
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Indirect dispatch/draw buffers
    BufferUtils::SingleBuffer indirectDispatch;
    BufferUtils::SingleBuffer indirectDraw;

    // Stream compaction buffers
    BufferUtils::SingleBuffer visibleIndices;
    BufferUtils::SingleBuffer cullIndirectDispatch;

    // Shadow culling buffers
    BufferUtils::SingleBuffer shadowVisible;
    BufferUtils::SingleBuffer shadowIndirectDraw;

    // Caustics uniform buffers (per-frame for underwater caustics)
    BufferUtils::PerFrameBufferSet causticsUniforms;

    // Liquid uniform buffers (composable material system - puddles, wetness)
    BufferUtils::PerFrameBufferSet liquidUniforms;

    // Material layer uniform buffers (composable material system - layer blending)
    BufferUtils::PerFrameBufferSet materialLayerUniforms;
};
