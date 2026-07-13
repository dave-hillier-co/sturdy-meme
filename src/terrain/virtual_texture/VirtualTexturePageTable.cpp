#include "VirtualTexturePageTable.h"
#include "CommandBufferUtils.h"
#include "VmaBufferFactory.h"
#include "SamplerFactory.h"
#include "VmaImageHandle.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>

namespace VirtualTexture {

std::unique_ptr<VirtualTexturePageTable> VirtualTexturePageTable::create(const InitInfo& info) {
    auto pageTable = std::make_unique<VirtualTexturePageTable>(ConstructToken{});
    if (!pageTable->initInternal(info)) {
        return nullptr;
    }
    return pageTable;
}

VirtualTexturePageTable::~VirtualTexturePageTable() {
    cleanup();
}

bool VirtualTexturePageTable::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTexturePageTable::initInternal requires raiiDevice");
        return false;
    }

    raiiDevice_ = info.raiiDevice;
    config = info.config;
    device_ = info.device;
    allocator_ = info.allocator;
    framesInFlight_ = info.framesInFlight;

    vk::Device device = info.device;
    VmaAllocator allocator = info.allocator;
    vk::CommandPool commandPool = info.commandPool;
    vk::Queue queue = info.queue;

    // Calculate total entries and offsets for each mip level
    size_t totalEntries = 0;
    mipOffsets.resize(config.maxMipLevels);
    mipSizes.resize(config.maxMipLevels);
    // Every mip starts dirty: the GPU image contents are undefined until the
    // first upload, and a garbage entry with a non-zero valid byte makes the
    // shader treat the tile as resident - it samples uninitialized cache
    // memory (magenta) and never writes feedback, so the tile is never
    // requested and the garbage persists. Uploading the zeroed CPU table over
    // every layer on the first frame prevents that.
    mipDirty.resize(config.maxMipLevels, true);
    dirty = true;

    for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
        mipOffsets[mip] = totalEntries;
        uint32_t tilesAtMip = config.getTilesAtMip(mip);
        mipSizes[mip] = tilesAtMip * tilesAtMip;
        totalEntries += mipSizes[mip];
    }

    // Initialize CPU data
    cpuData.resize(totalEntries);
    for (auto& entry : cpuData) {
        entry.valid = 0;
        entry.cacheX = 0;
        entry.cacheY = 0;
    }

    // Create page table textures
    if (!createPageTableTextures(device, allocator, commandPool, queue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT page table textures");
        return false;
    }

    // Create sampler
    if (!createSampler(device)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT page table sampler");
        return false;
    }

    // Create per-frame staging buffers, sized for the worst case of every
    // mip level being dirty in the same frame (each dirty mip gets its own
    // region of the buffer)
    vk::DeviceSize stagingSize = totalEntries * sizeof(uint32_t); // RGBA8 packed
    stagingBuffers_.resize(framesInFlight_);
    stagingMapped_.resize(framesInFlight_);

    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        if (!VmaBufferFactory::createStagingBuffer(allocator, stagingSize, stagingBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT page table staging buffer %u", i);
            return false;
        }
        stagingMapped_[i] = stagingBuffers_[i].map();
    }

    SDL_Log("VirtualTexturePageTable initialized: %u mip levels, %zu total entries, %u staging buffers",
            config.maxMipLevels, totalEntries, framesInFlight_);

    return true;
}

void VirtualTexturePageTable::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;  // Not initialized
    // VmaBuffer cleanup - unmap first
    for (size_t i = 0; i < stagingBuffers_.size(); ++i) {
        if (stagingMapped_[i]) {
            stagingBuffers_[i].unmap();
            stagingMapped_[i] = nullptr;
        }
        stagingBuffers_[i].reset();
    }
    stagingBuffers_.clear();
    stagingMapped_.clear();

    pageTableSampler_.reset();
    pageTableImage_.reset();

    cpuData.clear();
    mipOffsets.clear();
    mipSizes.clear();
    mipDirty.clear();
}

bool VirtualTexturePageTable::createPageTableTextures(vk::Device device, VmaAllocator allocator,
                                                       vk::CommandPool commandPool, vk::Queue queue) {
    // Single array image: every layer is sized for mip 0; mip N's entries
    // occupy the top-left corner of layer N. This wastes a little memory but
    // lets the shader address all mips through one usampler2DArray.
    uint32_t tilesPerAxis = config.getTilesPerAxis();

    VmaImageSpec spec{};
    spec = spec.withFormat(vk::Format::eR8G8B8A8Uint)
        .withExtent(vk::Extent3D{tilesPerAxis, tilesPerAxis, 1})
        .withMipLevels(1)
        .withArrayLayers(config.maxMipLevels)
        .withUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
        .withView(vk::ImageViewType::e2DArray, vk::ImageAspectFlagBits::eColor,
                  0, 1, 0, config.maxMipLevels)
        .withRequiredFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    pageTableImage_ = spec.build(allocator, device);
    if (!pageTableImage_.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create page table array image");
        return false;
    }

    // Transition all layers to shader read layout
    {
        CommandScope cmdScope(device, commandPool, queue);
        if (!cmdScope.begin()) return false;

        vk::CommandBuffer vkCmd(cmdScope.get());
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(pageTableImage_.getImage())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(config.maxMipLevels));

        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                              vk::PipelineStageFlagBits::eFragmentShader,
                              {}, {}, {}, barrier);

        if (!cmdScope.end()) return false;
    }

    return true;
}

bool VirtualTexturePageTable::createSampler(vk::Device device) {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTexturePageTable::createSampler requires raiiDevice");
        return false;
    }

    auto sampler = SamplerFactory::createSamplerNearestClamp(*raiiDevice_);
    if (!sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create page table sampler");
        return false;
    }
    pageTableSampler_ = std::move(*sampler);
    return true;
}

size_t VirtualTexturePageTable::getEntryIndex(TileId id) const {
    if (id.mipLevel >= config.maxMipLevels) return SIZE_MAX;

    uint32_t tilesAtMip = config.getTilesAtMip(id.mipLevel);
    if (id.x >= tilesAtMip || id.y >= tilesAtMip) return SIZE_MAX;

    return mipOffsets[id.mipLevel] + id.y * tilesAtMip + id.x;
}

void VirtualTexturePageTable::setEntry(TileId id, uint16_t cacheX, uint16_t cacheY) {
    size_t index = getEntryIndex(id);
    if (index == SIZE_MAX) return;

    cpuData[index].cacheX = cacheX;
    cpuData[index].cacheY = cacheY;
    cpuData[index].valid = 1;

    mipDirty[id.mipLevel] = true;
    dirty = true;
}

void VirtualTexturePageTable::clearEntry(TileId id) {
    size_t index = getEntryIndex(id);
    if (index == SIZE_MAX) return;

    cpuData[index].valid = 0;
    cpuData[index].cacheX = 0;
    cpuData[index].cacheY = 0;

    mipDirty[id.mipLevel] = true;
    dirty = true;
}

PageTableEntry VirtualTexturePageTable::getEntry(TileId id) const {
    size_t index = getEntryIndex(id);
    if (index == SIZE_MAX) return PageTableEntry{};
    return cpuData[index];
}

void VirtualTexturePageTable::recordUpload(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!dirty) return;

    // Select the staging buffer for this frame to avoid race conditions
    uint32_t bufferIndex = frameIndex % framesInFlight_;
    if (bufferIndex >= stagingBuffers_.size() || !stagingMapped_[bufferIndex]) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid page table staging buffer index %u", bufferIndex);
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    auto fullRange = vk::ImageSubresourceRange{}
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(config.maxMipLevels);

    // Transition all layers to transfer dst
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(pageTableImage_.getImage())
            .setSubresourceRange(fullRange);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                              vk::PipelineStageFlagBits::eTransfer,
                              {}, {}, {}, barrier);
    }

    // Each dirty mip gets its own region of the staging buffer; the copy
    // commands execute long after this function returns, so regions must
    // not be reused within a frame
    uint32_t* staging = static_cast<uint32_t*>(stagingMapped_[bufferIndex]);
    size_t stagingCursor = 0;

    for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
        if (!mipDirty[mip]) continue;

        uint32_t tilesAtMip = config.getTilesAtMip(mip);
        size_t numEntries = mipSizes[mip];

        for (size_t i = 0; i < numEntries; ++i) {
            staging[stagingCursor + i] = cpuData[mipOffsets[mip] + i].packRGBA8();
        }

        // Copy into this mip's layer (data sits in the top-left corner)
        auto region = vk::BufferImageCopy{}
            .setBufferOffset(stagingCursor * sizeof(uint32_t))
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource(vk::ImageSubresourceLayers{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(0)
                .setBaseArrayLayer(mip)
                .setLayerCount(1))
            .setImageOffset({0, 0, 0})
            .setImageExtent({tilesAtMip, tilesAtMip, 1});
        vkCmd.copyBufferToImage(stagingBuffers_[bufferIndex].get(), pageTableImage_.getImage(),
                                vk::ImageLayout::eTransferDstOptimal, region);

        stagingCursor += numEntries;
        mipDirty[mip] = false;
    }

    // Transition back to shader read
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(pageTableImage_.getImage())
            .setSubresourceRange(fullRange);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eFragmentShader,
                              {}, {}, {}, barrier);
    }

    dirty = false;
}

} // namespace VirtualTexture
