#pragma once

#include "VirtualTextureTypes.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <memory>
#include <optional>
#include "VmaBuffer.h"
#include "VmaImageHandle.h"

namespace VirtualTexture {

/**
 * VirtualTexturePageTable manages the indirection texture (page table).
 *
 * The page table maps virtual tile coordinates to physical cache locations.
 * Each mip level has its own indirection texture of appropriate size.
 * Entries are RGBA8: RG = cache position, B = unused, A = valid flag
 */
class VirtualTexturePageTable {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit VirtualTexturePageTable(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        vk::Device device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        vk::CommandPool commandPool = VK_NULL_HANDLE;
        vk::Queue queue = VK_NULL_HANDLE;
        VirtualTextureConfig config;
        uint32_t framesInFlight = 3;
    };

    /**
     * Factory: Create and initialize VirtualTexturePageTable.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<VirtualTexturePageTable> create(const InitInfo& info);

    ~VirtualTexturePageTable();

    // Non-copyable, non-movable
    VirtualTexturePageTable(const VirtualTexturePageTable&) = delete;
    VirtualTexturePageTable& operator=(const VirtualTexturePageTable&) = delete;
    VirtualTexturePageTable(VirtualTexturePageTable&&) = delete;
    VirtualTexturePageTable& operator=(VirtualTexturePageTable&&) = delete;

    // Update entry when tile is loaded into cache
    void setEntry(TileId id, uint16_t cacheX, uint16_t cacheY);

    // Invalidate entry when tile is evicted
    void clearEntry(TileId id);

    // Get the current entry for a tile
    PageTableEntry getEntry(TileId id) const;

    /**
     * Record page table upload commands into the provided command buffer.
     * Uses fence-based synchronization - caller is responsible for submitting
     * the command buffer and waiting on the appropriate frame fence.
     *
     * @param cmd Command buffer to record into (must be in recording state)
     * @param frameIndex Current frame index for staging buffer selection
     */
    void recordUpload(vk::CommandBuffer cmd, uint32_t frameIndex);

    // Check if any entries have changed
    bool isDirty() const { return dirty; }

    // Get the sampler for the page table
    vk::Sampler getSampler() const { return pageTableSampler_ ? **pageTableSampler_ : VK_NULL_HANDLE; }

    // Get the combined image view (texture array, one layer per mip level)
    vk::ImageView getCombinedImageView() const { return pageTableImage_.getView(); }


private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    // Create page table textures
    bool createPageTableTextures(vk::Device device, VmaAllocator allocator,
                                  vk::CommandPool commandPool, vk::Queue queue);

    // Create sampler
    bool createSampler(vk::Device device);

    // Get linear index into cpuData for a tile
    size_t getEntryIndex(TileId id) const;

    VirtualTextureConfig config;
    vk::Device device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Single array image: one layer per mip level, all layers sized for mip 0.
    // Mip N's entries occupy the top-left corner of layer N, matching the
    // shader's texelFetch(vtPageTable, ivec3(x, y, mip)) addressing.
    VmaImageHandle pageTableImage_;
    std::optional<vk::raii::Sampler> pageTableSampler_;

    // Per-frame staging buffers to avoid race conditions with in-flight frames
    std::vector<VmaBuffer> stagingBuffers_;
    std::vector<void*> stagingMapped_;
    uint32_t framesInFlight_ = 3;

    // CPU-side page table data (linear array, indexed per mip level)
    std::vector<PageTableEntry> cpuData;
    std::vector<size_t> mipOffsets;  // Offset into cpuData for each mip level
    std::vector<uint32_t> mipSizes;  // Number of entries per mip level

    bool dirty = false;
    std::vector<bool> mipDirty;  // Track which mip levels need upload
};

} // namespace VirtualTexture
