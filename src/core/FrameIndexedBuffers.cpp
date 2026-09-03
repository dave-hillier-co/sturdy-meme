#include "FrameIndexedBuffers.h"
#include <SDL3/SDL_log.h>

namespace BufferUtils {

bool FrameIndexedBuffers::resize(VmaAllocator allocator, uint32_t frameCount, vk::DeviceSize size,
                                  vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    destroy();

    if (!allocator || frameCount == 0 || size == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameIndexedBuffers::resize: invalid params (allocator=%p, frames=%u, size=%zu)",
            allocator, frameCount, static_cast<size_t>(size));
        return false;
    }

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    buffers_.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        VmaBuffer buffer;
        if (!VmaBuffer::create(allocator, bufferInfo, allocInfo, buffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "FrameIndexedBuffers::resize: failed to create buffer %u", i);
            destroy();  // frees the buffers created so far
            return false;
        }
        buffers_.push_back(std::move(buffer));
    }
    frameCount_ = frameCount;

    return true;
}

void FrameIndexedBuffers::destroy() {
    buffers_.clear();
    frameCount_ = 0;
}

vk::Buffer FrameIndexedBuffers::get(uint32_t frameIndex) const {
    if (buffers_.empty()) return vk::Buffer{};
    return vk::Buffer(buffers_[frameIndex % frameCount_].get());
}

vk::Buffer FrameIndexedBuffers::getVk(uint32_t frameIndex) const {
    return get(frameIndex);
}

}  // namespace BufferUtils
