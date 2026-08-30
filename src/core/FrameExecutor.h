#pragma once

// ============================================================================
// FrameExecutor.h - Owns the complete frame lifecycle
// ============================================================================
//
// FrameExecutor owns TripleBuffering and runs the per-frame loop:
//   sync → acquire → callback → submit → present → advance
//
// The caller provides a callback that records commands and returns
// the command buffer. Everything else is handled internally.
//
// Usage:
//   FrameExecutor executor;
//   executor.init(vulkanContext);
//
//   // In render loop:
//   FrameResult result = executor.execute(
//       [&](uint32_t imageIndex, uint32_t frameIndex) {
//           return recordCommands(imageIndex, frameIndex);
//       });
//

#include "TripleBuffering.h"
#include <vulkan/vulkan.hpp>
#include <functional>

class VulkanContext;

enum class FrameResult {
    Success,
    SwapchainOutOfDate,
    SurfaceLost,
    DeviceLost,
    AcquireFailed,
    SubmitFailed,
    Skipped
};

class FrameExecutor {
public:
    FrameExecutor() = default;
    ~FrameExecutor() = default;

    FrameExecutor(const FrameExecutor&) = delete;
    FrameExecutor& operator=(const FrameExecutor&) = delete;
    FrameExecutor(FrameExecutor&&) noexcept = default;
    FrameExecutor& operator=(FrameExecutor&&) noexcept = default;

    bool init(VulkanContext* ctx, uint32_t frameCount = TripleBuffering::DEFAULT_FRAME_COUNT);
    void destroy();

    // Execute a complete frame: sync → acquire → build → submit → present → advance.
    // Callback receives (imageIndex, frameIndex) and returns the recorded command buffer.
    // Return VK_NULL_HANDLE from the callback to skip the frame.
    using FrameBuilder = std::function<vk::CommandBuffer(uint32_t imageIndex, uint32_t frameIndex)>;
    FrameResult execute(const FrameBuilder& builder);

    // Frame index for the current frame slot (valid between execute calls)
    uint32_t currentFrameIndex() const { return frameSync_.currentIndex(); }

    // Wait for the previous frame's GPU work (safe to destroy resources after this)
    void waitForPreviousFrame() { frameSync_.waitForPreviousFrame(); }

    // Call before swapchain recreation: waits for all GPU work and resets frame state
    void prepareForResize();

    void setWindowSuspended(bool suspended) { windowSuspended_ = suspended; }

    // Swapchain image index used by the most recent successfully presented frame.
    // Valid only after execute() returned Success.
    uint32_t lastPresentedImageIndex() const { return lastPresentedImageIndex_; }

private:
    FrameResult acquireImage(uint32_t& imageIndex);
    FrameResult submitCommandBuffer(vk::CommandBuffer cmd, uint32_t imageIndex);
    FrameResult present(uint32_t imageIndex);

    // Ensure the per-image present-wait semaphores match the current swapchain
    // image count (recreates them on first use and after a resize that changes
    // the image count). Returns false if creation failed.
    bool ensurePresentSemaphores();

    TripleBuffering frameSync_;
    VulkanContext* vulkanContext_ = nullptr;
    bool windowSuspended_ = false;
    uint32_t lastPresentedImageIndex_ = 0;
};
