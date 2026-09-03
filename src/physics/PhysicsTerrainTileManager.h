#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include "PhysicsSystem.h"

class TerrainTileCache;

struct PhysicsTileEntry {
    int32_t tileX;
    int32_t tileZ;
    uint32_t lod;
    PhysicsBodyID bodyID;
    float worldMinX;
    float worldMinZ;
    float worldMaxX;
    float worldMaxZ;
};

class PhysicsTerrainTileManager {
public:
    struct Config {
        float loadRadius = 1000.0f;
        float unloadRadius = 1200.0f;
        uint32_t maxTilesPerFrame = 2;
        float terrainSize = 16384.0f;
        float heightScale = 0.0f;
    };

    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    PhysicsTerrainTileManager(ConstructToken, PhysicsWorld& physics, TerrainTileCache& tileCache,
                              const Config& config);

    /**
     * Factory: binds the physics world and terrain tile cache (both must outlive
     * the manager). Returns nullptr on failure.
     */
    static std::unique_ptr<PhysicsTerrainTileManager> create(PhysicsWorld& physics,
                                                             TerrainTileCache& tileCache,
                                                             const Config& config);

    // Removes every loaded physics tile body from the physics world.
    ~PhysicsTerrainTileManager();

    // Non-copyable, non-movable (holds references; stored via unique_ptr)
    PhysicsTerrainTileManager(const PhysicsTerrainTileManager&) = delete;
    PhysicsTerrainTileManager& operator=(const PhysicsTerrainTileManager&) = delete;
    PhysicsTerrainTileManager(PhysicsTerrainTileManager&&) = delete;
    PhysicsTerrainTileManager& operator=(PhysicsTerrainTileManager&&) = delete;

    void update(const glm::vec3& playerPosition);

    uint32_t getLoadedTileCount() const { return static_cast<uint32_t>(loadedTiles_.size()); }
    const Config& getConfig() const { return config_; }

    // Get all loaded physics tiles (for debug visualization)
    const std::unordered_map<uint64_t, PhysicsTileEntry>& getLoadedTiles() const { return loadedTiles_; }

private:
    uint64_t makeTileKey(int32_t tileX, int32_t tileZ, uint32_t lod) const;
    bool loadPhysicsTile(int32_t tileX, int32_t tileZ, uint32_t lod);
    void unloadPhysicsTile(uint64_t tileKey);

    struct TileRequest {
        int32_t tileX;
        int32_t tileZ;
        uint32_t lod;
    };
    std::vector<TileRequest> calculateRequiredTiles(const glm::vec3& position) const;

    PhysicsWorld& physics_;
    TerrainTileCache& tileCache_;
    Config config_;

    std::unordered_map<uint64_t, PhysicsTileEntry> loadedTiles_;
};
