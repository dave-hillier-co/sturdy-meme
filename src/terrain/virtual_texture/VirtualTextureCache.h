#pragma once

#include "VirtualTextureTypes.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include "VmaBuffer.h"
#include "VmaImageHandle.h"

namespace VirtualTexture {

/**
 * VirtualTextureCache manages the physical tile cache texture.
 *
 * The cache can use either RGBA8 or BC1 compressed format.
 * BC1 uses 4x less GPU memory but requires all tiles to be BC1 compressed.
 * It uses LRU eviction when the cache is full.
 */
class VirtualTextureCache {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit VirtualTextureCache(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VirtualTextureConfig config;
        uint32_t framesInFlight = 3;
        bool useCompression = false;
        // Staging capacity: maximum tile uploads recorded per frame
        uint32_t maxUploadsPerFrame = 16;
    };

    // Factory method - returns nullptr on failure
    static std::unique_ptr<VirtualTextureCache> create(const InitInfo& info);

    ~VirtualTextureCache();

    // Move-only
    VirtualTextureCache(VirtualTextureCache&& other) noexcept;
    VirtualTextureCache& operator=(VirtualTextureCache&& other) noexcept;
    VirtualTextureCache(const VirtualTextureCache&) = delete;
    VirtualTextureCache& operator=(const VirtualTextureCache&) = delete;

    // Allocate a slot for a new tile, evicting LRU if needed.
    // Slots used within the last framesInFlight frames are never evicted
    // (in-flight frames may still sample them), so allocation can fail.
    // If an eviction occurred, evictedTile receives the evicted tile's id so
    // the caller can invalidate its page table entry.
    // Returns the cache slot, or nullptr if allocation failed.
    CacheSlot* allocateSlot(TileId id, uint32_t currentFrame,
                            std::optional<TileId>* evictedTile = nullptr);

    // Mark a tile as used this frame (for LRU tracking)
    void markUsed(TileId id, uint32_t currentFrame);

    // Check if a tile is in the cache
    bool hasTile(TileId id) const;

    // Get the cache slot for a tile (nullptr if not in cache)
    const CacheSlot* getSlot(TileId id) const;

    /**
     * Record tile upload commands into the provided command buffer.
     * Uses fence-based synchronization - caller is responsible for submitting
     * the command buffer and waiting on the appropriate frame fence.
     *
     * @param id Tile ID to upload
     * @param pixelData Pixel data to upload (RGBA8 or BC1 compressed)
     * @param width Tile width
     * @param height Tile height
     * @param format Tile format (RGBA8 or BC1)
     * @param cmd Command buffer to record into (must be in recording state)
     * @param frameIndex Current frame index for staging buffer selection
     * @return false if the upload could not be recorded (format mismatch,
     *         staging capacity exhausted, tile not in cache)
     */
    bool recordTileUpload(TileId id, const void* pixelData, uint32_t width, uint32_t height,
                          TileFormat format, VkCommandBuffer cmd, uint32_t frameIndex);

    // Bracket a batch of recordTileUpload calls with a single pair of layout
    // transitions instead of one pair per tile. Dozens of image barriers per
    // frame force MoltenVK to split Metal encoders repeatedly, which showed up
    // as blocky stale-tile corruption on Apple GPUs.
    void recordUploadBatchBegin(VkCommandBuffer cmd);
    void recordUploadBatchEnd(VkCommandBuffer cmd);

    // Reset the staging sub-allocation cursor for this frame. Must be called
    // once per frame before any recordTileUpload calls.
    void beginFrame();

    // Check if the cache is using compressed BC1 format
    bool isCompressed() const { return useCompression_; }

    /**
     * Get the number of staging buffers (one per frame in flight)
     */
    uint32_t getStagingBufferCount() const { return static_cast<uint32_t>(stagingBuffers_.size()); }

    // Get the cache texture image view
    VkImageView getCacheImageView() const { return cacheImage_.getView(); }

    // Get the sampler for the cache texture
    VkSampler getCacheSampler() const { return cacheSampler_ ? **cacheSampler_ : VK_NULL_HANDLE; }

    // Get the slot index for a tile (UINT32_MAX if not found)
    uint32_t getTileSlotIndex(TileId id) const {
        auto it = tileToSlot.find(id.pack());
        if (it != tileToSlot.end()) {
            return static_cast<uint32_t>(it->second);
        }
        return UINT32_MAX;
    }

    // Get statistics
    uint32_t getSlotCount() const { return static_cast<uint32_t>(slots.size()); }
    uint32_t getUsedSlotCount() const;


private:
    bool initInternal(const InitInfo& info);

    // Find LRU slot for eviction, skipping slots that in-flight frames may
    // still sample. Returns slots.size() if no slot is evictable.
    size_t findLRUSlot(uint32_t currentFrame) const;

    // Bytes needed to stage one tile in the cache's format
    VkDeviceSize getTileStagingSize() const;

    // Create the cache texture
    bool createCacheTexture(VkDevice device, VmaAllocator allocator,
                            VkCommandPool commandPool, VkQueue queue);

    // Create sampler for the cache
    bool createSampler(VkDevice device);

    // Stored for cleanup
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VirtualTextureConfig config;
    bool useCompression_ = false;  // BC1 compressed cache
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Physical cache texture
    VmaImageHandle cacheImage_{};
    std::optional<vk::raii::Sampler> cacheSampler_;

    // Per-frame staging buffers to avoid race conditions with in-flight frames.
    // Each buffer holds maxUploadsPerFrame_ tile regions; stagingCursor_ tracks
    // the next free region within the current frame's buffer, because copy
    // commands execute long after recording and regions must not be reused.
    std::vector<VmaBuffer> stagingBuffers_;
    std::vector<void*> stagingMapped_;
    VkDeviceSize stagingCursor_ = 0;
    uint32_t maxUploadsPerFrame_ = 16;
    uint32_t framesInFlight_ = 3;

    // Cache slot management
    std::vector<CacheSlot> slots;
    std::unordered_map<uint32_t, size_t> tileToSlot;  // TileId::pack() -> slot index
};

} // namespace VirtualTexture
