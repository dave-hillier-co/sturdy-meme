#pragma once

#include "PerFrameBuffer.h"
#include "core/vulkan/VmaBuffer.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <utility>
#include <vector>

namespace BufferUtils {

// RAII owner for N per-frame host-visible buffers with persistent mapped
// pointers - the self-cleaning counterpart of PerFrameBufferSet.
//
// Creation goes through PerFrameBufferBuilder (identical usage/memory flags and
// log messages); the raw handles are then adopted into VmaBuffer so the buffers
// are freed by the destructor. Move-only, so a set can be handed to
// DeferredBufferRelease::retire() while frames in flight may still read it.
class MappedFrameBuffers {
public:
    MappedFrameBuffers() = default;

    MappedFrameBuffers(const MappedFrameBuffers&) = delete;
    MappedFrameBuffers& operator=(const MappedFrameBuffers&) = delete;
    MappedFrameBuffers(MappedFrameBuffers&&) noexcept = default;
    MappedFrameBuffers& operator=(MappedFrameBuffers&&) noexcept = default;

    // Build with the given builder; `allocator` must be the one the builder was
    // configured with (it owns the resulting allocations). Replaces `out` only
    // on success.
    static bool build(VmaAllocator allocator, const PerFrameBufferBuilder& builder,
                      MappedFrameBuffers& out) {
        PerFrameBufferSet raw{};
        if (!builder.build(raw)) {
            return false;
        }
        out = adopt(allocator, std::move(raw));
        return true;
    }

    // Take ownership of buffers created by PerFrameBufferBuilder.
    static MappedFrameBuffers adopt(VmaAllocator allocator, PerFrameBufferSet&& raw) {
        MappedFrameBuffers result;
        result.buffers_.reserve(raw.buffers.size());
        for (size_t i = 0; i < raw.buffers.size(); ++i) {
            result.buffers_.push_back(VmaBuffer::fromRaw(allocator, raw.buffers[i], raw.allocations[i]));
        }
        result.mapped_ = std::move(raw.mappedPointers);
        raw = PerFrameBufferSet{};
        return result;
    }

    vk::Buffer get(uint32_t frameIndex) const { return buffers_[frameIndex].get(); }
    void* mapped(uint32_t frameIndex) const { return mapped_[frameIndex]; }
    VmaAllocation allocation(uint32_t frameIndex) const { return buffers_[frameIndex].getAllocation(); }

    uint32_t size() const { return static_cast<uint32_t>(buffers_.size()); }
    bool empty() const { return buffers_.empty(); }

    // Copy of the raw handles, for APIs that take a std::vector<vk::Buffer>.
    std::vector<vk::Buffer> handles() const {
        std::vector<vk::Buffer> out;
        out.reserve(buffers_.size());
        for (const auto& b : buffers_) out.push_back(b.get());
        return out;
    }

    // Destroy the buffers now (only when the device is known not to read them).
    void reset() {
        buffers_.clear();
        mapped_.clear();
    }

private:
    std::vector<VmaBuffer> buffers_;
    std::vector<void*> mapped_;
};

}  // namespace BufferUtils
