#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <vector>

#include "PerFrameBuffer.h"
#include "VmaBuffer.h"

// RAII owner for N-per-frame VMA buffers with persistent mapped pointers.
//
// Built from BufferUtils::PerFrameBufferBuilder (same creation semantics:
// usage, memory usage and allocation flags), then each raw buffer/allocation
// pair is adopted into a VmaBuffer so the set frees itself in its destructor.
// Mapped pointers are only non-null when the builder requested
// VMA_ALLOCATION_CREATE_MAPPED_BIT (the builder default).
struct PerFrameOwnedBuffers {
    std::vector<VmaBuffer> buffers;
    std::vector<void*> mapped;

    vk::Buffer buffer(size_t i) const { return buffers[i].get(); }
    VmaAllocation allocation(size_t i) const { return buffers[i].getAllocation(); }
    size_t size() const { return buffers.size(); }
    bool empty() const { return buffers.empty(); }

    void reset() {
        mapped.clear();
        buffers.clear();
    }

    // Build with the given builder (which must already carry the allocator).
    // On failure the owner is left untouched.
    static bool build(VmaAllocator allocator,
                      const BufferUtils::PerFrameBufferBuilder& builder,
                      PerFrameOwnedBuffers& out) {
        BufferUtils::PerFrameBufferSet set{};
        if (!builder.build(set)) {
            return false;
        }

        PerFrameOwnedBuffers result;
        result.buffers.reserve(set.buffers.size());
        for (size_t i = 0; i < set.buffers.size(); ++i) {
            result.buffers.push_back(VmaBuffer::fromRaw(allocator, set.buffers[i], set.allocations[i]));
        }
        result.mapped = std::move(set.mappedPointers);
        out = std::move(result);
        return true;
    }
};
