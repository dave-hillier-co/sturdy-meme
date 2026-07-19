#pragma once

#include "RibbonMeshGenerator.h"
#include "SettlementRegistry.h"
#include "material/MaterialId.h"

#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

// Loads the street and wall LineStrings that town_generator exports in
// town_<id>.geojson (previously unrendered) and turns them into world
// geometry: streets become draped ribbons, walls become extruded runs with
// colliders.
class TownLinearFeatures {
public:
    struct Config {
        std::string townsDir;   // Directory containing town_<id>.geojson
        // Directory containing the street generator's settlement_<id>/
        // streets.geojson (used when the town layout has no street lines)
        std::string streetsDir;

        // Street ribbon materials/colors by prominence
        MaterialId streetMaterial = 0;
        glm::vec4 arteryColor{0.85f, 0.82f, 0.75f, 1.0f};
        glm::vec4 streetColor{0.75f, 0.70f, 0.62f, 1.0f};
        glm::vec4 alleyColor{0.62f, 0.58f, 0.52f, 1.0f};

        MaterialId wallMaterial = 0;
        float wallHeight = 5.0f;
        float wallThickness = 1.0f;

        // Register a static box collider (center, half extents, yaw)
        std::function<void(const glm::vec3&, const glm::vec3&, float)> addCollider;
        // Query world-space terrain height at (x, z)
        std::function<float(float, float)> getTerrainHeight;
        // Upload world-space meshes; returns one Mesh* per geometry
        std::function<std::vector<Mesh*>(std::vector<MeshGeometry>)> createMeshes;
    };

    struct Result {
        std::vector<ecs::Entity> entities;
        size_t streetChunks = 0;
        size_t wallSegments = 0;
    };

    explicit TownLinearFeatures(Config config) : config_(std::move(config)) {}

    // Generate street ribbons and wall runs for one settlement. ribbonGen is
    // shared with the road ribbons so chunking config stays consistent.
    Result generateForSettlement(ecs::World& world, RibbonMeshGenerator& ribbonGen,
                                 const Settlement& settlement);

private:
    Config config_;
};
