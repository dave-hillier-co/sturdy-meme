#pragma once

#include "GrassConstants.h"
#include "GrassTypes.h"
#include "PerFrameBuffer.h"
#include "DoubleBufferedBuffer.h"
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
 * Handles buffer creation/destruction and per-frame uniform updates.
 * Uses double-buffering for compute/render separation and per-frame buffers
 * for CPU-GPU synchronization.
 */
class GrassBuffers {
public:
    bool create(VmaAllocator allocator, uint32_t framesInFlight);
    void destroy(VmaAllocator allocator);

    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                        float terrainSize, float terrainHeightScale, float time,
                        DisplacementSystem* displacementSystem);

    // Buffer accessors
    BufferUtils::DoubleBufferedBufferSet& instanceBuffers() { return instanceBuffers_; }
    const BufferUtils::DoubleBufferedBufferSet& instanceBuffers() const { return instanceBuffers_; }

    BufferUtils::DoubleBufferedBufferSet& indirectBuffers() { return indirectBuffers_; }
    const BufferUtils::DoubleBufferedBufferSet& indirectBuffers() const { return indirectBuffers_; }

    BufferUtils::PerFrameBufferSet& uniformBuffers() { return uniformBuffers_; }
    const BufferUtils::PerFrameBufferSet& uniformBuffers() const { return uniformBuffers_; }

    BufferUtils::PerFrameBufferSet& paramsBuffers() { return paramsBuffers_; }
    const BufferUtils::PerFrameBufferSet& paramsBuffers() const { return paramsBuffers_; }

    // Buffer set management (double-buffered compute/render separation)
    BufferSetManager& bufferSets() { return bufferSets_; }
    uint32_t getComputeBufferSet() const { return bufferSets_.getComputeSet(); }
    uint32_t getRenderBufferSet() const { return bufferSets_.getRenderSet(); }
    uint32_t getBufferSetCount() const { return bufferSets_.getSetCount(); }
    void advanceBufferSet() { bufferSets_.advance(); }

private:
    BufferSetManager bufferSets_;
    BufferUtils::DoubleBufferedBufferSet instanceBuffers_;
    BufferUtils::DoubleBufferedBufferSet indirectBuffers_;
    BufferUtils::PerFrameBufferSet uniformBuffers_;
    BufferUtils::PerFrameBufferSet paramsBuffers_;
};
