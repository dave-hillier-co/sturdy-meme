#include "TerrainTileCache.h"
#include "TerrainHeight.h"
#include "core/vulkan/CommandBufferUtils.h"
#include "core/vulkan/VmaBufferFactory.h"
#include "core/vulkan/SamplerFactory.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <stb_image.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>

std::unique_ptr<TerrainTileCache> TerrainTileCache::create(const InitInfo& info) {
    auto cache = std::make_unique<TerrainTileCache>(ConstructToken{});
    if (!cache->initInternal(info)) {
        return nullptr;
    }
    return cache;
}

TerrainTileCache::~TerrainTileCache() {
    cleanup();
}

bool TerrainTileCache::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: raiiDevice is null");
        return false;
    }
    raiiDevice_ = info.raiiDevice;
    cacheDirectory = info.cacheDirectory;
    device = info.device;
    allocator = info.allocator;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    terrainSize = info.terrainSize;
    heightScale = info.heightScale;
    yieldCallback_ = info.yieldCallback;

    // Load metadata from cache
    if (!loadMetadata()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to load metadata from %s",
                     cacheDirectory.c_str());
        return false;
    }

    // Create sampler for tile textures using factory
    auto sampler = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create sampler");
        return false;
    }
    sampler_ = std::move(*sampler);

    // Initialize tile array manager
    {
        TileArrayManager::InitInfo arrayInfo{};
        arrayInfo.device = device;
        arrayInfo.allocator = allocator;
        arrayInfo.graphicsQueue = graphicsQueue;
        arrayInfo.commandPool = commandPool;
        arrayInfo.storedTileResolution = storedTileResolution;
        arrayInfo.maxLayers = MAX_ACTIVE_TILES;
        if (!tileArray_.init(arrayInfo)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to init TileArrayManager");
            return false;
        }
    }

    // Initialize tile info buffer
    {
        TileInfoBuffer::InitInfo bufInfo{};
        bufInfo.allocator = allocator;
        bufInfo.maxActiveTiles = MAX_ACTIVE_TILES;
        if (!tileInfoBuffer_.init(bufInfo)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to init TileInfoBuffer");
            return false;
        }
        tileInfoBuffer_.initializeAllFrames();
    }

    SDL_Log("TerrainTileCache initialized: %s", cacheDirectory.c_str());
    SDL_Log("  Terrain size: %.0fm, Tile resolution: %u (stored: %u with overlap), LOD levels: %u",
            terrainSize, tileResolution, storedTileResolution, numLODLevels);
    SDL_Log("  LOD0 grid: %ux%u tiles", tilesX, tilesZ);

    // Load all base LOD tiles synchronously at startup
    if (!loadBaseLODTiles()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Failed to load base LOD tiles");
        return false;
    }

    // Initialize hole mask manager
    {
        HoleMaskManager::InitInfo holeInfo{};
        holeInfo.raiiDevice = raiiDevice_;
        holeInfo.device = device;
        holeInfo.allocator = allocator;
        holeInfo.graphicsQueue = graphicsQueue;
        holeInfo.commandPool = commandPool;
        holeInfo.storedTileResolution = storedTileResolution;
        holeInfo.maxLayers = MAX_ACTIVE_TILES;
        if (!holeMask_.init(holeInfo)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "TerrainTileCache: Failed to init HoleMaskManager");
            return false;
        }
    }

    return true;
}

void TerrainTileCache::cleanup() {
    // Wait for GPU to finish
    if (device) {
        vkDeviceWaitIdle(device);
    }

    // Clean up sub-components
    baseHeightMap_.cleanup();
    holeMask_.cleanup();
    tileInfoBuffer_.cleanup();
    tileArray_.cleanup();

    // Unload all tiles (GPU residency is owned by the shared tile array, freed above)
    loadedTiles.clear();
    activeTiles.clear();

    // Destroy sampler (RAII via reset)
    sampler_.reset();
}

bool TerrainTileCache::loadMetadata() {
    std::string metaPath = cacheDirectory + "/terrain_tiles.meta";
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Cannot open metadata: %s",
                     metaPath.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            std::getline(iss, value);

            if (key == "tileResolution") tileResolution = std::stoul(value);
            else if (key == "numLODLevels") numLODLevels = std::stoul(value);
            else if (key == "tilesX") tilesX = std::stoul(value);
            else if (key == "tilesZ") tilesZ = std::stoul(value);
            else if (key == "sourceWidth") sourceWidth = std::stoul(value);
            else if (key == "sourceHeight") sourceHeight = std::stoul(value);
            else if (key == "heightScale") heightScale = std::stof(value);
            else if (key == "tileOverlap") tileOverlap = std::stoul(value);
            // Legacy: compute heightScale from altitude range if present in old metadata
            else if (key == "minAltitude" || key == "maxAltitude") {
                // Ignore - heightScale should be set directly
            }
        }
    }

    // Calculate stored tile resolution (includes overlap for seamless boundaries)
    storedTileResolution = tileResolution + tileOverlap;
    SDL_Log("TerrainTileCache: Tile resolution %u, stored with +%u overlap = %u, heightScale=%.1f",
            tileResolution, tileOverlap, storedTileResolution, heightScale);

    return true;
}

std::string TerrainTileCache::getTilePath(TileCoord coord, uint32_t lod) const {
    std::ostringstream oss;
    oss << cacheDirectory << "/tile_" << coord.x << "_" << coord.z << "_lod" << lod << ".png";
    return oss.str();
}

uint64_t TerrainTileCache::makeTileKey(TileCoord coord, uint32_t lod) const {
    return (static_cast<uint64_t>(lod) << 48) |
           (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 24) |
           static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
}

void TerrainTileCache::calculateTileWorldBounds(TileCoord coord, uint32_t lod, TerrainTile& tile) const {
    uint32_t lodTilesX = tilesX >> lod;
    uint32_t lodTilesZ = tilesZ >> lod;
    if (lodTilesX < 1) lodTilesX = 1;
    if (lodTilesZ < 1) lodTilesZ = 1;

    float tileWorldSizeX = terrainSize / lodTilesX;
    float tileWorldSizeZ = terrainSize / lodTilesZ;

    tile.worldMinX = (static_cast<float>(coord.x) / lodTilesX - 0.5f) * terrainSize;
    tile.worldMinZ = (static_cast<float>(coord.z) / lodTilesZ - 0.5f) * terrainSize;
    tile.worldMaxX = tile.worldMinX + tileWorldSizeX;
    tile.worldMaxZ = tile.worldMinZ + tileWorldSizeZ;
}

bool TerrainTileCache::loadTileDataFromDisk(TileCoord coord, uint32_t lod, TerrainTile& tile) {
    std::string path = getTilePath(coord, lod);

    int width, height, channels;
    uint16_t* data = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to load tile: %s",
                    path.c_str());
        return false;
    }

    uint32_t loadedRes = static_cast<uint32_t>(width);
    bool isOldFormat = (loadedRes == tileResolution);
    bool isNewFormat = (loadedRes == storedTileResolution);

    if (!isOldFormat && !isNewFormat) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Tile %s is %dx%d, expected %ux%u or %ux%u",
                     path.c_str(), width, height, tileResolution, tileResolution,
                     storedTileResolution, storedTileResolution);
        stbi_image_free(data);
        return false;
    }

    if (width != height) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Tile %s is not square (%dx%d)",
                     path.c_str(), width, height);
        stbi_image_free(data);
        return false;
    }

    tile.coord = coord;
    tile.lod = lod;
    calculateTileWorldBounds(coord, lod, tile);

    // Convert 16-bit to normalized float32
    tile.cpuData.resize(loadedRes * loadedRes);
    for (uint32_t i = 0; i < loadedRes * loadedRes; i++) {
        tile.cpuData[i] = static_cast<float>(data[i]) / 65535.0f;
    }

    stbi_image_free(data);
    return true;
}

uint32_t TerrainTileCache::getLODForDistance(float distance) const {
    if (distance < LOD0_MAX_DISTANCE) return 0;
    if (distance < LOD1_MAX_DISTANCE && numLODLevels > 1) return 1;
    if (distance < LOD2_MAX_DISTANCE && numLODLevels > 2) return 2;
    if (distance < LOD3_MAX_DISTANCE && numLODLevels > 3) return 3;
    return numLODLevels; // Beyond all LOD levels - use global fallback
}

TileCoord TerrainTileCache::worldToTileCoord(float worldX, float worldZ, uint32_t lod) const {
    float normX = (worldX / terrainSize) + 0.5f;
    float normZ = (worldZ / terrainSize) + 0.5f;
    normX = std::clamp(normX, 0.0f, 0.9999f);
    normZ = std::clamp(normZ, 0.0f, 0.9999f);

    uint32_t lodTilesX = tilesX >> lod;
    uint32_t lodTilesZ = tilesZ >> lod;
    if (lodTilesX < 1) lodTilesX = 1;
    if (lodTilesZ < 1) lodTilesZ = 1;

    TileCoord coord;
    coord.x = static_cast<int32_t>(normX * lodTilesX);
    coord.z = static_cast<int32_t>(normZ * lodTilesZ);

    return coord;
}

void TerrainTileCache::updateActiveTiles(const glm::vec3& cameraPos, float loadRadius, float unloadRadius) {
    std::vector<std::pair<TileCoord, uint32_t>> tilesToLoad;
    std::vector<uint64_t> tilesToUnload;

    float camX = cameraPos.x;
    float camZ = cameraPos.z;

    uint32_t baseLOD = baseHeightMap_.getBaseLOD();

    // For each LOD level, determine which tiles should be loaded
    for (uint32_t lod = 0; lod < numLODLevels; lod++) {
        float lodMaxDist = (lod == 0) ? LOD0_MAX_DISTANCE :
                          (lod == 1) ? LOD1_MAX_DISTANCE :
                          (lod == 2) ? LOD2_MAX_DISTANCE :
                          LOD3_MAX_DISTANCE;

        uint32_t lodTilesX = tilesX >> lod;
        uint32_t lodTilesZ = tilesZ >> lod;
        if (lodTilesX < 1) lodTilesX = 1;
        if (lodTilesZ < 1) lodTilesZ = 1;

        int32_t minTileX = static_cast<int32_t>(((camX - loadRadius) / terrainSize + 0.5f) * lodTilesX);
        int32_t maxTileX = static_cast<int32_t>(((camX + loadRadius) / terrainSize + 0.5f) * lodTilesX);
        int32_t minTileZ = static_cast<int32_t>(((camZ - loadRadius) / terrainSize + 0.5f) * lodTilesZ);
        int32_t maxTileZ = static_cast<int32_t>(((camZ + loadRadius) / terrainSize + 0.5f) * lodTilesZ);

        minTileX = std::max(0, minTileX);
        maxTileX = std::min(static_cast<int32_t>(lodTilesX - 1), maxTileX);
        minTileZ = std::max(0, minTileZ);
        maxTileZ = std::min(static_cast<int32_t>(lodTilesZ - 1), maxTileZ);

        for (int32_t tz = minTileZ; tz <= maxTileZ; tz++) {
            for (int32_t tx = minTileX; tx <= maxTileX; tx++) {
                float tileCenterX = ((static_cast<float>(tx) + 0.5f) / lodTilesX - 0.5f) * terrainSize;
                float tileCenterZ = ((static_cast<float>(tz) + 0.5f) / lodTilesZ - 0.5f) * terrainSize;

                float dist = std::sqrt((tileCenterX - camX) * (tileCenterX - camX) +
                                       (tileCenterZ - camZ) * (tileCenterZ - camZ));

                if (dist < lodMaxDist && dist < loadRadius) {
                    TileCoord coord{tx, tz};
                    if (!isTileLoaded(coord, lod)) {
                        tilesToLoad.push_back({coord, lod});
                    }
                }
            }
        }
    }

    // Find tiles to unload (beyond their LOD's useful range)
    for (auto& [key, tile] : loadedTiles) {
        if (tile.lod == baseLOD) continue;

        float tileCenterX = (tile.worldMinX + tile.worldMaxX) * 0.5f;
        float tileCenterZ = (tile.worldMinZ + tile.worldMaxZ) * 0.5f;

        float dist = std::sqrt((tileCenterX - camX) * (tileCenterX - camX) +
                               (tileCenterZ - camZ) * (tileCenterZ - camZ));

        float lodMaxDist = (tile.lod == 0) ? LOD0_MAX_DISTANCE :
                          (tile.lod == 1) ? LOD1_MAX_DISTANCE :
                          (tile.lod == 2) ? LOD2_MAX_DISTANCE :
                          LOD3_MAX_DISTANCE;

        float unloadDist = lodMaxDist + (unloadRadius - loadRadius);
        if (dist > unloadDist) {
            tilesToUnload.push_back(key);
        }
    }

    // Unload distant tiles
    for (uint64_t key : tilesToUnload) {
        auto it = loadedTiles.find(key);
        if (it != loadedTiles.end()) {
            TerrainTile& tile = it->second;
            if (tile.arrayLayerIndex >= 0) {
                tileArray_.freeLayer(tile.arrayLayerIndex);
            }
            loadedTiles.erase(it);
        }
    }

    // Load new tiles (limit per frame to avoid stalls)
    constexpr uint32_t MAX_TILES_PER_FRAME = 4;
    uint32_t tilesLoadedThisFrame = 0;

    for (const auto& [coord, lod] : tilesToLoad) {
        if (tilesLoadedThisFrame >= MAX_TILES_PER_FRAME) break;
        if (loadedTiles.size() >= MAX_ACTIVE_TILES) break;

        if (loadTile(coord, lod)) {
            tilesLoadedThisFrame++;
        }
    }

    // Update active tiles list. Only Resident tiles (those with a real GPU array layer)
    // belong here: activeTiles feeds TileInfoBuffer, which writes arrayLayerIndex to the
    // shader. A CpuResident tile (no layer) must not, or it would bind layer -1.
    activeTiles.clear();
    for (auto& [key, tile] : loadedTiles) {
        if (tile.state == TileState::Resident) {
            activeTiles.push_back(&tile);
        }
    }

    // Sort active tiles by LOD (lower LOD = higher resolution) so GPU checks LOD0 first
    std::sort(activeTiles.begin(), activeTiles.end(), [](const TerrainTile* a, const TerrainTile* b) {
        return a->lod < b->lod;
    });

    // Update tile info buffer
    tileInfoBuffer_.update(currentFrameIndex_, activeTiles);
}

bool TerrainTileCache::loadTile(TileCoord coord, uint32_t lod) {
    uint64_t key = makeTileKey(coord, lod);

    auto existingIt = loadedTiles.find(key);
    if (existingIt != loadedTiles.end() && existingIt->second.state == TileState::Resident) {
        return true;
    }

    bool hasCpuData = (existingIt != loadedTiles.end() && !existingIt->second.cpuData.empty());

    TerrainTile* tilePtr = nullptr;

    if (hasCpuData) {
        tilePtr = &existingIt->second;
    } else {
        TerrainTile& tile = loadedTiles[key];
        if (!loadTileDataFromDisk(coord, lod, tile)) {
            loadedTiles.erase(key);
            return false;
        }
        tilePtr = &tile;
    }

    TerrainTile& tile = *tilePtr;

    // Allocate a layer in the shared tile array and stage the CPU height data
    // directly into it. The array texture is the only resource the terrain shader
    // samples (see TerrainDescriptorSets), so no standalone per-tile image is needed.
    int32_t layerIndex = tileArray_.allocateLayer();
    if (layerIndex < 0) {
        // No GPU layer available: the tile keeps its CPU data (height queries/physics
        // still work) but is not renderable. It stays CpuResident and will be retried
        // on a later frame once a layer frees, rather than binding layer -1 to the shader.
        // Logged at debug level because a starved tile is retried every frame.
        tile.state = TileState::CpuResident;
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: No free array layers for tile (%d, %d) LOD%u",
                     coord.x, coord.z, lod);
        return true;
    }

    tile.arrayLayerIndex = layerIndex;
    tileArray_.copyTileToLayer(tile, static_cast<uint32_t>(layerIndex));
    tile.state = TileState::Resident;

    SDL_Log("TerrainTileCache: Loaded tile (%d, %d) LOD%u layer %d - world bounds [%.0f,%.0f]-[%.0f,%.0f]%s",
            coord.x, coord.z, lod, tile.arrayLayerIndex, tile.worldMinX, tile.worldMinZ, tile.worldMaxX, tile.worldMaxZ,
            hasCpuData ? " (added GPU to existing)" : "");

    // Upload hole mask for this tile (only meaningful once it has an array layer)
    holeMask_.uploadTileHoleMask(tile, tile.arrayLayerIndex);

    return true;
}


bool TerrainTileCache::isTileLoaded(TileCoord coord, uint32_t lod) const {
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    // "Loaded" here means renderable (has a GPU layer); a CpuResident tile still needs
    // loading so the render path will allocate it a layer.
    return it != loadedTiles.end() && it->second.state == TileState::Resident;
}

bool TerrainTileCache::getHeightAt(float worldX, float worldZ, float& outHeight) const {
    auto sampleTile = [&](const TerrainTile& tile, const char* source) -> bool {
        if (tile.cpuData.empty()) return false;
        if (worldX < tile.worldMinX || worldX >= tile.worldMaxX ||
            worldZ < tile.worldMinZ || worldZ >= tile.worldMaxZ) return false;

        float u = (worldX - tile.worldMinX) / (tile.worldMaxX - tile.worldMinX);
        float v = (worldZ - tile.worldMinZ) / (tile.worldMaxZ - tile.worldMinZ);
        uint32_t actualRes = static_cast<uint32_t>(std::sqrt(tile.cpuData.size()));

        outHeight = TerrainHeight::sampleWorldHeight(u, v, tile.cpuData.data(),
                                                      actualRes, heightScale);

        static int debugCount = 0;
        if (debugCount < 5) {
            SDL_Log("getHeightAt(%.1f, %.1f): %s LOD%u tile(%d,%d) uv(%.4f,%.4f) res=%u h=%.2f",
                    worldX, worldZ, source, tile.lod, tile.coord.x, tile.coord.z,
                    u, v, actualRes, outHeight);
            debugCount++;
        }
        return true;
    };

    // First check active tiles (GPU tiles - highest priority).
    // activeTiles is already kept sorted by LOD in updateActiveTiles(), so iterate directly.
    for (const TerrainTile* tile : activeTiles) {
        if (sampleTile(*tile, "active")) return true;
    }

    // Also check all loaded tiles (includes CPU-only tiles from physics preloading)
    uint32_t baseLOD = baseHeightMap_.getBaseLOD();
    std::vector<const TerrainTile*> sortedLoaded;
    for (const auto& [key, tile] : loadedTiles) {
        if (tile.lod == baseLOD) continue;
        sortedLoaded.push_back(&tile);
    }
    std::sort(sortedLoaded.begin(), sortedLoaded.end(), [](const TerrainTile* a, const TerrainTile* b) {
        return a->lod < b->lod;
    });
    for (const TerrainTile* tile : sortedLoaded) {
        if (sampleTile(*tile, "loaded")) return true;
    }

    // Fallback to base LOD tiles
    return baseHeightMap_.sampleHeight(worldX, worldZ, outHeight);
}

TerrainTileCache::HeightQueryInfo TerrainTileCache::getHeightAtDebug(float worldX, float worldZ) const {
    HeightQueryInfo info{};
    info.found = false;
    info.height = 0.0f;
    info.tileX = 0;
    info.tileZ = 0;
    info.lod = 0;
    info.source = "none";

    auto sampleTile = [&](const TerrainTile& tile, const char* source) -> bool {
        if (tile.cpuData.empty()) return false;
        if (worldX < tile.worldMinX || worldX >= tile.worldMaxX ||
            worldZ < tile.worldMinZ || worldZ >= tile.worldMaxZ) return false;

        float u = (worldX - tile.worldMinX) / (tile.worldMaxX - tile.worldMinX);
        float v = (worldZ - tile.worldMinZ) / (tile.worldMaxZ - tile.worldMinZ);
        uint32_t actualRes = static_cast<uint32_t>(std::sqrt(tile.cpuData.size()));

        info.height = TerrainHeight::sampleWorldHeight(u, v, tile.cpuData.data(), actualRes, heightScale);
        info.tileX = tile.coord.x;
        info.tileZ = tile.coord.z;
        info.lod = tile.lod;
        info.source = source;
        info.found = true;
        return true;
    };

    // Check active tiles first (already kept sorted by LOD in updateActiveTiles()).
    for (const TerrainTile* tile : activeTiles) {
        if (sampleTile(*tile, "active")) return info;
    }

    // Check loaded tiles (sorted by LOD, excluding baseLOD)
    uint32_t baseLOD = baseHeightMap_.getBaseLOD();
    std::vector<const TerrainTile*> sortedLoaded;
    for (const auto& [key, tile] : loadedTiles) {
        if (tile.lod == baseLOD) continue;
        sortedLoaded.push_back(&tile);
    }
    std::sort(sortedLoaded.begin(), sortedLoaded.end(), [](const TerrainTile* a, const TerrainTile* b) {
        return a->lod < b->lod;
    });
    for (const TerrainTile* tile : sortedLoaded) {
        if (sampleTile(*tile, "loaded")) return info;
    }

    // Fallback to base LOD
    const TerrainTile* baseTile = baseHeightMap_.getTileAt(worldX, worldZ);
    if (baseTile && !baseTile->cpuData.empty()) {
        sampleTile(*baseTile, "baseLOD");
    }

    return info;
}

const TerrainTile* TerrainTileCache::getLoadedTile(TileCoord coord, uint32_t lod) const {
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    if (it != loadedTiles.end() && it->second.state != TileState::Unloaded) {
        return &it->second;
    }
    return nullptr;
}

bool TerrainTileCache::requestTileLoad(TileCoord coord, uint32_t lod) {
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    if (it != loadedTiles.end() && it->second.state != TileState::Unloaded) {
        return true;
    }
    return loadTile(coord, lod);
}

bool TerrainTileCache::loadTileCPUOnly(TileCoord coord, uint32_t lod) {
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    if (it != loadedTiles.end() && !it->second.cpuData.empty()) {
        return true;
    }

    TerrainTile& tile = loadedTiles[key];
    if (!loadTileDataFromDisk(coord, lod, tile)) {
        loadedTiles.erase(key);
        return false;
    }

    // CPU height data only (for physics/queries); no GPU array layer yet.
    tile.state = TileState::CpuResident;

    SDL_Log("TerrainTileCache: Loaded tile CPU data (%d, %d) LOD%u - world bounds [%.0f,%.0f]-[%.0f,%.0f]",
            coord.x, coord.z, lod, tile.worldMinX, tile.worldMinZ, tile.worldMaxX, tile.worldMaxZ);

    return true;
}

void TerrainTileCache::preloadTilesAround(float worldX, float worldZ, float radius) {
    const uint32_t lod = 0;

    int32_t minTileX = static_cast<int32_t>(((worldX - radius) / terrainSize + 0.5f) * tilesX);
    int32_t maxTileX = static_cast<int32_t>(((worldX + radius) / terrainSize + 0.5f) * tilesX);
    int32_t minTileZ = static_cast<int32_t>(((worldZ - radius) / terrainSize + 0.5f) * tilesZ);
    int32_t maxTileZ = static_cast<int32_t>(((worldZ + radius) / terrainSize + 0.5f) * tilesZ);

    minTileX = std::max(0, minTileX);
    maxTileX = std::min(static_cast<int32_t>(tilesX - 1), maxTileX);
    minTileZ = std::max(0, minTileZ);
    maxTileZ = std::min(static_cast<int32_t>(tilesZ - 1), maxTileZ);

    uint32_t tilesLoaded = 0;
    for (int32_t tz = minTileZ; tz <= maxTileZ; tz++) {
        for (int32_t tx = minTileX; tx <= maxTileX; tx++) {
            float tileCenterX = ((static_cast<float>(tx) + 0.5f) / tilesX - 0.5f) * terrainSize;
            float tileCenterZ = ((static_cast<float>(tz) + 0.5f) / tilesZ - 0.5f) * terrainSize;

            float dx = worldX - tileCenterX;
            float dz = worldZ - tileCenterZ;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < radius) {
                TileCoord coord{tx, tz};
                if (loadTileCPUOnly(coord, lod)) {
                    tilesLoaded++;
                }
            }
        }
    }

    SDL_Log("TerrainTileCache: Pre-loaded %u LOD0 tiles around (%.0f, %.0f) radius %.0f",
            tilesLoaded, worldX, worldZ, radius);
}

bool TerrainTileCache::loadBaseLODTiles() {
    BaseHeightMap::InitInfo baseInfo{};
    baseInfo.device = device;
    baseInfo.allocator = allocator;
    baseInfo.graphicsQueue = graphicsQueue;
    baseInfo.commandPool = commandPool;
    baseInfo.terrainSize = terrainSize;
    baseInfo.heightScale = heightScale;
    baseInfo.tileResolution = tileResolution;
    baseInfo.tilesX = tilesX;
    baseInfo.tilesZ = tilesZ;
    baseInfo.numLODLevels = numLODLevels;
    baseInfo.yieldCallback = yieldCallback_;
    baseHeightMap_.init(baseInfo);

    return baseHeightMap_.loadBaseLODTiles([this](int32_t tx, int32_t tz, uint32_t lod) -> TerrainTile* {
        TileCoord coord{tx, tz};
        if (loadTileCPUOnly(coord, lod)) {
            uint64_t key = makeTileKey(coord, lod);
            auto it = loadedTiles.find(key);
            if (it != loadedTiles.end()) {
                return &it->second;
            }
        }
        return nullptr;
    });
}

std::vector<const TerrainTile*> TerrainTileCache::getAllCPUTiles() const {
    std::vector<const TerrainTile*> result;
    result.reserve(loadedTiles.size());
    for (const auto& [key, tile] : loadedTiles) {
        if (!tile.cpuData.empty()) {
            result.push_back(&tile);
        }
    }
    return result;
}

// ============================================================================
// Hole mask delegation
// ============================================================================

bool TerrainTileCache::isHole(float x, float z) const {
    return holeMask_.isHole(x, z);
}

void TerrainTileCache::addHoleCircle(float centerX, float centerZ, float radius) {
    holeMask_.addHoleCircle(centerX, centerZ, radius, activeTiles);
}

void TerrainTileCache::removeHoleCircle(float centerX, float centerZ, float radius) {
    holeMask_.removeHoleCircle(centerX, centerZ, radius, activeTiles);
}

std::vector<uint8_t> TerrainTileCache::rasterizeHolesForTile(
    float tileMinX, float tileMinZ, float tileMaxX, float tileMaxZ, uint32_t resolution) const {
    return holeMask_.rasterizeHolesForTile(tileMinX, tileMinZ, tileMaxX, tileMaxZ, resolution);
}

void TerrainTileCache::uploadHoleMaskToGPU() {
    holeMask_.uploadAllActiveMasks(activeTiles);
}
