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
//   auto executor = FrameExecutor::create(vulkanContext);
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
#include <memory>

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
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    FrameExecutor(ConstructToken, VulkanContext& ctx, TripleBuffering&& frameSync);
    ~FrameExecutor() = default;

    FrameExecutor(const FrameExecutor&) = delete;
    FrameExecutor& operator=(const FrameExecutor&) = delete;
    FrameExecutor(FrameExecutor&&) = delete;
    FrameExecutor& operator=(FrameExecutor&&) = delete;

    // Create the executor and its sync objects. The context must outlive the
    // executor. Returns nullptr on failure.
    static std::unique_ptr<FrameExecutor> create(VulkanContext& ctx,
                                                 uint32_t frameCount = TripleBuffering::DEFAULT_FRAME_COUNT);

    // Execute a complete frame: sync → acquire → build → submit → present → advance.
    // Callback receives (imageIndex, frameIndex) and returns the recorded command buffer.
    // Return a null vk::CommandBuffer from the callback to skip the frame.
    using FrameBuilder = std::function<vk::CommandBuffer(uint32_t imageIndex, uint32_t frameIndex)>;
    FrameResult execute(const FrameBuilder& builder);

    // Frame index for the current frame slot (valid between execute calls)
    uint32_t currentFrameIndex() const { return frameSync_.currentIndex(); }

    // Wait for the previous frame's GPU work (safe to destroy resources after this)
    void waitForPreviousFrame() { frameSync_.waitForPreviousFrame(); }

    void setWindowSuspended(bool suspended) { windowSuspended_ = suspended; }

private:
    FrameResult acquireImage(uint32_t& imageIndex);
    FrameResult submitCommandBuffer(vk::CommandBuffer cmd, uint32_t imageIndex);
    FrameResult present(uint32_t imageIndex);

    // Ensure the per-image present-wait semaphores match the current swapchain
    // image count (recreates them on first use and after a resize that changes
    // the image count). Returns false if creation failed.
    bool ensurePresentSemaphores();

    TripleBuffering frameSync_;
    VulkanContext* vulkanContext_ = nullptr;  // borrowed
    bool windowSuspended_ = false;
};
