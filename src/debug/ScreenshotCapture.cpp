#include "ScreenshotCapture.h"

#include "core/vulkan/VulkanContext.h"
#include "core/vulkan/CommandBufferUtils.h"
#include "core/vulkan/VmaBuffer.h"

#include <SDL3/SDL_log.h>
#include <stb_image_write.h>

#include <vector>

namespace ScreenshotCapture {

bool captureSwapchainImage(VulkanContext& context, uint32_t imageIndex,
                           const std::string& outputPath) {
    if (imageIndex >= context.getSwapchainImageCount()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ScreenshotCapture: image index %u out of range", imageIndex);
        return false;
    }

    const vk::Device device = context.getVkDevice();
    const vk::Extent2D extent = context.getVkSwapchainExtent();
    const vk::Format format = context.getVkSwapchainImageFormat();
    const vk::Image image = context.getSwapchainImage(imageIndex);
    const vk::DeviceSize bufferSize =
        static_cast<vk::DeviceSize>(extent.width) * extent.height * 4;

    // Host-visible staging buffer for the readback
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(bufferSize)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaBuffer staging;
    if (!VmaBuffer::create(context.getAllocator(), bufferInfo, allocInfo, staging)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ScreenshotCapture: failed to create staging buffer");
        return false;
    }

    CommandScope cmdScope(device, context.getCommandPool(), context.getVkGraphicsQueue());
    if (!cmdScope.begin()) return false;
    vk::CommandBuffer cmd = cmdScope.get();

    const auto subresourceRange = vk::ImageSubresourceRange{}
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0).setLevelCount(1)
        .setBaseArrayLayer(0).setLayerCount(1);

    // PresentSrc -> TransferSrc
    auto toTransfer = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
        .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
        .setOldLayout(vk::ImageLayout::ePresentSrcKHR)
        .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(subresourceRange);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe,
                        vk::PipelineStageFlagBits::eTransfer,
                        {}, nullptr, nullptr, toTransfer);

    auto region = vk::BufferImageCopy{}
        .setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(vk::ImageSubresourceLayers{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1))
        .setImageOffset({0, 0, 0})
        .setImageExtent({extent.width, extent.height, 1});
    cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal,
                          vk::Buffer(staging.get()), region);

    // TransferSrc -> PresentSrc (restore for the presentation engine)
    auto toPresent = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
        .setDstAccessMask(vk::AccessFlagBits::eMemoryRead)
        .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(subresourceRange);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eBottomOfPipe,
                        {}, nullptr, nullptr, toPresent);

    if (!cmdScope.end()) return false;

    // Read back and convert to tightly packed RGBA8
    VmaAllocationInfo mappedInfo = {};
    vmaGetAllocationInfo(context.getAllocator(), staging.getAllocation(), &mappedInfo);
    if (!mappedInfo.pMappedData) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ScreenshotCapture: staging buffer not mapped");
        return false;
    }
    vmaInvalidateAllocation(context.getAllocator(), staging.getAllocation(), 0, VK_WHOLE_SIZE);

    const uint8_t* src = static_cast<const uint8_t*>(mappedInfo.pMappedData);
    std::vector<uint8_t> rgba(bufferSize);
    const bool bgra = (format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eB8G8R8A8Unorm);
    for (size_t px = 0; px < static_cast<size_t>(extent.width) * extent.height; ++px) {
        const uint8_t* in = src + px * 4;
        uint8_t* out = rgba.data() + px * 4;
        if (bgra) {
            out[0] = in[2];
            out[1] = in[1];
            out[2] = in[0];
        } else {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
        }
        out[3] = 255;  // Composite alpha modes can leave alpha at 0; force opaque
    }

    if (!stbi_write_png(outputPath.c_str(), static_cast<int>(extent.width),
                        static_cast<int>(extent.height), 4, rgba.data(),
                        static_cast<int>(extent.width) * 4)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ScreenshotCapture: failed to write %s", outputPath.c_str());
        return false;
    }
    return true;
}

} // namespace ScreenshotCapture
