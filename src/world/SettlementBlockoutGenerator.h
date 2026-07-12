#pragma once

#include "SettlementRegistry.h"
#include "material/MaterialId.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <vector>

#include "ecs/World.h"

class KitBuildingAssembler;

// Places deterministic buildings inside each settlement: kit wall shells
// traced along town-layout footprints, with extruded-prism and blockout-box
// fallbacks. Placement depends only on the settlement data and the terrain,
// so two runs produce identical layouts (verified via layout hash).
//
// Generation is incremental: construct with a Config, then call
// generateSettlement() once per settlement — typically one per frame, so the
// startup cost amortizes instead of stalling a single frame.
class SettlementBlockoutGenerator {
public:
    struct Config {
        // Query world-space terrain height at (x, z)
        std::function<float(float, float)> getTerrainHeight;
        // Optional: pre-load terrain tiles around (x, z, radius) before height queries
        std::function<void(float, float, float)> preloadTiles;

        Mesh* buildingMesh = nullptr;          // Unit cube (half-extent 0.5)
        MaterialId materialId = 0;

        // Vertical layer materials for merged town buildings, bottom to top:
        // [0] is the below-ground foundation plinth, [1..] are floor bands
        // cycled per storey so each floor reads with a distinct texture. Falls
        // back to a single materialId mesh when empty.
        std::vector<MaterialId> layerMaterials;
        float foundationDepth = 1.5f;   // Plinth depth below the lowest ground corner
        float storeyHeight = 3.0f;      // Nominal floor height used to split bands

        // When set, footprint-mode settlements are emitted as merged
        // world-space meshes instead of per-building oriented cubes. The whole
        // batch is uploaded through one staging buffer and one submit; the
        // callee owns the returned meshes (nullptr where upload failed).
        std::function<std::vector<Mesh*>(std::vector<MeshGeometry>)> createMeshes;

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

        // Kit-wall mode: when kitModelsDir is set (and createMeshes/kitMaterial
        // are provided), buildings are assembled from modular kit wall pieces
        // traced along their footprint edges instead of extruded prisms or
        // boxes. Empty disables kit walls (rollback switch).
        std::string kitModelsDir;
        // Resolve a kit glTF material name (e.g. "MI_Plaster") to a MaterialId.
        std::function<MaterialId(const std::string&)> kitMaterial;
        // Per-settlement and global caps on kit wall vertices; buildings past
        // either budget fall back to extruded prisms so GPU memory stays
        // bounded (Vertex is 64 bytes: 8M verts ~= 512MB). Chunks are emitted
        // centre-outward, so overflow prisms land on settlement outskirts.
        size_t kitVertexBudgetPerSettlement = 900000;
        size_t kitVertexBudgetTotal = 8000000;
        // Kit meshes are flushed per spatial cell of this size so their AABBs
        // stay chunk-local and GPU/shadow culling can reject most of a town.
        float kitChunkSize = 128.0f;

        // Caps to respect the GPU scene buffer budget across all settlements
        int maxBuildingsPerSettlement = 500;
        float minFootprintSize = 4.0f;  // Skip footprints smaller than this (meters)
    };

    struct Result {
        std::vector<ecs::Entity> entities;
        uint32_t layoutHash = 0;   // Deterministic hash of all placements
        size_t rejectedSlots = 0;  // Slope/sea/exclusion rejections
        int totalBuildings = 0;
    };

    explicit SettlementBlockoutGenerator(Config config);
    ~SettlementBlockoutGenerator();

    // Creates the building entities for one settlement and returns them; the
    // caller must register them with the scene
    // (SceneBuilder::addExternalSceneEntity). Cumulative stats accumulate in
    // result().
    std::vector<ecs::Entity> generateSettlement(ecs::World& world,
                                                const Settlement& settlement);

    const Result& result() const { return result_; }

    // Log the cumulative totals (call after the last settlement).
    void logSummary() const;

private:
    Config config_;
    Result result_;
    std::unique_ptr<KitBuildingAssembler> kit_;
    size_t totalKitVerts_ = 0;  // Global kit budget spent across settlements
};
