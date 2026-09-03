#include "FrameExecutor.h"
#include "VulkanContext.h"
#include "vulkan/QueueLock.h"
#include <SDL3/SDL.h>

FrameExecutor::FrameExecutor(ConstructToken, VulkanContext& ctx, TripleBuffering&& frameSync)
    : frameSync_(std::move(frameSync)), vulkanContext_(&ctx) {}

std::unique_ptr<FrameExecutor> FrameExecutor::create(VulkanContext& ctx, uint32_t frameCount) {
    auto frameSync = TripleBuffering::create(ctx.getRaiiDevice(), frameCount);
    if (!frameSync) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FrameExecutor::init: failed to create sync objects");
        return nullptr;
    }
    auto executor = std::make_unique<FrameExecutor>(ConstructToken{}, ctx, std::move(*frameSync));
    // Present-wait semaphores are sized to the swapchain image count, not the
    // frame-in-flight count (see TripleBuffering binary-semaphore notes).
    if (!executor->ensurePresentSemaphores()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FrameExecutor::init: failed to create present semaphores");
        return nullptr;
    }
    SDL_Log("FrameExecutor initialized (%u frames in flight)", frameCount);
    return executor;
}

bool FrameExecutor::ensurePresentSemaphores() {
    uint32_t imageCount = vulkanContext_->getSwapchainImageCount();
    if (imageCount == 0) {
        // Swapchain not ready yet (e.g. minimized); nothing to create.
        return true;
    }
    if (frameSync_.presentSemaphoreCount() == imageCount) {
        return true;
    }
    return frameSync_.initPresentSemaphores(imageCount);
}

FrameResult FrameExecutor::execute(const FrameBuilder& builder) {
    if (windowSuspended_) return FrameResult::Skipped;

    vk::Extent2D extent = vulkanContext_->getVkSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) return FrameResult::Skipped;

    // A swapchain recreate (resize) can change the image count. The recreate
    // path waits for device idle, so it is safe to resize the per-image
    // present-wait semaphores here before any submit/present this frame.
    if (!ensurePresentSemaphores()) return FrameResult::Skipped;

    // Wait for this frame slot to be available
    frameSync_.waitForCurrentFrameIfNeeded();

    // Acquire swapchain image
    uint32_t imageIndex;
    FrameResult acquireResult = acquireImage(imageIndex);
    if (acquireResult != FrameResult::Success) return acquireResult;

    uint32_t frameIndex = frameSync_.currentIndex();

    // Build frame — caller records commands
    vk::CommandBuffer cmd = builder(imageIndex, frameIndex);
    if (!cmd) {
        frameSync_.advance();
        return FrameResult::Skipped;
    }

    // Submit
    FrameResult submitResult = submitCommandBuffer(cmd, imageIndex);
    if (submitResult != FrameResult::Success) return submitResult;

    // Present
    FrameResult presentResult = present(imageIndex);

    // Advance to next frame slot regardless of present result
    frameSync_.advance();

    return presentResult;
}

FrameResult FrameExecutor::acquireImage(uint32_t& imageIndex) {
    vk::Device device = vulkanContext_->getVkDevice();
    vk::SwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    constexpr uint64_t acquireTimeoutNs = 100'000'000; // 100ms
    VkResult vkResult = vkAcquireNextImageKHR(
        device, swapchain, acquireTimeoutNs,
        frameSync_.currentImageAvailableSemaphore(),
        vk::Fence{}, &imageIndex);

    if (vkResult == VK_TIMEOUT || vkResult == VK_NOT_READY) {
        return FrameResult::Skipped;
    } else if (vkResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return FrameResult::SwapchainOutOfDate;
    } else if (vkResult == VK_ERROR_SURFACE_LOST_KHR) {
        return FrameResult::SurfaceLost;
    } else if (vkResult == VK_ERROR_DEVICE_LOST) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan device lost during acquire");
        return FrameResult::DeviceLost;
    } else if (vkResult != VK_SUCCESS && vkResult != VK_SUBOPTIMAL_KHR) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire swapchain image: %d", vkResult);
        return FrameResult::AcquireFailed;
    }

    frameSync_.resetCurrentFence();
    return FrameResult::Success;
}

FrameResult FrameExecutor::submitCommandBuffer(vk::CommandBuffer cmd, uint32_t imageIndex) {
    vk::Queue graphicsQueue = vulkanContext_->getVkGraphicsQueue();
    vk::CommandBuffer vkCmd(cmd);

    vk::Semaphore waitSemaphores[] = {frameSync_.currentImageAvailableSemaphore()};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    // Present-wait semaphore is keyed by swapchain image index (not frame slot)
    // so a pending present is never left waiting on a re-signaled semaphore.
    vk::Semaphore signalSemaphores[] = {
        frameSync_.renderFinishedSemaphoreForImage(imageIndex),
        frameSync_.frameTimelineSemaphore()
    };

    uint64_t timelineSignalValue = frameSync_.nextFrameSignalValue();
    uint64_t waitValues[] = {0};
    uint64_t signalValues[] = {0, timelineSignalValue};

    auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
        .setWaitSemaphoreValueCount(1)
        .setPWaitSemaphoreValues(waitValues)
        .setSignalSemaphoreValueCount(2)
        .setPSignalSemaphoreValues(signalValues);

    auto submitInfo = vk::SubmitInfo{}
        .setPNext(&timelineInfo)
        .setWaitSemaphores(waitSemaphores)
        .setWaitDstStageMask(waitStages)
        .setCommandBuffers(vkCmd)
        .setSignalSemaphores(signalSemaphores);

    try {
        GraphicsQueueLock::Guard lock(GraphicsQueueLock::mutex());
        vk::Queue(graphicsQueue).submit(submitInfo, nullptr);
    } catch (const vk::DeviceLostError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Device lost during queue submit");
        return FrameResult::DeviceLost;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to submit command buffer: %s", e.what());
        return FrameResult::SubmitFailed;
    }

    return FrameResult::Success;
}

FrameResult FrameExecutor::present(uint32_t imageIndex) {
    vk::Queue presentQueue = vulkanContext_->getVkPresentQueue();
    vk::SwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    vk::Semaphore waitSemaphores[] = {frameSync_.renderFinishedSemaphoreForImage(imageIndex)};
    vk::SwapchainKHR swapChains[] = {swapchain};

    auto presentInfo = vk::PresentInfoKHR{}
        .setWaitSemaphores(waitSemaphores)
        .setSwapchains(swapChains)
        .setImageIndices(imageIndex);

    try {
        GraphicsQueueLock::Guard lock(GraphicsQueueLock::mutex());
        auto presentResult = vk::Queue(presentQueue).presentKHR(presentInfo);
        (void)presentResult;
    } catch (const vk::OutOfDateKHRError&) {
        return FrameResult::SwapchainOutOfDate;
    } catch (const vk::SurfaceLostKHRError&) {
        return FrameResult::SurfaceLost;
    } catch (const vk::DeviceLostError&) {
        return FrameResult::DeviceLost;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to present: %s", e.what());
        return FrameResult::SubmitFailed;
    }

    return FrameResult::Success;
}
