#pragma once

#include <vulkan/vulkan.hpp>
#include <atomic>
#include <string>
#include <thread>

#include "vulkan/VmaBuffer.h"

/**
 * ScreenshotCapture - native swapchain screen grabs.
 *
 * Flow (no main-thread stall):
 *  1. request() flags a capture (safe from the event/input path).
 *  2. recordIfRequested() is called while recording the frame's command buffer,
 *     after the swapchain render pass. It appends a copy of the swapchain image
 *     into a fresh host-readable staging buffer.
 *  3. pollCompleted() is called each frame after the frame slot's fence has been
 *     waited. When the capturing slot comes around again the GPU copy is
 *     guaranteed complete; the staging buffer is handed to a worker thread that
 *     converts to RGBA and encodes a PNG into the output directory.
 */
class ScreenshotCapture {
public:
    ScreenshotCapture(VmaAllocator allocator, std::string outputDir)
        : allocator_(allocator), outputDir_(std::move(outputDir)) {}
    ~ScreenshotCapture();

    ScreenshotCapture(const ScreenshotCapture&) = delete;
    ScreenshotCapture& operator=(const ScreenshotCapture&) = delete;

    // Flag a capture for the next recorded frame. Thread-safe.
    void request() { requested_ = true; }

    // Record the swapchain-image -> staging-buffer copy if a capture is
    // requested. The image must be in ePresentSrcKHR layout (i.e. call after
    // the swapchain render pass, before cmd.end()).
    void recordIfRequested(vk::CommandBuffer cmd, vk::Image swapchainImage,
                           vk::Extent2D extent, vk::Format format, uint32_t frameIndex);

    // Call once per frame after the fence wait for frameIndex. Kicks off PNG
    // encoding on a worker thread when the pending capture's data is ready.
    void pollCompleted(uint32_t frameIndex);

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    std::string outputDir_;

    std::atomic<bool> requested_{false};

    // Pending GPU->staging copy, consumed when its frame slot recycles.
    bool pending_ = false;
    uint32_t pendingFrameIndex_ = 0;
    vk::Extent2D pendingExtent_{};
    vk::Format pendingFormat_ = vk::Format::eUndefined;
    VmaBuffer stagingBuffer_;

    std::thread saveThread_;
};
