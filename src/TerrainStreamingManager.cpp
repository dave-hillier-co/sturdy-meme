#include "TerrainStreamingManager.h"
#include <algorithm>
#include <cmath>

TerrainStreamingManager::~TerrainStreamingManager() {
    shutdown();
}

bool TerrainStreamingManager::init(const StreamingManager::InitInfo& baseInfo,
                                    const TerrainStreamingConfig& terrainConfig) {
    config = terrainConfig;

    // Override budget with terrain-specific settings
    StreamingManager::InitInfo info = baseInfo;
    info.budget = config.budget;

    if (!StreamingManager::init(info)) {
        return false;
    }

    return true;
}

void TerrainStreamingManager::shutdown() {
    // First shutdown the base (stops worker threads)
    StreamingManager::shutdown();

    // Clear pending uploads
    {
        std::lock_guard<std::mutex> lock(pendingUploadMutex);
        pendingGPUUpload.clear();
    }

    // Destroy all tile GPU resources
    for (auto& [coord, tile] : tiles) {
        if (tile->getLoadState() == TileLoadState::Loaded) {
            tile->destroyGPUResources(device, allocator);
            removeGPUMemory(tile->getGPUMemoryUsage());
        }
    }

    tiles.clear();
    visibleTiles.clear();

    {
        std::lock_guard<std::mutex> lock(loadingTilesMutex);
        loadingTiles.clear();
    }
}

void TerrainStreamingManager::update(const glm::vec3& cameraPos, uint64_t frameNumber) {
    // Process any completed background loads
    processCompletedLoads();

    // Determine which tiles should be loaded
    updateTileRequests(cameraPos, frameNumber);

    // Evict tiles that are too far or we're over budget
    evictTiles(cameraPos, frameNumber);

    // Update the list of visible tiles
    updateVisibleTiles(cameraPos);

    lastCameraPos = cameraPos;
    hasLastCameraPos = true;
}

uint32_t TerrainStreamingManager::processCompletedLoads() {
    std::vector<TerrainTile*> tilesToUpload;

    {
        std::lock_guard<std::mutex> lock(pendingUploadMutex);
        tilesToUpload.swap(pendingGPUUpload);
    }

    uint32_t processed = 0;

    for (TerrainTile* tile : tilesToUpload) {
        if (tile->getLoadState() != TileLoadState::Loading) {
            continue;  // State changed, skip
        }

        // Create GPU resources on main thread
        if (tile->createGPUResources(device, allocator, graphicsQueue, commandPool)) {
            tile->setLoadState(TileLoadState::Loaded);
            addGPUMemory(tile->getGPUMemoryUsage());
            processed++;
        } else {
            // Failed to create GPU resources
            tile->setLoadState(TileLoadState::Unloaded);
        }

        // Remove from loading set
        {
            std::lock_guard<std::mutex> loadLock(loadingTilesMutex);
            loadingTiles.erase(tile->getCoord());
        }
    }

    return processed;
}

TerrainTile::Coord TerrainStreamingManager::worldToTileCoord(float worldX, float worldZ) const {
    float tileSize = config.tileConfig.tileSize;
    return {
        static_cast<int32_t>(std::floor(worldX / tileSize)),
        static_cast<int32_t>(std::floor(worldZ / tileSize))
    };
}

TerrainTile* TerrainStreamingManager::getOrCreateTile(const TerrainTile::Coord& coord) {
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second.get();
    }

    // Create new tile
    auto tile = std::make_unique<TerrainTile>();
    tile->init(coord, config.tileConfig);
    TerrainTile* tilePtr = tile.get();
    tiles[coord] = std::move(tile);

    return tilePtr;
}

void TerrainStreamingManager::requestTileLoad(TerrainTile* tile, float distance, uint64_t frameNumber) {
    // Check if already loading or loaded
    TileLoadState state = tile->getLoadState();
    if (state == TileLoadState::Loading || state == TileLoadState::Loaded) {
        return;
    }

    // Check if already in loading set
    {
        std::lock_guard<std::mutex> lock(loadingTilesMutex);
        if (loadingTiles.count(tile->getCoord()) > 0) {
            return;
        }
        loadingTiles.insert(tile->getCoord());
    }

    tile->setLoadState(TileLoadState::Loading);

    // Submit background work
    LoadPriority priority{distance, 1.0f, frameNumber};

    submitWork([this, tile]() {
        // Load height data on background thread
        if (tile->loadHeightData()) {
            // Queue for GPU upload on main thread
            std::lock_guard<std::mutex> lock(pendingUploadMutex);
            pendingGPUUpload.push_back(tile);
        } else {
            tile->setLoadState(TileLoadState::Unloaded);

            std::lock_guard<std::mutex> loadLock(loadingTilesMutex);
            loadingTiles.erase(tile->getCoord());
        }
    }, priority);
}

void TerrainStreamingManager::updateTileRequests(const glm::vec3& cameraPos, uint64_t frameNumber) {
    // Determine tile range to check based on load radius
    float loadRadius = config.loadRadius;
    float tileSize = config.tileConfig.tileSize;

    TerrainTile::Coord camCoord = worldToTileCoord(cameraPos.x, cameraPos.z);

    int radiusTiles = static_cast<int>(std::ceil(loadRadius / tileSize)) + 1;

    // Collect tiles that need loading with their distances
    struct TileRequest {
        TerrainTile::Coord coord;
        float distance;
    };
    std::vector<TileRequest> requests;

    for (int dz = -radiusTiles; dz <= radiusTiles; dz++) {
        for (int dx = -radiusTiles; dx <= radiusTiles; dx++) {
            TerrainTile::Coord coord{camCoord.x + dx, camCoord.z + dz};

            // Calculate distance to tile center
            float tileCenterX = (coord.x + 0.5f) * tileSize;
            float tileCenterZ = (coord.z + 0.5f) * tileSize;
            float distance = std::sqrt(
                (tileCenterX - cameraPos.x) * (tileCenterX - cameraPos.x) +
                (tileCenterZ - cameraPos.z) * (tileCenterZ - cameraPos.z)
            );

            if (distance <= loadRadius) {
                requests.push_back({coord, distance});
            }
        }
    }

    // Sort by distance
    std::sort(requests.begin(), requests.end(),
              [](const TileRequest& a, const TileRequest& b) {
                  return a.distance < b.distance;
              });

    // Request loading for closest tiles (respecting per-frame limit)
    uint32_t loadRequests = 0;
    for (const auto& req : requests) {
        if (loadRequests >= config.budget.maxLoadRequestsPerFrame) {
            break;
        }

        // Check if we're approaching budget limit
        if (currentGPUMemory.load() > config.budget.targetGPUMemory) {
            break;
        }

        TerrainTile* tile = getOrCreateTile(req.coord);
        if (tile->getLoadState() == TileLoadState::Unloaded) {
            requestTileLoad(tile, req.distance, frameNumber);
            loadRequests++;
        }
    }
}

void TerrainStreamingManager::evictTiles(const glm::vec3& cameraPos, uint64_t frameNumber) {
    // Collect tiles for potential eviction
    struct EvictionCandidate {
        TerrainTile* tile;
        float distance;
        uint64_t lastAccess;
    };
    std::vector<EvictionCandidate> candidates;

    for (auto& [coord, tile] : tiles) {
        if (tile->getLoadState() != TileLoadState::Loaded) {
            continue;
        }

        float distance = tile->getDistanceToCamera(cameraPos);

        // Always evict if beyond unload radius
        if (distance > config.unloadRadius) {
            candidates.push_back({tile.get(), distance, tile->getLastAccessFrame()});
        }
        // Also consider for eviction if over budget
        else if (currentGPUMemory.load() > config.budget.maxGPUMemory) {
            candidates.push_back({tile.get(), distance, tile->getLastAccessFrame()});
        }
    }

    if (candidates.empty()) {
        return;
    }

    // Sort by eviction priority (far tiles first, then LRU)
    std::sort(candidates.begin(), candidates.end(),
              [this](const EvictionCandidate& a, const EvictionCandidate& b) {
                  // First priority: outside unload radius
                  bool aOutside = a.distance > config.unloadRadius;
                  bool bOutside = b.distance > config.unloadRadius;
                  if (aOutside != bOutside) {
                      return aOutside;  // Outside tiles first
                  }

                  // Second priority: furthest from camera
                  if (a.distance != b.distance) {
                      return a.distance > b.distance;
                  }

                  // Third priority: least recently accessed
                  return a.lastAccess < b.lastAccess;
              });

    // Evict tiles (respecting per-frame limit)
    uint32_t evicted = 0;
    for (const auto& candidate : candidates) {
        if (evicted >= config.budget.maxUnloadsPerFrame) {
            break;
        }

        // Skip if we're under budget and tile is within unload radius
        if (currentGPUMemory.load() <= config.budget.targetGPUMemory &&
            candidate.distance <= config.unloadRadius) {
            break;
        }

        TerrainTile* tile = candidate.tile;
        size_t memUsage = tile->getGPUMemoryUsage();

        tile->setLoadState(TileLoadState::Unloading);
        tile->destroyGPUResources(device, allocator);
        removeGPUMemory(memUsage);
        tile->reset();

        evicted++;
    }
}

void TerrainStreamingManager::updateVisibleTiles(const glm::vec3& cameraPos) {
    visibleTiles.clear();

    for (auto& [coord, tile] : tiles) {
        if (tile->getLoadState() == TileLoadState::Loaded) {
            tile->markAccessed(0);  // Will be updated with actual frame number
            visibleTiles.push_back(tile.get());
        }
    }

    // Sort by distance for rendering (optional, helps with overdraw)
    std::sort(visibleTiles.begin(), visibleTiles.end(),
              [&cameraPos](const TerrainTile* a, const TerrainTile* b) {
                  return a->getDistanceToCamera(cameraPos) < b->getDistanceToCamera(cameraPos);
              });
}

std::vector<TerrainTile*> TerrainStreamingManager::getLoadedTiles() const {
    std::vector<TerrainTile*> result;
    for (const auto& [coord, tile] : tiles) {
        if (tile->getLoadState() == TileLoadState::Loaded) {
            result.push_back(tile.get());
        }
    }
    return result;
}

float TerrainStreamingManager::getHeightAt(float worldX, float worldZ) const {
    TerrainTile* tile = getTileAt(worldX, worldZ);
    if (!tile || tile->getLoadState() != TileLoadState::Loaded) {
        return 0.0f;
    }

    // Convert to local coordinates
    glm::vec2 worldMin = tile->getWorldMin();
    float localX = worldX - worldMin.x;
    float localZ = worldZ - worldMin.y;

    return tile->getHeightAt(localX, localZ);
}

bool TerrainStreamingManager::hasTileAt(float worldX, float worldZ) const {
    TerrainTile::Coord coord = worldToTileCoord(worldX, worldZ);
    auto it = tiles.find(coord);
    return it != tiles.end() && it->second->getLoadState() == TileLoadState::Loaded;
}

TerrainTile* TerrainStreamingManager::getTileAt(float worldX, float worldZ) const {
    TerrainTile::Coord coord = worldToTileCoord(worldX, worldZ);
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second.get();
    }
    return nullptr;
}

uint32_t TerrainStreamingManager::getLoadedTileCount() const {
    uint32_t count = 0;
    for (const auto& [coord, tile] : tiles) {
        if (tile->getLoadState() == TileLoadState::Loaded) {
            count++;
        }
    }
    return count;
}

uint32_t TerrainStreamingManager::getLoadingTileCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(loadingTilesMutex));
    return static_cast<uint32_t>(loadingTiles.size());
}
