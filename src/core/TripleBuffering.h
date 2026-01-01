#pragma once

// ============================================================================
// TripleBuffering.h - Frame synchronization using FrameBuffered template
// ============================================================================
//
// This header provides a specialized TripleBuffering class that uses the
// generic FrameBuffered<T> template for managing Vulkan synchronization
// primitives (fences and semaphores) per frame.
//
// The class encapsulates:
// - Per-frame synchronization primitives (fences, semaphores)
// - Frame index management and cycling
// - Convenient methods for common synchronization patterns
//
// Usage:
//   TripleBuffering frames;
//   if (!frames.init(device)) return false;
//
//   // In render loop:
//   frames.waitForCurrentFrameIfNeeded();
//   uint32_t idx = frames.currentIndex();
//   // ... record commands using idx for buffer selection ...
//   frames.resetCurrentFence();
//   // ... submit commands with frames.currentImageAvailableSemaphore() ...
//   frames.advance();
//

#include "FrameBuffered.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>

// ============================================================================
// FrameSyncPrimitives - Per-frame synchronization resources
// ============================================================================

struct FrameSyncPrimitives {
    std::optional<vk::raii::Semaphore> imageAvailable;
    std::optional<vk::raii::Semaphore> renderFinished;
    std::optional<vk::raii::Fence> inFlightFence;

    // Non-copyable (Vulkan resources)
    FrameSyncPrimitives() = default;
    FrameSyncPrimitives(const FrameSyncPrimitives&) = delete;
    FrameSyncPrimitives& operator=(const FrameSyncPrimitives&) = delete;

    // Movable
    FrameSyncPrimitives(FrameSyncPrimitives&&) noexcept = default;
    FrameSyncPrimitives& operator=(FrameSyncPrimitives&&) noexcept = default;
};

// ============================================================================
// TripleBuffering - Manages frame-in-flight synchronization using FrameBuffered
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
    // Synchronization - Fences
    // =========================================================================

    // Get fence for current frame
    VkFence currentFence() const {
        return **frames_.current().inFlightFence;
    }

    // Get fence for any frame index
    VkFence fence(uint32_t frameIndex) const {
        return **frames_.at(frameIndex).inFlightFence;
    }

    // Check if current frame's fence is already signaled (non-blocking)
    bool isCurrentFenceSignaled() const {
        return frames_.current().inFlightFence->getStatus() == vk::Result::eSuccess;
    }

    // Wait for current frame's fence (blocks until signaled)
    void waitForCurrentFrame() const {
        (void)device_->waitForFences(**frames_.current().inFlightFence, vk::True, UINT64_MAX);
    }

    // Wait for current frame only if not already signaled (optimization)
    void waitForCurrentFrameIfNeeded() const {
        if (!isCurrentFenceSignaled()) {
            waitForCurrentFrame();
        }
    }

    // Wait for previous frame's fence (useful before destroying resources)
    void waitForPreviousFrame() const {
        const auto& prev = frames_.previous();
        if (prev.inFlightFence->getStatus() != vk::Result::eSuccess) {
            (void)device_->waitForFences(**prev.inFlightFence, vk::True, UINT64_MAX);
        }
    }

    // Reset current frame's fence (call before queue submit)
    void resetCurrentFence() const {
        device_->resetFences(**frames_.current().inFlightFence);
    }

    // =========================================================================
    // Synchronization - Semaphores
    // =========================================================================

    // Get image available semaphore for current frame
    VkSemaphore currentImageAvailableSemaphore() const {
        return **frames_.current().imageAvailable;
    }

    // Get render finished semaphore for current frame
    VkSemaphore currentRenderFinishedSemaphore() const {
        return **frames_.current().renderFinished;
    }

    // Get semaphores for any frame index
    VkSemaphore imageAvailableSemaphore(uint32_t frameIndex) const {
        return **frames_.at(frameIndex).imageAvailable;
    }

    VkSemaphore renderFinishedSemaphore(uint32_t frameIndex) const {
        return **frames_.at(frameIndex).renderFinished;
    }

    // =========================================================================
    // Direct Access (for compatibility with existing code)
    // =========================================================================

    // Access the underlying FrameBuffered container
    const FrameBuffered<FrameSyncPrimitives>& frames() const { return frames_; }
    FrameBuffered<FrameSyncPrimitives>& frames() { return frames_; }

private:
    const vk::raii::Device* device_ = nullptr;
    FrameBuffered<FrameSyncPrimitives> frames_;
};
