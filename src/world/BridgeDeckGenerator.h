#pragma once

#include "terrain/RoadNetworkLoader.h"
#include "material/MaterialId.h"
#include "core/Mesh.h"
#include "ecs/World.h"

#include <glm/glm.hpp>
#include <functional>
#include <vector>

// Builds simple walkable bridge decks at the road network's bridge
// crossings (fords are baked into the terrain texture instead). Each bridge
// is a flat deck spanning the banks plus low side rails, with a static box
// collider so the road stays walkable across the river.
class BridgeDeckGenerator {
public:
    struct Config {
        // Query world-space terrain height at (x, z)
        std::function<float(float, float)> getTerrainHeight;
        // Optional: pre-load terrain tiles around (x, z, radius)
        std::function<void(float, float, float)> preloadTiles;
        // Upload world-space meshes; returns one Mesh* per geometry
        std::function<std::vector<Mesh*>(std::vector<MeshGeometry>)> createMeshes;
        // Register a static box collider (center, half extents, yaw)
        std::function<void(const glm::vec3&, const glm::vec3&, float)> addCollider;

        MaterialId deckMaterial = 0;
        float deckThickness = 0.5f;   // Deck slab thickness
        float deckClearance = 0.3f;   // Deck top above the higher bank
        float railHeight = 0.9f;      // Side rail height above deck
        float railWidth = 0.25f;      // Side rail thickness
        float approachLength = 4.0f;  // Extra deck beyond the water span per side
    };

    struct Result {
        std::vector<ecs::Entity> entities;
        int bridgesBuilt = 0;
    };

    explicit BridgeDeckGenerator(Config config) : config_(std::move(config)) {}

    // Generate decks for every bridge crossing. Crossing positions are
    // content-space (as loaded from roads.geojson).
    Result generate(ecs::World& world, const std::vector<WaterCrossing>& crossings);

private:
    Config config_;
};
