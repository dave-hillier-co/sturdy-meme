#pragma once

#include "LoadJobQueue.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <functional>

/**
 * LoadJobFactory - Factory for creating common load jobs
 *
 * Provides convenience functions for creating async load jobs
 * for textures, meshes, heightmaps, and custom data.
 */

namespace Loading {

/**
 * Factory for creating common load jobs
 */
class LoadJobFactory {
public:
    /**
     * Create a texture load job (PNG/JPG via stb_image)
     * @param id Unique identifier
     * @param path Full path to texture file
     * @param srgb Whether to treat as sRGB
     * @param priority Lower = higher priority
     */
    static LoadJob createTextureJob(const std::string& id, const std::string& path,
                                    bool srgb = true, int priority = 0);

    /**
     * Create a heightmap load job (16-bit or 8-bit PNG)
     * @param id Unique identifier
     * @param path Full path to heightmap file
     * @param priority Lower = higher priority
     */
    static LoadJob createHeightmapJob(const std::string& id, const std::string& path,
                                      int priority = 0);

    /**
     * Create a raw file load job
     * @param id Unique identifier
     * @param path Full path to file
     * @param phase Phase name for progress display
     * @param priority Lower = higher priority
     */
    static LoadJob createFileJob(const std::string& id, const std::string& path,
                                 const std::string& phase = "Data", int priority = 0);

    /**
     * Create a custom CPU job (e.g., procedural generation)
     * @param id Unique identifier
     * @param phase Phase name for progress display
     * @param execute Function that produces the staged resource
     * @param priority Lower = higher priority
     */
    static LoadJob createCustomJob(const std::string& id, const std::string& phase,
                                   std::function<std::unique_ptr<StagedResource>()> execute,
                                   int priority = 0);
};

/**
 * GPU upload context for staged resources
 */
struct GPUUploadContext {
    VmaAllocator allocator;
    VkDevice device;
    VkCommandPool commandPool;
    VkQueue queue;
    VkPhysicalDevice physicalDevice;
};

/**
 * Result of a GPU texture upload
 */
struct UploadedTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

/**
 * StagedResourceUploader - Uploads staged resources to GPU
 *
 * Call these from the main thread after async loading completes.
 */
class StagedResourceUploader {
public:
    explicit StagedResourceUploader(const GPUUploadContext& ctx) : ctx_(ctx) {}

    /**
     * Upload a staged texture to GPU
     * Creates VkImage, VkImageView, handles staging buffer and transitions
     */
    UploadedTexture uploadTexture(const StagedTexture& staged);

    /**
     * Upload a staged buffer to GPU (returns VkBuffer)
     */
    VkBuffer uploadBuffer(const StagedBuffer& staged, VkBufferUsageFlags usage);

private:
    GPUUploadContext ctx_;
};

} // namespace Loading
