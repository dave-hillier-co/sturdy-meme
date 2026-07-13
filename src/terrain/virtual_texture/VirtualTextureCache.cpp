#include "VirtualTextureCache.h"
#include "VmaBufferFactory.h"
#include "SamplerFactory.h"
#include "VmaImageHandle.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>
#include <utility>

namespace VirtualTexture {

std::unique_ptr<VirtualTextureCache> VirtualTextureCache::create(const InitInfo& info) {
    auto cache = std::make_unique<VirtualTextureCache>(ConstructToken{});
    if (!cache->initInternal(info)) {
        return nullptr;
    }
    return cache;
}

VirtualTextureCache::~VirtualTextureCache() {
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

    cacheSampler_.reset();

    cacheImage_.reset();

    slots.clear();
    tileToSlot.clear();
}

VirtualTextureCache::VirtualTextureCache(VirtualTextureCache&& other) noexcept
    : device_(other.device_)
    , allocator_(other.allocator_)
    , config(other.config)
    , useCompression_(other.useCompression_)
    , raiiDevice_(other.raiiDevice_)
    , cacheImage_(std::move(other.cacheImage_))
    , cacheSampler_(std::move(other.cacheSampler_))
    , stagingBuffers_(std::move(other.stagingBuffers_))
    , stagingMapped_(std::move(other.stagingMapped_))
    , stagingCursor_(other.stagingCursor_)
    , maxUploadsPerFrame_(other.maxUploadsPerFrame_)
    , framesInFlight_(other.framesInFlight_)
    , slots(std::move(other.slots))
    , tileToSlot(std::move(other.tileToSlot))
{
    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = VK_NULL_HANDLE;
    other.cacheImage_ = {};
}

VirtualTextureCache& VirtualTextureCache::operator=(VirtualTextureCache&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        for (size_t i = 0; i < stagingBuffers_.size(); ++i) {
            if (stagingMapped_[i]) {
                stagingBuffers_[i].unmap();
            }
            stagingBuffers_[i].reset();
        }
        cacheSampler_.reset();
        cacheImage_.reset();

        // Move from other
        device_ = other.device_;
        allocator_ = other.allocator_;
        config = other.config;
        useCompression_ = other.useCompression_;
        raiiDevice_ = other.raiiDevice_;
        cacheImage_ = std::move(other.cacheImage_);
        cacheSampler_ = std::move(other.cacheSampler_);
        stagingBuffers_ = std::move(other.stagingBuffers_);
        stagingMapped_ = std::move(other.stagingMapped_);
        stagingCursor_ = other.stagingCursor_;
        maxUploadsPerFrame_ = other.maxUploadsPerFrame_;
        framesInFlight_ = other.framesInFlight_;
        slots = std::move(other.slots);
        tileToSlot = std::move(other.tileToSlot);

        other.device_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
        other.cacheImage_ = {};
    }
    return *this;
}

bool VirtualTextureCache::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTextureCache::init requires raiiDevice");
        return false;
    }

    device_ = info.device;
    allocator_ = info.allocator;
    raiiDevice_ = info.raiiDevice;
    config = info.config;
    framesInFlight_ = info.framesInFlight;
    useCompression_ = info.useCompression;

    VkDevice device = info.device;
    VmaAllocator allocator = info.allocator;
    VkCommandPool commandPool = info.commandPool;
    VkQueue queue = info.queue;

    // Initialize slot array
    uint32_t totalSlots = config.getTotalCacheSlots();
    slots.resize(totalSlots);

    uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
    for (uint32_t i = 0; i < totalSlots; ++i) {
        slots[i].occupied = false;
        slots[i].lastUsedFrame = 0;
    }

    // Create the cache texture
    if (!createCacheTexture(device, allocator, commandPool, queue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT cache texture");
        return false;
    }

    // Create sampler
    if (!createSampler(device)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT cache sampler");
        return false;
    }

    // Create per-frame staging buffers to avoid race conditions with in-flight
    // frames. Each buffer holds one region per upload that can be recorded in
    // a frame, since the copy commands execute after recording finishes.
    maxUploadsPerFrame_ = info.maxUploadsPerFrame;
    VkDeviceSize stagingSize = getTileStagingSize() * maxUploadsPerFrame_;

    stagingBuffers_.resize(framesInFlight_);
    stagingMapped_.resize(framesInFlight_);

    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        if (!VmaBufferFactory::createStagingBuffer(allocator, stagingSize, stagingBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT staging buffer %u", i);
            return false;
        }
        stagingMapped_[i] = stagingBuffers_[i].map();
    }

    SDL_Log("VirtualTextureCache initialized: %u slots (%ux%u tiles), %upx cache, %u staging buffers, format: %s",
            totalSlots, slotsPerAxis, slotsPerAxis, config.cacheSizePixels, framesInFlight_,
            useCompression_ ? "BC1" : "RGBA8");

    return true;
}

bool VirtualTextureCache::createCacheTexture(VkDevice device, VmaAllocator allocator,
                                              VkCommandPool commandPool, VkQueue queue) {
    vk::Format cacheFormat = useCompression_ ? vk::Format::eBc1RgbSrgbBlock : vk::Format::eR8G8B8A8Srgb;
    VmaImageSpec spec{};
    spec = spec.withFormat(cacheFormat)
        .withExtent(vk::Extent3D{config.cacheSizePixels, config.cacheSizePixels, 1})
        .withMipLevels(1)
        .withUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
        .withView(vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
        .withRequiredFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    cacheImage_ = spec.build(allocator, device);
    if (!cacheImage_.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT cache image");
        return false;
    }

    // Transition to shader read layout
    vk::Device vkDevice(device);
    auto cmdAllocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(commandPool)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    vk::CommandBuffer vkCmd = vkDevice.allocateCommandBuffers(cmdAllocInfo).front();

    auto beginInfo = vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    vkCmd.begin(beginInfo);

    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cacheImage_.getImage())
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, barrier);

    vkCmd.end();

    vk::Queue vkQueue(queue);
    auto submitInfo = vk::SubmitInfo{}
        .setCommandBuffers(vkCmd);

    vkQueue.submit(submitInfo, VK_NULL_HANDLE);
    vkQueue.waitIdle();

    vkDevice.freeCommandBuffers(commandPool, vkCmd);

    return true;
}

bool VirtualTextureCache::createSampler(VkDevice device) {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTextureCache::createSampler requires raiiDevice");
        return false;
    }

    auto sampler = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cache sampler");
        return false;
    }
    cacheSampler_ = std::move(*sampler);
    return true;
}

CacheSlot* VirtualTextureCache::allocateSlot(TileId id, uint32_t currentFrame,
                                             std::optional<TileId>* evictedTile) {
    uint32_t packed = id.pack();

    // Keep the coarsest mip level permanently resident so the shader's mip
    // fallback chain always finds a real tile (never the magenta debug color).
    const bool shouldPin = (id.mipLevel + 1u >= config.maxMipLevels);

    // Check if already in cache
    auto it = tileToSlot.find(packed);
    if (it != tileToSlot.end()) {
        slots[it->second].lastUsedFrame = currentFrame;
        return &slots[it->second];
    }

    // Find an empty slot first
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].occupied) {
            slots[i].occupied = true;
            slots[i].tileId = id;
            slots[i].lastUsedFrame = currentFrame;
            slots[i].pinned = shouldPin;
            tileToSlot[packed] = i;
            return &slots[i];
        }
    }

    // No empty slots, evict LRU
    size_t lruIndex = findLRUSlot(currentFrame);
    if (lruIndex < slots.size()) {
        // Remove old mapping and report the eviction so the caller can
        // invalidate the evicted tile's page table entry
        TileId oldTile = slots[lruIndex].tileId;
        tileToSlot.erase(oldTile.pack());
        if (evictedTile) {
            *evictedTile = oldTile;
        }

        // Set new tile
        slots[lruIndex].tileId = id;
        slots[lruIndex].lastUsedFrame = currentFrame;
        slots[lruIndex].pinned = shouldPin;
        tileToSlot[packed] = lruIndex;
        return &slots[lruIndex];
    }

    return nullptr;
}

void VirtualTextureCache::markUsed(TileId id, uint32_t currentFrame) {
    auto it = tileToSlot.find(id.pack());
    if (it != tileToSlot.end()) {
        slots[it->second].lastUsedFrame = currentFrame;
    }
}

bool VirtualTextureCache::hasTile(TileId id) const {
    return tileToSlot.find(id.pack()) != tileToSlot.end();
}

const CacheSlot* VirtualTextureCache::getSlot(TileId id) const {
    auto it = tileToSlot.find(id.pack());
    if (it != tileToSlot.end()) {
        return &slots[it->second];
    }
    return nullptr;
}

size_t VirtualTextureCache::findLRUSlot(uint32_t currentFrame) const {
    size_t lruIndex = slots.size();
    uint32_t oldestFrame = UINT32_MAX;

    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].occupied) continue;
        // Never evict pinned slots (the always-resident coarse mip tail)
        if (slots[i].pinned) continue;
        // Skip slots in-flight frames may still sample: their page table
        // version still maps this slot until those frames complete
        if (slots[i].lastUsedFrame + framesInFlight_ > currentFrame) continue;
        if (slots[i].lastUsedFrame < oldestFrame) {
            oldestFrame = slots[i].lastUsedFrame;
            lruIndex = i;
        }
    }

    return lruIndex;
}

VkDeviceSize VirtualTextureCache::getTileStagingSize() const {
    if (useCompression_) {
        // BC1: 8 bytes per 4x4 block
        uint32_t blockWidth = (config.tileSizePixels + 3) / 4;
        uint32_t blockHeight = (config.tileSizePixels + 3) / 4;
        return blockWidth * blockHeight * 8;
    }
    // RGBA8: 4 bytes per pixel
    return config.tileSizePixels * config.tileSizePixels * 4;
}

void VirtualTextureCache::beginFrame() {
    stagingCursor_ = 0;
}

bool VirtualTextureCache::recordTileUpload(TileId id, const void* pixelData,
                                            uint32_t width, uint32_t height,
                                            TileFormat format, VkCommandBuffer cmd, uint32_t frameIndex) {
    // Find the slot for this tile
    auto it = tileToSlot.find(id.pack());
    if (it == tileToSlot.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot upload tile not in cache");
        return false;
    }

    // Check format compatibility
    bool tileIsCompressed = (format != TileFormat::RGBA8);
    if (tileIsCompressed != useCompression_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Tile format mismatch: tile is %s, cache is %s",
            tileIsCompressed ? "compressed" : "RGBA8",
            useCompression_ ? "BC1" : "RGBA8");
        return false;
    }

    // An oversized tile would overwrite neighboring cache slots
    if (width > config.tileSizePixels || height > config.tileSizePixels) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Tile %ux%u exceeds cache slot size %u", width, height, config.tileSizePixels);
        return false;
    }

    // Select the staging buffer for this frame to avoid race conditions
    uint32_t bufferIndex = frameIndex % framesInFlight_;
    if (bufferIndex >= stagingBuffers_.size() || !stagingMapped_[bufferIndex]) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid staging buffer index %u", bufferIndex);
        return false;
    }

    size_t slotIndex = it->second;
    uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
    uint32_t slotX = slotIndex % slotsPerAxis;
    uint32_t slotY = slotIndex / slotsPerAxis;

    // Calculate data size based on format
    VkDeviceSize dataSize;
    if (useCompression_) {
        // BC1: 8 bytes per 4x4 block
        uint32_t blockWidth = (width + 3) / 4;
        uint32_t blockHeight = (height + 3) / 4;
        dataSize = blockWidth * blockHeight * 8;
    } else {
        // RGBA8: 4 bytes per pixel
        dataSize = width * height * 4;
    }

    // Sub-allocate a region of the per-frame staging buffer. Regions can't be
    // reused within a frame: the copy commands execute long after recording.
    VkDeviceSize stagingCapacity = getTileStagingSize() * maxUploadsPerFrame_;
    if (stagingCursor_ + dataSize > stagingCapacity) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "VT staging buffer exhausted for this frame (%u uploads max)",
                    maxUploadsPerFrame_);
        return false;
    }
    VkDeviceSize stagingOffset = stagingCursor_;
    stagingCursor_ += dataSize;

    std::memcpy(static_cast<uint8_t*>(stagingMapped_[bufferIndex]) + stagingOffset,
                pixelData, dataSize);

    vk::CommandBuffer vkCmd(cmd);

    // Copy buffer to image region at tile slot position. The image is in
    // TransferDstOptimal here - the caller brackets the batch with
    // recordUploadBatchBegin/End.
    {
        auto region = vk::BufferImageCopy{}
            .setBufferOffset(stagingOffset)
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource(vk::ImageSubresourceLayers{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(0)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setImageOffset({static_cast<int32_t>(slotX * config.tileSizePixels),
                            static_cast<int32_t>(slotY * config.tileSizePixels), 0})
            .setImageExtent({width, height, 1});
        vkCmd.copyBufferToImage(stagingBuffers_[bufferIndex].get(), cacheImage_.getImage(),
                                vk::ImageLayout::eTransferDstOptimal, region);
    }

    return true;
}

void VirtualTextureCache::recordUploadBatchBegin(VkCommandBuffer cmd) {
    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
        .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cacheImage_.getImage())
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    vk::CommandBuffer(cmd).pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                                           vk::PipelineStageFlagBits::eTransfer,
                                           {}, {}, {}, barrier);
}

void VirtualTextureCache::recordUploadBatchEnd(VkCommandBuffer cmd) {
    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(cacheImage_.getImage())
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    vk::CommandBuffer(cmd).pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                           vk::PipelineStageFlagBits::eFragmentShader,
                                           {}, {}, {}, barrier);
}

uint32_t VirtualTextureCache::getUsedSlotCount() const {
    uint32_t count = 0;
    for (const auto& slot : slots) {
        if (slot.occupied) ++count;
    }
    return count;
}

} // namespace VirtualTexture
