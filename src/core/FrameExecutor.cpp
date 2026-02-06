#include "FrameExecutor.h"
#include "VulkanContext.h"
#include <SDL3/SDL.h>

bool FrameExecutor::init(VulkanContext* ctx, uint32_t frameCount) {
    if (!ctx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FrameExecutor::init: null VulkanContext");
        return false;
    }
    vulkanContext_ = ctx;
    if (!frameSync_.init(ctx->getRaiiDevice(), frameCount)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FrameExecutor::init: failed to create sync objects");
        return false;
    }
    SDL_Log("FrameExecutor initialized (%u frames in flight)", frameCount);
    return true;
}

void FrameExecutor::destroy() {
    frameSync_.destroy();
    vulkanContext_ = nullptr;
}

void FrameExecutor::prepareForResize() {
    frameSync_.waitForAllFrames();
    frameSync_.resetForResize();
}

FrameResult FrameExecutor::execute(const FrameBuilder& builder) {
    if (windowSuspended_) return FrameResult::Skipped;

    VkExtent2D extent = vulkanContext_->getVkSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) return FrameResult::Skipped;

    // Wait for this frame slot to be available
    frameSync_.waitForCurrentFrameIfNeeded();

    // Acquire swapchain image
    uint32_t imageIndex;
    FrameResult acquireResult = acquireImage(imageIndex);
    if (acquireResult != FrameResult::Success) return acquireResult;

    uint32_t frameIndex = frameSync_.currentIndex();

    // Build frame — caller records commands
    VkCommandBuffer cmd = builder(imageIndex, frameIndex);
    if (cmd == VK_NULL_HANDLE) {
        frameSync_.advance();
        return FrameResult::Skipped;
    }

    // Submit
    FrameResult submitResult = submitCommandBuffer(cmd);
    if (submitResult != FrameResult::Success) return submitResult;

    // Present
    FrameResult presentResult = present(imageIndex);

    // Advance to next frame slot regardless of present result
    frameSync_.advance();

    return presentResult;
}

FrameResult FrameExecutor::acquireImage(uint32_t& imageIndex) {
    VkDevice device = vulkanContext_->getVkDevice();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    constexpr uint64_t acquireTimeoutNs = 100'000'000; // 100ms
    VkResult vkResult = vkAcquireNextImageKHR(
        device, swapchain, acquireTimeoutNs,
        frameSync_.currentImageAvailableSemaphore(),
        VK_NULL_HANDLE, &imageIndex);

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

FrameResult FrameExecutor::submitCommandBuffer(VkCommandBuffer cmd) {
    VkQueue graphicsQueue = vulkanContext_->getVkGraphicsQueue();
    vk::CommandBuffer vkCmd(cmd);

    vk::Semaphore waitSemaphores[] = {frameSync_.currentImageAvailableSemaphore()};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {
        frameSync_.currentRenderFinishedSemaphore(),
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
    VkQueue presentQueue = vulkanContext_->getVkPresentQueue();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();

    vk::Semaphore waitSemaphores[] = {frameSync_.currentRenderFinishedSemaphore()};
    vk::SwapchainKHR swapChains[] = {swapchain};

    auto presentInfo = vk::PresentInfoKHR{}
        .setWaitSemaphores(waitSemaphores)
        .setSwapchains(swapChains)
        .setImageIndices(imageIndex);

    try {
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
