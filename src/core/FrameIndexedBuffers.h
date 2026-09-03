#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <cassert>
#include <cstdint>
#include <vector>
#include "vulkan/VmaBuffer.h"

namespace BufferUtils {

// IMPORTANT: When using multiple buffer sets for compute/render ping-pong patterns,
// the buffer set count MUST match the frames-in-flight count. Using fewer buffer sets
// (e.g., 2 sets with 3 frames in flight) causes frame N and frame N+2 to share buffers,
// leading to race conditions where GPU may still be reading from a buffer while CPU writes.
//
// Use TripleBufferedBufferSet for systems that need per-frame isolation with 3 frames in flight.
// The buffer set count should always equal MAX_FRAMES_IN_FLIGHT from Renderer.h.

// =============================================================================
// FrameIndexedBuffers - Type-safe per-frame buffer management
// =============================================================================
//
// This template enforces correct frame-indexed buffer access, preventing the common
// bug where a separate counter (like currentBufferSet_) gets out of sync with frameIndex,
// causing compute and graphics passes to use different buffers.
//
// Key design principles:
// - No parameterless getters: You MUST provide frameIndex to access a buffer
// - No separate counter needed: Buffer selection is always based on frameIndex
// - Compile-time safety: Can't accidentally use the wrong index
//
// Usage:
//   FrameIndexedBuffers buffers;
//   buffers.resize(allocator, frameCount, bufferSize, usage);
//
//   // In recordCulling(frameIndex):
//   vk::Buffer buffer = buffers.get(frameIndex);
//
//   // In render(frameIndex):
//   vk::Buffer buffer = buffers.get(frameIndex);  // Same buffer - guaranteed!
//
// Migration from std::vector<vk::Buffer> + currentBufferSet_:
//   Before (buggy):
//     std::vector<vk::Buffer> buffers_;
//     uint32_t currentBufferSet_ = 0;
//     vk::Buffer getBuffer() { return buffers_[currentBufferSet_]; }  // Can desync!
//     void swap() { currentBufferSet_ = (currentBufferSet_ + 1) % 3; }
//
//   After (safe):
//     FrameIndexedBuffers buffers_;
//     vk::Buffer getBuffer(uint32_t frameIndex) { return buffers_.get(frameIndex); }
//     // No swap() needed - frameIndex is the source of truth
//
class FrameIndexedBuffers {
public:
    FrameIndexedBuffers() = default;
    ~FrameIndexedBuffers() = default;  // each VmaBuffer frees itself

    // Non-copyable (contains Vulkan resources)
    FrameIndexedBuffers(const FrameIndexedBuffers&) = delete;
    FrameIndexedBuffers& operator=(const FrameIndexedBuffers&) = delete;

    // Movable
    FrameIndexedBuffers(FrameIndexedBuffers&& other) noexcept = default;
    FrameIndexedBuffers& operator=(FrameIndexedBuffers&& other) noexcept = default;

    // Allocate buffers for each frame (any previous buffers are released first)
    bool resize(VmaAllocator allocator, uint32_t frameCount, vk::DeviceSize size,
                vk::BufferUsageFlags usage,
                VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY);

    // Release all buffers now. Not required for cleanup (the destructor and
    // resize() do this); kept for callers that drop GPU data early.
    void destroy();

    // =========================================================================
    // SAFE ACCESS - Must provide frameIndex
    // =========================================================================

    // Get buffer for a specific frame (primary access method)
    vk::Buffer get(uint32_t frameIndex) const;

    // Get raw vk::Buffer for APIs that need it
    vk::Buffer getVk(uint32_t frameIndex) const;

    // =========================================================================
    // Utility methods
    // =========================================================================

    bool empty() const { return buffers_.empty(); }
    uint32_t size() const { return frameCount_; }

    // For descriptor set initialization where you need to bind all frames
    vk::Buffer operator[](uint32_t index) const {
        assert(index < frameCount_ && "Index out of bounds");
        return vk::Buffer(buffers_[index].get());
    }

private:
    std::vector<VmaBuffer> buffers_;  // owning: buffer + allocation per frame
    uint32_t frameCount_ = 0;
};

}  // namespace BufferUtils
