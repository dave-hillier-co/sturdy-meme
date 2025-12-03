#pragma once

#include "StreamingManager.h"
#include "TerrainTile.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

// Configuration for terrain streaming
struct TerrainStreamingConfig {
    TerrainTileConfig tileConfig;                 // Per-tile configuration
    float loadRadius = 1024.0f;                   // Distance to start loading tiles
    float unloadRadius = 1280.0f;                 // Distance to unload tiles (with hysteresis)
    uint32_t maxLoadedTiles = 64;                 // Maximum number of loaded tiles
    StreamingBudget budget = {
        .maxGPUMemory = 128 * 1024 * 1024,        // 128 MB for terrain
        .targetGPUMemory = 100 * 1024 * 1024,
        .maxConcurrentLoads = 2,
        .maxLoadRequestsPerFrame = 2,
        .maxUnloadsPerFrame = 2
    };
};

// Manages streaming of terrain tiles based on camera position
class TerrainStreamingManager : public StreamingManager {
public:
    TerrainStreamingManager() = default;
    ~TerrainStreamingManager() override;

    // Initialize with terrain-specific configuration
    bool init(const StreamingManager::InitInfo& baseInfo,
              const TerrainStreamingConfig& terrainConfig);

    // Shutdown and cleanup
    void shutdown() override;

    // Update streaming state based on camera position
    void update(const glm::vec3& cameraPos, uint64_t frameNumber) override;

    // Get tiles that are loaded and visible
    const std::vector<TerrainTile*>& getVisibleTiles() const { return visibleTiles; }

    // Get all loaded tiles (for rendering)
    std::vector<TerrainTile*> getLoadedTiles() const;

    // Get height at world position (queries appropriate tile)
    float getHeightAt(float worldX, float worldZ) const;

    // Check if a tile exists at the given world position
    bool hasTileAt(float worldX, float worldZ) const;

    // Get tile at world position (may be null if not loaded)
    TerrainTile* getTileAt(float worldX, float worldZ) const;

    // Statistics
    uint32_t getLoadedTileCount() const;
    uint32_t getLoadingTileCount() const;

    // Configuration access
    const TerrainStreamingConfig& getConfig() const { return config; }

protected:
    // Process completed loads (called from main thread)
    uint32_t processCompletedLoads() override;

private:
    // Convert world position to tile coordinate
    TerrainTile::Coord worldToTileCoord(float worldX, float worldZ) const;

    // Get or create tile at coordinate
    TerrainTile* getOrCreateTile(const TerrainTile::Coord& coord);

    // Request tile to be loaded
    void requestTileLoad(TerrainTile* tile, float distance, uint64_t frameNumber);

    // Determine which tiles should be loaded based on camera
    void updateTileRequests(const glm::vec3& cameraPos, uint64_t frameNumber);

    // Unload tiles that are too far or over budget
    void evictTiles(const glm::vec3& cameraPos, uint64_t frameNumber);

    // Update visible tiles list
    void updateVisibleTiles(const glm::vec3& cameraPos);

    // Configuration
    TerrainStreamingConfig config;

    // All tiles (pooled, keyed by coordinate)
    std::unordered_map<TerrainTile::Coord, std::unique_ptr<TerrainTile>, TileCoordHash> tiles;

    // Currently visible tiles (updated each frame)
    std::vector<TerrainTile*> visibleTiles;

    // Tiles pending GPU resource creation (loaded on background thread, need GPU upload)
    std::vector<TerrainTile*> pendingGPUUpload;
    std::mutex pendingUploadMutex;

    // Track tiles currently being loaded (to avoid duplicate requests)
    std::unordered_set<TerrainTile::Coord, TileCoordHash> loadingTiles;
    std::mutex loadingTilesMutex;

    // Last camera position (for incremental updates)
    glm::vec3 lastCameraPos = glm::vec3(0.0f);
    bool hasLastCameraPos = false;
};
