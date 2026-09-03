#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "VmaBuffer.h"

// ============================================================================
// DeferredBufferRelease - frame-delayed destruction of GPU resources
// ============================================================================
//
// A system that replaces a buffer while frames are still executing must not
// destroy the old one: command buffers already submitted may read it. Instead
// of vkDeviceWaitIdle (a full pipeline stall on the presenting thread), retire
// the old owner here and call tick() once per frame from the system's record
// call. Retired resources are destroyed once `framesInFlight` ticks have
// elapsed, i.e. after every frame slot that could have referenced them has had
// its fence waited.
//
// Timing argument: a resource retired while frame c is being prepared can be
// referenced by frames <= c at most. tick() runs at the start of recording
// frame c+k, after the fence of frame slot (c+k) % N was waited, which is
// frame c+k-N. With k = N that is frame c itself, and fences are waited in
// frame order, so every frame <= c has completed. Retiring before or after
// this frame's tick() only makes the wait one frame longer, never shorter.
//
// Owners are type-erased through shared_ptr<void>, so any movable RAII type
// works: VmaBuffer, BufferUtils::FrameIndexedBuffers, std::vector<VmaBuffer>...
class DeferredBufferRelease {
public:
    explicit DeferredBufferRelease(uint32_t framesInFlight = 3)
        : framesInFlight_(framesInFlight) {}

    // Not copyable (would double-destroy); movable.
    DeferredBufferRelease(const DeferredBufferRelease&) = delete;
    DeferredBufferRelease& operator=(const DeferredBufferRelease&) = delete;
    DeferredBufferRelease(DeferredBufferRelease&&) noexcept = default;
    DeferredBufferRelease& operator=(DeferredBufferRelease&&) noexcept = default;

    ~DeferredBufferRelease() = default;  // pending owners destroyed with the vector

    void setFramesInFlight(uint32_t framesInFlight) { framesInFlight_ = framesInFlight; }
    uint32_t framesInFlight() const { return framesInFlight_; }

    // Take ownership of a movable RAII object and destroy it later.
    template <typename T>
    void retire(T&& owner) {
        using Owned = std::decay_t<T>;
        entries_.push_back(Entry{tick_, std::make_shared<Owned>(std::move(owner))});
    }

    // Take ownership of a raw VMA buffer/allocation pair.
    void retire(VmaAllocator allocator, vk::Buffer buffer, VmaAllocation allocation) {
        if (!buffer) return;
        retire(VmaBuffer::fromRaw(allocator, buffer, allocation));
    }

    // Call exactly once per frame, at the start of this system's per-frame
    // record/update. Releases everything retired framesInFlight ticks ago.
    void tick() {
        ++tick_;
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(), [this](const Entry& e) {
                return tick_ - e.retiredAtTick >= framesInFlight_;
            }),
            entries_.end());
    }

    // Immediate release. Only valid once the device is known to be idle
    // (shutdown paths).
    void releaseAll() { entries_.clear(); }

    size_t pendingCount() const { return entries_.size(); }

private:
    struct Entry {
        uint64_t retiredAtTick;
        std::shared_ptr<void> resource;
    };

    std::vector<Entry> entries_;
    uint64_t tick_ = 0;
    uint32_t framesInFlight_ = 3;
};
