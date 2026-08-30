#include "ScreenshotCapture.h"

#include <SDL3/SDL.h>
#include <lodepng.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <utility>
#include <vector>

#include "vulkan/VmaBufferFactory.h"

namespace {

std::string makeScreenshotPath(const std::string& outputDir) {
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);

    std::filesystem::path base = std::filesystem::path(outputDir) / ("screenshot_" + std::string(stamp));
    std::filesystem::path path = base;
    path += ".png";
    for (int n = 1; std::filesystem::exists(path, ec); ++n) {
        path = base;
        path += "_" + std::to_string(n) + ".png";
    }
    return path.string();
}

void encodeAndSave(VmaBuffer buffer, vk::Extent2D extent, vk::Format format,
                   std::string outputDir) {
    const uint32_t width = extent.width;
    const uint32_t height = extent.height;
    const size_t pixelCount = static_cast<size_t>(width) * height;

    vmaInvalidateAllocation(buffer.allocator(), buffer.getAllocation(), 0, VK_WHOLE_SIZE);
    const uint8_t* src = static_cast<const uint8_t*>(buffer.map());
    if (!src) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: failed to map staging buffer");
        return;
    }

    const bool bgra = format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eB8G8R8A8Unorm;
    std::vector<uint8_t> rgba(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = src[i * 4 + (bgra ? 2 : 0)];
        rgba[i * 4 + 1] = src[i * 4 + 1];
        rgba[i * 4 + 2] = src[i * 4 + (bgra ? 0 : 2)];
        rgba[i * 4 + 3] = 255;  // swapchain alpha is undefined for opaque composite
    }
    buffer.unmap();
    buffer.reset();

    std::string path = makeScreenshotPath(outputDir);
    unsigned error = lodepng::encode(path, rgba, width, height);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: PNG encode failed for %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return;
    }
    SDL_Log("Screenshot saved: %s (%ux%u)", path.c_str(), width, height);
}

}  // namespace

ScreenshotCapture::~ScreenshotCapture() {
    if (saveThread_.joinable()) {
        saveThread_.join();
    }
}

void ScreenshotCapture::recordIfRequested(vk::CommandBuffer cmd, vk::Image swapchainImage,
                                          vk::Extent2D extent, vk::Format format,
                                          uint32_t frameIndex) {
    if (!requested_.exchange(false)) return;
    if (pending_) {
        // Previous capture not read back yet; drop this request rather than queue.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: capture already in flight, ignoring request");
        return;
    }
    if (!swapchainImage || extent.width == 0 || extent.height == 0) return;

    vk::DeviceSize size = vk::DeviceSize(extent.width) * extent.height * 4;
    if (!VmaBufferFactory::createReadbackBuffer(allocator_, size, stagingBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: failed to create readback buffer");
        return;
    }

    auto subresource = vk::ImageSubresourceRange{}
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setLevelCount(1)
        .setLayerCount(1);

    auto toTransfer = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
        .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
        .setOldLayout(vk::ImageLayout::ePresentSrcKHR)
        .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(swapchainImage)
        .setSubresourceRange(subresource);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, toTransfer);

    auto region = vk::BufferImageCopy{}
        .setImageSubresource(vk::ImageSubresourceLayers{}
                                 .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                 .setLayerCount(1))
        .setImageExtent(vk::Extent3D{extent.width, extent.height, 1});
    cmd.copyImageToBuffer(swapchainImage, vk::ImageLayout::eTransferSrcOptimal,
                          vk::Buffer(stagingBuffer_.get()), region);

    auto toPresent = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
        .setDstAccessMask({})
        .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(swapchainImage)
        .setSubresourceRange(subresource);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, toPresent);

    pending_ = true;
    pendingFrameIndex_ = frameIndex;
    pendingExtent_ = extent;
    pendingFormat_ = format;
}

void ScreenshotCapture::pollCompleted(uint32_t frameIndex) {
    if (!pending_ || frameIndex != pendingFrameIndex_) return;
    pending_ = false;

    if (saveThread_.joinable()) {
        saveThread_.join();
    }
    // Hand the staging buffer to the worker: swizzle + PNG encode happen
    // off the presenting thread, and the buffer is destroyed there.
    saveThread_ = std::thread(encodeAndSave, std::move(stagingBuffer_),
                              pendingExtent_, pendingFormat_, outputDir_);
}
