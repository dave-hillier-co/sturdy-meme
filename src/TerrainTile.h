#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <atomic>

// Loading state for a terrain tile
enum class TileLoadState {
    Unloaded,       // Not loaded, no GPU resources
    Loading,        // Currently being loaded on background thread
    Loaded,         // Fully loaded and ready for rendering
    Unloading       // Marked for unload, pending GPU idle
};

// Configuration for terrain tiles
struct TerrainTileConfig {
    uint32_t heightmapResolution = 256;  // Per-tile heightmap resolution
    float tileSize = 256.0f;              // World units per tile
    float heightScale = 50.0f;            // Maximum height
    int cbtMaxDepth = 16;                 // CBT depth per tile (less than global)
    int cbtInitDepth = 4;                 // Initial CBT subdivision
};

// Represents a single terrain tile with its own heightmap and CBT
class TerrainTile {
public:
    // Tile coordinate (grid position)
    struct Coord {
        int32_t x;
        int32_t z;

        bool operator==(const Coord& other) const {
            return x == other.x && z == other.z;
        }
    };

    TerrainTile() = default;
    ~TerrainTile() = default;

    // Initialize tile with configuration (allocates CPU data only)
    void init(const Coord& coord, const TerrainTileConfig& config);

    // Load heightmap data (can be called from background thread)
    // Returns true if data was loaded successfully
    bool loadHeightData();

    // Create GPU resources (must be called from main thread with Vulkan context)
    bool createGPUResources(VkDevice device, VmaAllocator allocator,
                            VkQueue graphicsQueue, VkCommandPool commandPool);

    // Destroy GPU resources (must be called from main thread)
    void destroyGPUResources(VkDevice device, VmaAllocator allocator);

    // Reset tile for reuse
    void reset();

    // Accessors
    const Coord& getCoord() const { return coord; }
    TileLoadState getLoadState() const { return loadState.load(); }
    void setLoadState(TileLoadState state) { loadState.store(state); }

    // World position of tile's minimum corner
    glm::vec2 getWorldMin() const { return worldMin; }
    glm::vec2 getWorldMax() const { return worldMin + glm::vec2(tileSize); }
    glm::vec2 getWorldCenter() const { return worldMin + glm::vec2(tileSize * 0.5f); }

    // GPU resources
    VkImage getHeightmapImage() const { return heightmapImage; }
    VkImageView getHeightmapView() const { return heightmapView; }
    VkSampler getHeightmapSampler() const { return heightmapSampler; }
    VkBuffer getCBTBuffer() const { return cbtBuffer; }
    uint32_t getCBTBufferSize() const { return cbtBufferSize; }

    // CPU height query (for physics)
    float getHeightAt(float localX, float localZ) const;

    // Memory size estimate (for budget tracking)
    size_t getGPUMemoryUsage() const;

    // Last access frame (for LRU eviction)
    void markAccessed(uint64_t frameNumber) { lastAccessFrame = frameNumber; }
    uint64_t getLastAccessFrame() const { return lastAccessFrame; }

    // Distance to camera (for priority sorting)
    float getDistanceToCamera(const glm::vec3& cameraPos) const;

private:
    // Calculate CBT buffer size for this tile's depth
    static uint32_t calculateCBTBufferSize(int maxDepth);

    // Initialize CBT buffer with initial subdivision
    void initializeCBT(void* mappedBuffer);

    // Tile identity
    Coord coord = {0, 0};
    TerrainTileConfig config;

    // World space bounds
    glm::vec2 worldMin = glm::vec2(0.0f);
    float tileSize = 256.0f;

    // Loading state (atomic for thread safety)
    std::atomic<TileLoadState> loadState{TileLoadState::Unloaded};

    // CPU heightmap data
    std::vector<float> cpuHeightData;

    // GPU resources
    VkImage heightmapImage = VK_NULL_HANDLE;
    VmaAllocation heightmapAllocation = VK_NULL_HANDLE;
    VkImageView heightmapView = VK_NULL_HANDLE;
    VkSampler heightmapSampler = VK_NULL_HANDLE;

    // Per-tile CBT buffer
    VkBuffer cbtBuffer = VK_NULL_HANDLE;
    VmaAllocation cbtAllocation = VK_NULL_HANDLE;
    uint32_t cbtBufferSize = 0;

    // LRU tracking
    uint64_t lastAccessFrame = 0;
};

// Hash function for tile coordinates (for use in std::unordered_map)
struct TileCoordHash {
    size_t operator()(const TerrainTile::Coord& coord) const {
        // Simple hash combining x and z
        return std::hash<int32_t>()(coord.x) ^ (std::hash<int32_t>()(coord.z) << 16);
    }
};
