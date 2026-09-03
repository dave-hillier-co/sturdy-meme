#pragma once

#include "GrassConstants.h"
#include "GrassTypes.h"
#include "FrameIndexedBuffers.h"
#include "MappedFrameBuffers.h"
#include "BufferSetManager.h"
#include "UBOs.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <cstdint>

class DisplacementSystem;

/**
 * GrassBuffers - Owns GPU buffer resources for the grass system.
 *
 * Manages instance, indirect draw, culling uniform, and grass parameter buffers.
 * Handles buffer creation and per-frame uniform updates; the buffers are RAII
 * members and are released when this object is destroyed.
 * Uses double-buffering for compute/render separation and per-frame buffers
 * for CPU-GPU synchronization.
 */
class GrassBuffers {
public:
    GrassBuffers() = default;
    GrassBuffers(const GrassBuffers&) = delete;
    GrassBuffers& operator=(const GrassBuffers&) = delete;
    GrassBuffers(GrassBuffers&&) noexcept = default;
    GrassBuffers& operator=(GrassBuffers&&) noexcept = default;

    bool create(VmaAllocator allocator, uint32_t framesInFlight);

    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                        float terrainSize, float terrainHeightScale, float time,
                        DisplacementSystem* displacementSystem);

    // Buffer accessors (index by buffer set / frame index)
    const BufferUtils::FrameIndexedBuffers& instanceBuffers() const { return instanceBuffers_; }
    const BufferUtils::FrameIndexedBuffers& indirectBuffers() const { return indirectBuffers_; }
    const BufferUtils::MappedFrameBuffers& uniformBuffers() const { return uniformBuffers_; }
    const BufferUtils::MappedFrameBuffers& paramsBuffers() const { return paramsBuffers_; }

    // Buffer set management (double-buffered compute/render separation)
    BufferSetManager& bufferSets() { return bufferSets_; }
    uint32_t getComputeBufferSet() const { return bufferSets_.getComputeSet(); }
    uint32_t getRenderBufferSet() const { return bufferSets_.getRenderSet(); }
    uint32_t getBufferSetCount() const { return bufferSets_.getSetCount(); }
    void advanceBufferSet() { bufferSets_.advance(); }

private:
    BufferSetManager bufferSets_;
    BufferUtils::FrameIndexedBuffers instanceBuffers_;
    BufferUtils::FrameIndexedBuffers indirectBuffers_;
    BufferUtils::MappedFrameBuffers uniformBuffers_;
    BufferUtils::MappedFrameBuffers paramsBuffers_;
};
