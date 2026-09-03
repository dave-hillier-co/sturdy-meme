#pragma once

// ============================================================================
// TripleBuffering.h - Frame synchronization using timeline semaphores
// ============================================================================
//
// This header provides a specialized TripleBuffering class that uses Vulkan 1.2
// timeline semaphores for efficient frame synchronization with non-blocking
// completion checks.
//
// Timeline semaphore advantages over fence-based sync:
// - Non-blocking counter queries via vkGetSemaphoreCounterValue
// - Single semaphore object instead of per-frame fences
// - Cleaner synchronization model with monotonic values
// - Lower overhead than fence polling
//
// Usage:
//   TripleBuffering frames;
//   if (!frames.init(device)) return false;
//
//   // In render loop:
//   frames.waitForCurrentFrameIfNeeded();
//   uint32_t idx = frames.currentIndex();
//   // ... record commands using idx for buffer selection ...
//   // ... submit commands with frames.currentFrameSignalValue() in timeline submit info ...
//   frames.advance();
//

#include "FrameBuffered.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <array>

// ============================================================================
// FrameSyncPrimitives - Per-frame synchronization resources
// ============================================================================

struct FrameSyncPrimitives {
    std::optional<vk::raii::Semaphore> imageAvailable;     // Binary semaphore for swapchain acquire

    // Non-copyable (Vulkan resources)
    FrameSyncPrimitives() = default;
    FrameSyncPrimitives(const FrameSyncPrimitives&) = delete;
    FrameSyncPrimitives& operator=(const FrameSyncPrimitives&) = delete;

    // Movable
    FrameSyncPrimitives(FrameSyncPrimitives&&) noexcept = default;
    FrameSyncPrimitives& operator=(FrameSyncPrimitives&&) noexcept = default;
};

// ============================================================================
// TripleBuffering - Manages frame-in-flight synchronization using timeline semaphores
// ============================================================================

class TripleBuffering {
public:
    // Default to 3 frames in flight (triple buffering)
    static constexpr uint32_t DEFAULT_FRAME_COUNT = 3;

    TripleBuffering() = default;
    ~TripleBuffering() = default;

    // Non-copyable (contains Vulkan resources)
    TripleBuffering(const TripleBuffering&) = delete;
    TripleBuffering& operator=(const TripleBuffering&) = delete;

    // Movable
    TripleBuffering(TripleBuffering&&) noexcept = default;
    TripleBuffering& operator=(TripleBuffering&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    // Initialize synchronization primitives for the specified frame count
    // Returns false on failure
    bool init(const vk::raii::Device& device, uint32_t frameCount = DEFAULT_FRAME_COUNT);

    // Clean up synchronization primitives
    void destroy();

    // Check if initialized
    bool isInitialized() const { return !frames_.empty() && device_ != nullptr; }

    // =========================================================================
    // Frame Index Management (delegated to FrameBuffered)
    // =========================================================================

    uint32_t frameCount() const { return frames_.frameCount(); }
    uint32_t currentIndex() const { return frames_.currentIndex(); }
    uint32_t previousIndex() const { return frames_.previousIndex(); }
    uint32_t nextIndex() const { return frames_.nextIndex(); }
    uint32_t wrapIndex(uint32_t index) const { return frames_.wrapIndex(index); }

    void advance() { frames_.advance(); }
    void reset() { frames_.reset(); }

    // Get pointer to current frame index (for legacy code needing pointer access)
    const uint32_t* currentIndexPtr() const { return frames_.currentIndexPtr(); }

    // =========================================================================
    // Synchronization - Timeline Semaphore (Vulkan 1.2)
    // =========================================================================

    // Get the timeline semaphore used for frame completion tracking
    vk::Semaphore frameTimelineSemaphore() const {
        return frameTimeline_ ? **frameTimeline_ : vk::Semaphore{};
    }

    // Get current frame's timeline counter value to wait for (non-blocking check)
    uint64_t currentFrameWaitValue() const {
        return frameSignalValues_[frames_.currentIndex()];
    }

    // Get the signal value to use for the current frame's queue submit
    // Call this before submitting, it increments the counter
    uint64_t nextFrameSignalValue() {
        uint64_t value = ++globalFrameCounter_;
        frameSignalValues_[frames_.currentIndex()] = value;
        return value;
    }

    // Get the current GPU-side timeline counter value (non-blocking)
    uint64_t getTimelineCounterValue() const;

    // Check if current frame's work has completed (non-blocking)
    // This is the key function for polling-based frame sync
    bool isCurrentFrameComplete() const {
        return getTimelineCounterValue() >= currentFrameWaitValue();
    }

    // Wait for current frame to complete (blocking)
    void waitForCurrentFrame() const;

    // Wait for current frame only if not already complete (optimization)
    void waitForCurrentFrameIfNeeded() const {
        if (!isCurrentFrameComplete()) {
            waitForCurrentFrame();
        }
    }

    // Wait for previous frame to complete (useful before destroying resources)
    void waitForPreviousFrame() const;

    // Check if all frames in flight have completed
    bool areAllFramesComplete() const {
        return getTimelineCounterValue() >= globalFrameCounter_;
    }

    // Wait for all frames in flight to complete
    void waitForAllFrames() const;

    // =========================================================================
    // Synchronization - Binary Semaphores (for swapchain)
    // =========================================================================
    //
    // The image-available semaphore is per-frame-in-flight slot: it is signaled
    // by vkAcquireNextImageKHR before the image index is known, so it must be
    // tied to the frame slot.
    //
    // The render-finished (present-wait) semaphore is per *swapchain image*, NOT
    // per frame slot. Present completion is not tracked by the frame timeline
    // semaphore, so a per-slot semaphore could be re-signaled by a new submit
    // while an earlier present is still waiting on it (the classic WSI hazard the
    // validation layers flag as VUID-vkQueuePresentKHR / sync-hazard). Indexing
    // by image index is safe because the presentation engine cannot return an
    // image from vkAcquireNextImageKHR until its previous present has completed.

    // (Re)create the per-image present-wait semaphores. Call once the swapchain
    // image count is known, and again on resize if the image count changed.
    // Requires init() to have been called first (needs a valid device).
    bool initPresentSemaphores(uint32_t imageCount);

    // Number of per-image present-wait semaphores currently allocated
    uint32_t presentSemaphoreCount() const {
        return static_cast<uint32_t>(presentSemaphores_.size());
    }

    // Get image available semaphore for current frame
    vk::Semaphore currentImageAvailableSemaphore() const {
        return **frames_.current().imageAvailable;
    }

    // Get the render-finished (present-wait) semaphore for a swapchain image
    vk::Semaphore renderFinishedSemaphoreForImage(uint32_t imageIndex) const {
        return *presentSemaphores_[imageIndex];
    }

    // Get image available semaphore for any frame index
    vk::Semaphore imageAvailableSemaphore(uint32_t frameIndex) const {
        return **frames_.at(frameIndex).imageAvailable;
    }

    // =========================================================================
    // Legacy Fence API (compatibility shim using timeline semaphore)
    // =========================================================================

    // These methods provide backward compatibility for code expecting fences.
    // They now use the timeline semaphore internally.

    // Legacy: Check if current frame's work is done (non-blocking)
    // Replacement for fence status check
    bool isCurrentFenceSignaled() const { return isCurrentFrameComplete(); }

    // Legacy: Get "fence" for queue submit - now returns a timeline semaphore
    // Note: callers must update to use timeline semaphore submit info
    vk::Semaphore currentFence() const { return frameTimelineSemaphore(); }

    // Legacy: Reset fence before submit - now a no-op (timeline semaphores don't reset)
    void resetCurrentFence() const { /* no-op for timeline semaphores */ }

    // =========================================================================
    // Timeline Submit Helpers
    // =========================================================================

    // Create TimelineSemaphoreSubmitInfo for queue submit
    // Chain this to SubmitInfo::pNext and use frameTimelineSemaphore() in signalSemaphores
    vk::TimelineSemaphoreSubmitInfo createTimelineSubmitInfo(
        uint64_t signalValue,
        const uint64_t* waitValues = nullptr,
        uint32_t waitCount = 0) const;

    // =========================================================================
    // Direct Access (for compatibility with existing code)
    // =========================================================================

    // Access the underlying FrameBuffered container
    const FrameBuffered<FrameSyncPrimitives>& frames() const { return frames_; }
    FrameBuffered<FrameSyncPrimitives>& frames() { return frames_; }

private:
    const vk::raii::Device* device_ = nullptr;
    FrameBuffered<FrameSyncPrimitives> frames_;

    // Timeline semaphore for frame completion tracking (Vulkan 1.2)
    std::optional<vk::raii::Semaphore> frameTimeline_;

    // Per-swapchain-image present-wait (render-finished) binary semaphores.
    // Indexed by swapchain image index, not frame-in-flight slot. See the
    // binary-semaphore section above for the WSI hazard this avoids.
    std::vector<vk::raii::Semaphore> presentSemaphores_;

    // Per-frame signal values (what value was signaled when this frame was submitted)
    std::array<uint64_t, 8> frameSignalValues_{};  // Supports up to 8 frames in flight

    // Global monotonically increasing frame counter
    uint64_t globalFrameCounter_ = 0;
};
