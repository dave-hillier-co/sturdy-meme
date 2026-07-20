#pragma once

#include "material/MaterialId.h"
#include "core/Mesh.h"
#include "ecs/World.h"

#include <glm/glm.hpp>
#include <functional>
#include <vector>

// Turns polylines (roads, town streets, rivers) into draped ribbon meshes:
// triangle strips following the terrain (or a fixed water level), lifted
// slightly to avoid z-fighting with the terrain underneath. Long ribbons are
// chunked into separate meshes so their AABBs stay local and culling works.
class RibbonMeshGenerator {
public:
    struct Config {
        // Query world-space terrain height at (x, z)
        std::function<float(float, float)> getTerrainHeight;
        // Optional: pre-load terrain tiles around (x, z, radius)
        std::function<void(float, float, float)> preloadTiles;
        // Upload world-space meshes; returns one Mesh* per geometry
        std::function<std::vector<Mesh*>(std::vector<MeshGeometry>)> createMeshes;

        float sampleSpacing = 4.0f;   // Resample interval along the polyline
        float lift = 0.08f;           // Height above terrain (avoids z-fighting)
        float uvScaleAlong = 0.25f;   // V repeat per meter along the ribbon
        float chunkLength = 256.0f;   // Split ribbons into chunks of this length
    };

    // A circular region the ribbon must not cover (e.g. a bridge deck spans
    // the river; the draped road would dip through the water there).
    struct SkipZone {
        glm::vec2 center;   // World XZ
        float radius = 0.0f;
    };

    struct Ribbon {
        // World-space centerline. y is used as the surface height when
        // followTerrain is false (rivers); ignored when true (roads drape).
        std::vector<glm::vec3> points;
        std::vector<float> widths;    // Per-point width; last value repeats
        MaterialId material = 0;
        glm::vec4 color{1.0f};        // Vertex color tint
        bool followTerrain = true;
        bool castsShadow = false;
    };

    struct Result {
        std::vector<ecs::Entity> entities;
        size_t chunksBuilt = 0;
    };

    explicit RibbonMeshGenerator(Config config) : config_(std::move(config)) {}

    // Build all ribbons and create one entity per chunk mesh.
    Result generate(ecs::World& world, const std::vector<Ribbon>& ribbons,
                    const std::vector<SkipZone>& skipZones = {});

private:
    Config config_;
};
