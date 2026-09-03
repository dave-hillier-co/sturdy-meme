#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <string>
#include <optional>

/**
 * PipelineCache - Manages Vulkan pipeline cache with disk persistence
 *
 * Pipeline caches significantly reduce shader compilation time on subsequent
 * runs by storing driver-specific compiled pipeline data.
 *
 * Usage:
 *   auto cache = PipelineCache::create(raiiDevice, "pipeline_cache.bin");
 *   // Use cache->getCache() when creating pipelines
 *   // Destruction saves the cache to disk and releases the handle
 */
class PipelineCache {
public:
    /**
     * Create the pipeline cache, seeding it from cacheFilePath if that file exists.
     * @param raiiDevice The Vulkan RAII device (must outlive the returned object)
     * @param cacheFilePath Path to the cache file (loaded if exists, written on destruction)
     * @return the cache, or nullopt on failure
     */
    static std::optional<PipelineCache> create(const vk::raii::Device& raiiDevice,
                                               const std::string& cacheFilePath = "pipeline_cache.bin");

    // Saves the cache to disk, then the raii handle destroys itself.
    ~PipelineCache();

    // Non-copyable, movable
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&&) noexcept = default;
    PipelineCache& operator=(PipelineCache&&) noexcept = default;

    /**
     * Get the pipeline cache handle for use in pipeline creation
     */
    vk::PipelineCache getCache() const { return pipelineCache_ ? **pipelineCache_ : vk::PipelineCache{}; }

    /**
     * Save the current cache state to disk
     * Can be called periodically to avoid losing cache on crash
     * @return true on success
     */
    bool saveToFile();

private:
    PipelineCache(const vk::raii::Device& raiiDevice, std::string cacheFilePath);

    bool loadFromFile();

    const vk::raii::Device* device_ = nullptr;
    std::optional<vk::raii::PipelineCache> pipelineCache_;
    std::string cacheFilePath_;
};
