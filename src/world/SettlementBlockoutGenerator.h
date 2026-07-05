#pragma once

#include "SettlementRegistry.h"
#include "material/MaterialId.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <functional>
#include <vector>

#include "ecs/World.h"

// Places deterministic blockout buildings (scaled boxes) inside each
// settlement's radius. Placement depends only on the settlement data and the
// terrain, so two runs produce identical layouts (verified via layout hash).
class SettlementBlockoutGenerator {
public:
    struct Config {
        // Query world-space terrain height at (x, z)
        std::function<float(float, float)> getTerrainHeight;
        // Optional: pre-load terrain tiles around (x, z, radius) before height queries
        std::function<void(float, float, float)> preloadTiles;

        Mesh* buildingMesh = nullptr;          // Unit cube (half-extent 0.5)
        MaterialId materialId = 0;

        // When set, footprint-mode settlements are emitted as one merged
        // world-space mesh of extruded footprint prisms (exact plot shapes)
        // instead of per-building oriented cubes. The callee owns the mesh.
        std::function<Mesh*(std::vector<Vertex>, std::vector<uint32_t>)> createMesh;

        // Fallback (random-box) path: called once per placed building with its
        // oriented bounding box (center, half extents, yaw) to register a
        // static box collider.
        std::function<void(const glm::vec3&, const glm::vec3&, float)> addCollider;

        // Merged (footprint) path: called once per settlement with the merged
        // prism geometry to register a triangle-mesh collider that exactly
        // matches the footprints (including concave/L-shaped plots).
        std::function<void(const std::vector<Vertex>&,
                           const std::vector<uint32_t>&)> addMeshCollider;

        float seaLevel = 23.0f;                // World Y below which no building goes
        float maxCornerHeightSpread = 2.0f;    // Reject slots steeper than this (meters)

        // Exclusion zone (hand-placed content around the spawn/well at Town 1)
        glm::vec2 exclusionCenter{0.0f};
        float exclusionRadius = 0.0f;

        // Directory containing town_<id>.geojson layouts (content-space) from
        // town_generator. When a layout exists it drives building placement;
        // otherwise the settlement falls back to random blockout boxes.
        std::string townsDir;
        // Caps to respect the GPU scene buffer budget across all settlements
        int maxBuildingsPerSettlement = 500;
        float minFootprintSize = 4.0f;  // Skip footprints smaller than this (meters)
    };

    struct Result {
        std::vector<ecs::Entity> entities;
        uint32_t layoutHash = 0;   // Deterministic hash of all placements
        size_t rejectedSlots = 0;  // Slope/sea/exclusion rejections
    };

    // Creates building entities for all settlements. The caller must register
    // the returned entities with the scene (SceneBuilder::addExternalSceneEntity).
    static Result generate(ecs::World& world,
                           const std::vector<Settlement>& settlements,
                           const Config& config);
};
