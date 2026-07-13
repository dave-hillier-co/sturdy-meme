#include "VirtualTextureSystem.h"
#include "VmaBufferFactory.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>

namespace VirtualTexture {

bool VirtualTextureSystem::init(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VirtualTextureSystem::init requires raiiDevice");
        return false;
    }

    config = info.config;
    framesInFlight_ = info.framesInFlight;

    SDL_Log("Initializing VirtualTextureSystem...");
    SDL_Log("  Virtual size: %u px", config.virtualSizePixels);
    SDL_Log("  Tile size: %u px", config.tileSizePixels);
    SDL_Log("  Cache size: %u px", config.cacheSizePixels);
    SDL_Log("  Max mip levels: %u", config.maxMipLevels);
    SDL_Log("  Frames in flight: %u", framesInFlight_);

    // Initialize cache with RAII wrapper
    VirtualTextureCache::InitInfo cacheInfo;
    cacheInfo.raiiDevice = info.raiiDevice;
    cacheInfo.device = info.device;
    cacheInfo.allocator = info.allocator;
    cacheInfo.commandPool = info.commandPool;
    cacheInfo.queue = info.queue;
    cacheInfo.config = config;
    cacheInfo.framesInFlight = framesInFlight_;
    cacheInfo.useCompression = false;
    cacheInfo.maxUploadsPerFrame = MAX_UPLOADS_PER_FRAME;

    cache = VirtualTextureCache::create(cacheInfo);
    if (!cache) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT cache");
        return false;
    }

    // Initialize page table
    VirtualTexturePageTable::InitInfo ptInfo;
    ptInfo.raiiDevice = info.raiiDevice;
    ptInfo.device = info.device;
    ptInfo.allocator = info.allocator;
    ptInfo.commandPool = info.commandPool;
    ptInfo.queue = info.queue;
    ptInfo.config = config;
    ptInfo.framesInFlight = framesInFlight_;

    pageTable = VirtualTexturePageTable::create(ptInfo);
    if (!pageTable) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT page table");
        return false;
    }

    // Initialize feedback (use framesInFlight for buffering)
    feedback = VirtualTextureFeedback::create(info.device, info.allocator, 4096, framesInFlight_);
    if (!feedback) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT feedback");
        return false;
    }

    // Initialize tile loader
    tileLoader = VirtualTextureTileLoader::create(info.tilePath, 2);
    if (!tileLoader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT tile loader");
        return false;
    }

    // Create and fill the params UBO (constant for the system's lifetime)
    if (!VmaBufferFactory::createUniformBuffer(info.allocator, sizeof(VTParamsUBO), paramsBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT params UBO");
        return false;
    }
    {
        VTParamsUBO params = getParams();
        void* mapped = paramsBuffer_.map();
        std::memcpy(mapped, &params, sizeof(params));
        paramsBuffer_.unmap();
    }

    // Preload the coarsest mip level. These tiles are pinned in the cache
    // (see VirtualTextureCache::allocateSlot) so they stay resident for the
    // system's lifetime, guaranteeing the shader's mip fallback chain always
    // terminates in a real tile instead of the magenta debug color - this is
    // what prevents pink terrain when fine tiles haven't streamed in yet.
    {
        uint32_t coarsestMip = config.maxMipLevels - 1;
        uint32_t tilesPerAxis = config.getTilesAtMip(coarsestMip);
        for (uint32_t y = 0; y < tilesPerAxis; ++y) {
            for (uint32_t x = 0; x < tilesPerAxis; ++x) {
                requestTile(TileId(static_cast<uint16_t>(x),
                                   static_cast<uint16_t>(y),
                                   static_cast<uint8_t>(coarsestMip)));
            }
        }
        SDL_Log("  Preloading %u coarsest-mip (mip %u) fallback tiles",
                tilesPerAxis * tilesPerAxis, coarsestMip);
    }

    SDL_Log("VirtualTextureSystem initialized successfully");
    return true;
}

void VirtualTextureSystem::destroy(VkDevice device, VmaAllocator allocator) {
    // RAII-managed subsystems are destroyed automatically via std::optional reset
    tileLoader.reset();
    feedback.reset();
    pageTable.reset();
    cache.reset();
    paramsBuffer_.reset();
    pendingTiles.clear();
    pendingUploads_.clear();
}

void VirtualTextureSystem::beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Clear the feedback buffer for this frame
    feedback->clear(cmd, frameIndex);
}

void VirtualTextureSystem::endFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Memory barrier to ensure shader writes are visible before transfer
    vk::CommandBuffer vkCmd(cmd);
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eTransferRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                          vk::PipelineStageFlagBits::eTransfer,
                          {}, barrier, {}, {});

    // Copy feedback buffer from GPU storage to CPU readback buffer
    // The actual CPU read will happen in a future frame after fence wait
    feedback->recordCopyToReadback(cmd, frameIndex);
}

void VirtualTextureSystem::update(VkCommandBuffer cmd, uint32_t frameIndex) {
    currentFrame++;

    // Process feedback from a COMPLETED frame. The only fence-safe slot is
    // frameIndex itself: it was last written framesInFlight frames ago and
    // the caller has just waited on that frame's fence. Any other slot may
    // still be in flight on the GPU.
    // For the first few frames, we skip feedback processing since the slot
    // hasn't been written yet.
    if (currentFrame > framesInFlight_) {
        processFeedback(frameIndex);
    }

    // Record upload commands for any tiles that finished loading
    recordPendingTileUploads(cmd, frameIndex);

    // Record page table upload commands if dirty
    pageTable->recordUpload(cmd, frameIndex);
}

void VirtualTextureSystem::processFeedback(uint32_t frameIndex) {
    // Read back tile requests from GPU
    feedback->readback(frameIndex);

    // Get deduplicated, sorted list of requested tiles
    std::vector<TileId> requested = feedback->getRequestedTiles();

    if (requested.empty()) {
        // No requests - relax penalty if we have headroom
        if (currentPenalty > 0.0f && pendingTiles.empty()) {
            currentPenalty = std::max(0.0f, currentPenalty - PENALTY_RELAX_RATE);
        }
        return;
    }

    // Calculate cache pressure: how many tiles we're trying to load vs capacity
    uint32_t totalCacheSlots = config.getTotalCacheSlots();
    uint32_t usedSlots = cache->getUsedSlotCount();
    uint32_t pendingCount = static_cast<uint32_t>(pendingTiles.size());

    // Count how many new tiles we'd be requesting
    uint32_t newRequestCount = 0;
    for (const auto& id : requested) {
        if (!cache->hasTile(id) &&
            pendingTiles.find(id.pack()) == pendingTiles.end() &&
            !tileLoader->isQueued(id)) {
            newRequestCount++;
        }
    }

    // Apply penalty scheme: increase penalty if we're over budget
    float projectedUsage = static_cast<float>(usedSlots + pendingCount + newRequestCount) /
                           static_cast<float>(totalCacheSlots);

    // Target 80% cache utilization to leave headroom
    constexpr float TARGET_UTILIZATION = 0.8f;

    if (projectedUsage > TARGET_UTILIZATION) {
        // Increase penalty to request coarser mips
        currentPenalty = std::min(MAX_PENALTY, currentPenalty + PENALTY_INCREMENT);
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "VT: Over budget (%.1f%% projected), penalty now %.1f mip levels",
                     projectedUsage * 100.0f, currentPenalty);
    } else if (currentPenalty > 0.0f && projectedUsage < TARGET_UTILIZATION * 0.5f) {
        // Relax penalty when well under budget
        currentPenalty = std::max(0.0f, currentPenalty - PENALTY_RELAX_RATE);
    }

    uint32_t queued = 0;
    for (const auto& id : requested) {
        if (queued >= MAX_REQUESTS_PER_FRAME) {
            break;
        }

        // Apply penalty: shift requested mip level coarser
        TileId adjustedId = id;
        if (currentPenalty > 0.0f) {
            uint8_t penaltyMips = static_cast<uint8_t>(currentPenalty);
            adjustedId.mipLevel = std::min(
                static_cast<uint8_t>(config.maxMipLevels - 1),
                static_cast<uint8_t>(id.mipLevel + penaltyMips)
            );

            // Adjust tile coordinates for the new mip level
            if (adjustedId.mipLevel > id.mipLevel) {
                uint8_t mipDiff = adjustedId.mipLevel - id.mipLevel;
                adjustedId.x = id.x >> mipDiff;
                adjustedId.y = id.y >> mipDiff;
            }
        }

        uint32_t packed = adjustedId.pack();

        // Skip if already in cache
        if (cache->hasTile(adjustedId)) {
            cache->markUsed(adjustedId, currentFrame);
            continue;
        }

        // Skip if already pending
        if (pendingTiles.find(packed) != pendingTiles.end()) {
            continue;
        }

        // Skip if already queued for loading
        if (tileLoader->isQueued(adjustedId)) {
            continue;
        }

        // Queue for loading with priority based on mip level
        // Lower mip = larger tile = higher priority
        int priority = static_cast<int>(adjustedId.mipLevel);
        tileLoader->queueTile(adjustedId, priority);
        pendingTiles.insert(packed);
        queued++;
    }

    if (queued > 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "VT: Queued %u new tile requests (penalty: %.1f)", queued, currentPenalty);
    }
}

void VirtualTextureSystem::recordPendingTileUploads(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Collect tiles that finished loading; anything beyond this frame's
    // upload budget stays in the queue and is retried next frame
    for (auto& tile : tileLoader->getLoadedTiles()) {
        pendingUploads_.push_back(std::move(tile));
    }

    if (pendingUploads_.empty()) {
        return;
    }

    cache->beginFrame();

    uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
    uint32_t uploaded = 0;

    // One transition pair brackets the whole batch; per-tile barriers caused
    // MoltenVK to split Metal encoders dozens of times per frame (blocky
    // stale-tile corruption on Apple GPUs)
    bool batchOpen = false;

    while (!pendingUploads_.empty() && uploaded < MAX_UPLOADS_PER_FRAME) {
        LoadedTile& tile = pendingUploads_.front();
        uint32_t packed = tile.id.pack();

        // Allocate cache slot, invalidating the evicted tile's page table
        // entry if eviction occurred
        std::optional<TileId> evicted;
        CacheSlot* slot = cache->allocateSlot(tile.id, currentFrame, &evicted);
        if (!slot) {
            // Every slot is occupied by recently-used tiles; retry next frame
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                         "VT: Cache full, deferring %zu tile uploads", pendingUploads_.size());
            break;
        }
        if (evicted) {
            pageTable->clearEntry(*evicted);
        }

        // Record tile upload commands into the main command buffer
        // This uses per-frame staging buffers to avoid race conditions
        if (!batchOpen) {
            cache->recordUploadBatchBegin(cmd);
            batchOpen = true;
        }
        bool ok = cache->recordTileUpload(tile.id, tile.pixels.data(),
                                          tile.width, tile.height,
                                          tile.format, cmd, frameIndex);
        if (ok) {
            // Update page table (CPU-side, will be uploaded via recordUpload)
            uint32_t slotIndex = cache->getTileSlotIndex(tile.id);
            if (slotIndex != UINT32_MAX) {
                uint16_t cacheX = slotIndex % slotsPerAxis;
                uint16_t cacheY = slotIndex / slotsPerAxis;
                pageTable->setEntry(tile.id, cacheX, cacheY);
            }
            uploaded++;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "VT: Dropping tile (%u,%u) mip %u - upload failed",
                        tile.id.x, tile.id.y, tile.id.mipLevel);
        }

        // Whether uploaded or dropped, this tile is no longer pending
        pendingTiles.erase(packed);
        pendingUploads_.pop_front();
    }

    if (batchOpen) {
        cache->recordUploadBatchEnd(cmd);
    }

    if (uploaded > 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "VT: Recorded %u tile uploads", uploaded);
    }
}

VTParamsUBO VirtualTextureSystem::getParams() const {
    VTParamsUBO params{};

    float virtSize = static_cast<float>(config.virtualSizePixels);
    float cacheSize = static_cast<float>(config.cacheSizePixels);
    float tileSize = static_cast<float>(config.tileSizePixels);
    float border = static_cast<float>(config.borderPixels);

    // xy = size, zw = 1/size
    params.virtualTextureSizeAndInverse = glm::vec4(
        virtSize, virtSize, 1.0f / virtSize, 1.0f / virtSize
    );

    // xy = size, zw = 1/size
    params.physicalCacheSizeAndInverse = glm::vec4(
        cacheSize, cacheSize, 1.0f / cacheSize, 1.0f / cacheSize
    );

    // x = tile size, y = border, z = tile with border, w = unused
    params.tileSizeAndBorder = glm::vec4(
        tileSize, border, tileSize + border * 2.0f, 0.0f
    );

    params.maxMipLevel = config.maxMipLevels - 1;

    return params;
}

void VirtualTextureSystem::requestTile(TileId id) {
    if (!cache->hasTile(id) && !tileLoader->isQueued(id)) {
        tileLoader->queueTile(id, 0); // High priority
        pendingTiles.insert(id.pack());
    }
}

} // namespace VirtualTexture
