#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <limits>
#include <cstdint>
#include <memory>
#include "VulkanRAII.h"

// Hole definition - geometric primitive for terrain holes
struct TerrainHole {
    enum class Type { Circle };
    Type type = Type::Circle;
    float centerX = 0.0f;
    float centerZ = 0.0f;
    float radius = 0.0f;
};

// Height map for terrain - handles generation, GPU texture, and CPU queries
// Also handles hole mask for caves/wells (areas with no collision/rendering)
class TerrainHeightMap {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
        uint32_t resolution;
        float terrainSize;
        float heightScale;
        std::string heightmapPath;      // Optional: path to 16-bit PNG heightmap (empty = procedural)
        float minAltitude = 0.0f;       // Altitude for height value 0 (when loading from file)
        float maxAltitude = 200.0f;     // Altitude for height value 65535 (when loading from file)
    };

    // Special return value indicating a hole in terrain (no ground)
    static constexpr float NO_GROUND = -std::numeric_limits<float>::infinity();

    /**
     * Factory: Create and initialize TerrainHeightMap.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TerrainHeightMap> create(const InitInfo& info);

    ~TerrainHeightMap();

    // Non-copyable, non-movable
    TerrainHeightMap(const TerrainHeightMap&) = delete;
    TerrainHeightMap& operator=(const TerrainHeightMap&) = delete;
    TerrainHeightMap(TerrainHeightMap&&) = delete;
    TerrainHeightMap& operator=(TerrainHeightMap&&) = delete;

    // GPU resource accessors
    VkImageView getView() const { return imageView; }
    VkSampler getSampler() const { return sampler.get(); }

    // Hole mask GPU resource accessors
    VkImageView getHoleMaskView() const { return holeMaskImageView; }
    VkSampler getHoleMaskSampler() const { return holeMaskSampler.get(); }

    // CPU-side height query (for physics/collision)
    // Returns NO_GROUND if position is inside a hole
    float getHeightAt(float x, float z) const;

    // Hole definitions - geometric primitives, rasterized per-tile on demand
    void addHoleCircle(float centerX, float centerZ, float radius);
    void removeHoleCircle(float centerX, float centerZ, float radius);
    const std::vector<TerrainHole>& getHoles() const { return holes_; }

    // Query if a point is inside any hole (analytical, not rasterized)
    bool isHole(float x, float z) const;

    // Rasterize holes into a tile mask at specified resolution
    // Returns mask where 255 = hole, 0 = solid
    std::vector<uint8_t> rasterizeHolesForTile(float tileMinX, float tileMinZ,
                                                float tileMaxX, float tileMaxZ,
                                                uint32_t resolution) const;

    // Legacy: coarse global hole mask for GPU (kept for compatibility)
    void uploadHoleMaskToGPU();

    // Raw data accessors
    const float* getData() const { return cpuData.data(); }
    uint32_t getResolution() const { return resolution; }
    float getHeightScale() const { return heightScale; }
    float getTerrainSize() const { return terrainSize; }

private:
    TerrainHeightMap() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool generateHeightData();
    bool loadHeightDataFromFile(const std::string& path, float minAlt, float maxAlt);
    bool createGPUResources();
    bool createHoleMaskResources();
    bool uploadToGPU();
    bool uploadHoleMaskToGPUInternal();
    void rasterizeHolesToGlobalMask();

    // Helper to convert world coords to texel coords
    void worldToTexel(float x, float z, int& texelX, int& texelY) const;
    void worldToHoleMaskTexel(float x, float z, int& texelX, int& texelY) const;

    // Init params (stored for queries)
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    float terrainSize = 500.0f;
    float heightScale = 0.0f;
    uint32_t resolution = 512;
    uint32_t holeMaskResolution = 2048;  // Global coarse mask for GPU (~8m/texel)

    // GPU resources for height map
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    ManagedSampler sampler;

    // GPU resources for hole mask (R8_UNORM: 0=solid, 255=hole)
    VkImage holeMaskImage = VK_NULL_HANDLE;
    VmaAllocation holeMaskAllocation = VK_NULL_HANDLE;
    VkImageView holeMaskImageView = VK_NULL_HANDLE;
    ManagedSampler holeMaskSampler;

    // CPU-side data
    std::vector<float> cpuData;
    std::vector<uint8_t> holeMaskCpuData;  // Global coarse mask: 0 = solid, 255 = hole
    bool holeMaskDirty = false;

    // Hole definitions - geometric primitives
    std::vector<TerrainHole> holes_;
};
