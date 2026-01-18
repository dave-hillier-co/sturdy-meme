#include "GrassTileTracker.h"

GrassTileTracker::UpdateResult GrassTileTracker::update(const glm::vec3& cameraPos,
                                                         uint64_t currentFrame,
                                                         uint32_t framesInFlight) {
    UpdateResult result;
    glm::vec2 cameraXZ(cameraPos.x, cameraPos.z);

    // Build desired tile set across all LOD levels
    std::unordered_set<TileCoord, TileCoordHash> desiredTiles;

    for (uint32_t lod = 0; lod < GrassConstants::NUM_LOD_LEVELS; ++lod) {
        auto tilesForLod = getDesiredTilesForLod(cameraXZ, lod);
        for (const auto& coord : tilesForLod) {
            // For LOD 1 and 2: Skip tiles covered by higher LOD
            if (lod > 0) {
                float tileSize = GrassConstants::getTileSizeForLod(lod);
                glm::vec2 tileCenter(
                    static_cast<float>(coord.x) * tileSize + tileSize * 0.5f,
                    static_cast<float>(coord.z) * tileSize + tileSize * 0.5f
                );
                if (isCoveredByHigherLod(tileCenter, lod, cameraXZ)) {
                    continue;
                }
            }
            desiredTiles.insert(coord);
        }
    }

    // Determine load requests (desired but not loaded)
    for (const auto& coord : desiredTiles) {
        if (loadedTiles_.find(coord) == loadedTiles_.end()) {
            TileRequest req;
            req.coord = coord;
            req.load = true;
            req.priority = calculateTilePriority(coord, cameraXZ);
            result.loadRequests.push_back(req);
        }
    }

    // Sort load requests by priority (highest first)
    std::sort(result.loadRequests.begin(), result.loadRequests.end(),
        [](const TileRequest& a, const TileRequest& b) {
            return a.priority > b.priority;
        });

    // Determine unload requests (loaded but not desired and safe to unload)
    for (const auto& [coord, info] : loadedTiles_) {
        if (desiredTiles.find(coord) == desiredTiles.end()) {
            // Check distance-based unloading with hysteresis
            float unloadRadius = getUnloadRadiusForLod(coord.lod);
            float unloadRadiusSq = unloadRadius * unloadRadius;

            float tileSize = GrassConstants::getTileSizeForLod(coord.lod);
            glm::vec2 tileCenter(
                static_cast<float>(coord.x) * tileSize + tileSize * 0.5f,
                static_cast<float>(coord.z) * tileSize + tileSize * 0.5f
            );
            float distSq = glm::dot(tileCenter - cameraXZ, tileCenter - cameraXZ);

            // Only unload if beyond unload radius AND safe (not in use by GPU)
            if (distSq > unloadRadiusSq && canUnloadTile(coord, currentFrame, framesInFlight)) {
                TileRequest req;
                req.coord = coord;
                req.load = false;
                req.priority = 0.0f;  // Unload priority not used
                result.unloadRequests.push_back(req);
            }
        }
    }

    // Update active tile set
    activeTileSet_ = desiredTiles;

    // Update tracking for tiles that remain active
    for (const auto& coord : desiredTiles) {
        if (loadedTiles_.find(coord) != loadedTiles_.end()) {
            loadedTiles_[coord].lastUsedFrame = currentFrame;
        }
    }

    // Update camera tile (LOD 0)
    currentCameraTile_ = worldToTileCoord(cameraXZ, 0);

    // Build sorted active tile list for result
    result.activeTiles.reserve(desiredTiles.size());
    for (const auto& coord : desiredTiles) {
        // Only include tiles that are actually loaded
        if (loadedTiles_.find(coord) != loadedTiles_.end()) {
            result.activeTiles.push_back(coord);
        }
    }

    // Sort by LOD (lower = higher detail = render first), then by distance
    std::sort(result.activeTiles.begin(), result.activeTiles.end(),
        [&cameraXZ](const TileCoord& a, const TileCoord& b) {
            if (a.lod != b.lod) {
                return a.lod < b.lod;
            }
            // Within same LOD, sort by distance
            float tileSize_a = GrassConstants::getTileSizeForLod(a.lod);
            float tileSize_b = GrassConstants::getTileSizeForLod(b.lod);
            glm::vec2 center_a(static_cast<float>(a.x) * tileSize_a + tileSize_a * 0.5f,
                               static_cast<float>(a.z) * tileSize_a + tileSize_a * 0.5f);
            glm::vec2 center_b(static_cast<float>(b.x) * tileSize_b + tileSize_b * 0.5f,
                               static_cast<float>(b.z) * tileSize_b + tileSize_b * 0.5f);
            return glm::dot(center_a - cameraXZ, center_a - cameraXZ) <
                   glm::dot(center_b - cameraXZ, center_b - cameraXZ);
        });

    return result;
}

std::vector<GrassTileTracker::TileCoord> GrassTileTracker::getActiveTilesAtLod(uint32_t lod) const {
    std::vector<TileCoord> tiles;
    for (const auto& coord : activeTileSet_) {
        if (coord.lod == lod) {
            tiles.push_back(coord);
        }
    }
    return tiles;
}

bool GrassTileTracker::isCoveredByHigherLod(const glm::vec2& worldPos, uint32_t currentLod,
                                             const glm::vec2& cameraXZ) const {
    for (uint32_t higherLod = 0; higherLod < currentLod; ++higherLod) {
        float tileSize = GrassConstants::getTileSizeForLod(higherLod);
        uint32_t tilesPerAxis = (higherLod == 0) ? GrassConstants::TILES_PER_AXIS_LOD0 :
                                (higherLod == 1) ? GrassConstants::TILES_PER_AXIS_LOD1 :
                                                   GrassConstants::TILES_PER_AXIS_LOD2;
        int halfExtent = static_cast<int>(tilesPerAxis) / 2;

        TileCoord cameraTile = worldToTileCoord(cameraXZ, higherLod);
        float minX = static_cast<float>(cameraTile.x - halfExtent) * tileSize;
        float maxX = static_cast<float>(cameraTile.x + halfExtent + 1) * tileSize;
        float minZ = static_cast<float>(cameraTile.z - halfExtent) * tileSize;
        float maxZ = static_cast<float>(cameraTile.z + halfExtent + 1) * tileSize;

        if (worldPos.x >= minX && worldPos.x < maxX &&
            worldPos.y >= minZ && worldPos.y < maxZ) {
            return true;
        }
    }
    return false;
}

std::vector<GrassTileTracker::TileCoord> GrassTileTracker::getDesiredTilesForLod(
    const glm::vec2& cameraXZ, uint32_t lod) const {

    std::vector<TileCoord> tiles;

    uint32_t tilesPerAxis = (lod == 0) ? GrassConstants::TILES_PER_AXIS_LOD0 :
                            (lod == 1) ? GrassConstants::TILES_PER_AXIS_LOD1 :
                                         GrassConstants::TILES_PER_AXIS_LOD2;
    int halfExtent = static_cast<int>(tilesPerAxis) / 2;

    TileCoord cameraTile = worldToTileCoord(cameraXZ, lod);

    for (int dz = -halfExtent; dz <= halfExtent; ++dz) {
        for (int dx = -halfExtent; dx <= halfExtent; ++dx) {
            tiles.push_back({cameraTile.x + dx, cameraTile.z + dz, lod});
        }
    }

    return tiles;
}

float GrassTileTracker::getUnloadRadiusForLod(uint32_t lod) const {
    float tileSize = GrassConstants::getTileSizeForLod(lod);
    uint32_t tilesPerAxis = (lod == 0) ? GrassConstants::TILES_PER_AXIS_LOD0 :
                            (lod == 1) ? GrassConstants::TILES_PER_AXIS_LOD1 :
                                         GrassConstants::TILES_PER_AXIS_LOD2;
    float halfExtent = static_cast<float>(tilesPerAxis) / 2.0f;
    float activeRadius = (halfExtent + 0.5f) * tileSize;
    return activeRadius + GrassConstants::TILE_UNLOAD_MARGIN;
}
