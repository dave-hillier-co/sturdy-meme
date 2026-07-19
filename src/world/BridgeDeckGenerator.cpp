#include "BridgeDeckGenerator.h"
#include "WorldCoords.h"
#include "GeneratedMeshUtil.h"
#include "ecs/EntityFactory.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

using GeneratedMeshUtil::appendOrientedBox;

BridgeDeckGenerator::Result BridgeDeckGenerator::generate(
    ecs::World& world, const std::vector<WaterCrossing>& crossings) {
    Result result;
    if (!config_.getTerrainHeight || !config_.createMeshes) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BridgeDeckGenerator: missing height query or mesh sink");
        return result;
    }

    std::vector<MeshGeometry> pendingGeo;
    struct PendingCollider {
        glm::vec3 center;
        glm::vec3 halfExtents;
        float yaw;
    };
    std::vector<PendingCollider> pendingColliders;

    for (const auto& crossing : crossings) {
        if (!crossing.isBridge) continue;

        glm::vec2 worldXZ = WorldCoords::contentToWorld(crossing.position);
        glm::vec2 dir = crossing.direction;
        float dirLen = glm::length(dir);
        dir = dirLen > 0.001f ? dir / dirLen : glm::vec2(1.0f, 0.0f);
        glm::vec2 perp(-dir.y, dir.x);

        float halfLength = crossing.span * 0.5f + config_.approachLength;
        float halfWidth = getRoadWidth(crossing.roadType) * 0.5f + 0.5f;

        if (config_.preloadTiles) {
            config_.preloadTiles(worldXZ.x, worldXZ.y, halfLength + 20.0f);
        }

        // Deck top sits just above the higher bank so both approaches meet
        // the terrain without a step.
        glm::vec2 bankA = worldXZ - dir * halfLength;
        glm::vec2 bankB = worldXZ + dir * halfLength;
        float heightA = config_.getTerrainHeight(bankA.x, bankA.y);
        float heightB = config_.getTerrainHeight(bankB.x, bankB.y);
        float deckTop = std::max(heightA, heightB) + config_.deckClearance;

        const glm::vec3 axisAlong(dir.x * halfLength, 0.0f, dir.y * halfLength);
        const glm::vec3 axisAcross(perp.x * halfWidth, 0.0f, perp.y * halfWidth);
        float halfThickness = config_.deckThickness * 0.5f;
        glm::vec3 deckCenter(worldXZ.x, deckTop - halfThickness, worldXZ.y);

        MeshGeometry geo;
        appendOrientedBox(geo, deckCenter, axisAlong, axisAcross, halfThickness);

        // Low side rails along both deck edges
        float railHalfHeight = config_.railHeight * 0.5f;
        float railHalfWidth = config_.railWidth * 0.5f;
        for (float side : {-1.0f, 1.0f}) {
            glm::vec2 railXZ = worldXZ + perp * side * (halfWidth - railHalfWidth);
            glm::vec3 railCenter(railXZ.x, deckTop + railHalfHeight, railXZ.y);
            appendOrientedBox(geo, railCenter, axisAlong,
                              glm::vec3(perp.x * railHalfWidth, 0.0f, perp.y * railHalfWidth),
                              railHalfHeight);
        }
        pendingGeo.push_back(std::move(geo));

        // Collider: the walkable deck slab. Yaw convention matches
        // glm::angleAxis(yaw, +Y) rotating local +X onto the road direction.
        float yaw = std::atan2(-dir.y, dir.x);
        pendingColliders.push_back({deckCenter,
                                    glm::vec3(halfLength, halfThickness, halfWidth),
                                    yaw});
        ++result.bridgesBuilt;
    }

    if (pendingGeo.empty()) {
        return result;
    }

    std::vector<Mesh*> meshes = config_.createMeshes(std::move(pendingGeo));
    ecs::EntityFactory factory(world);
    for (Mesh* mesh : meshes) {
        if (!mesh) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "BridgeDeckGenerator: mesh upload failed");
            continue;
        }
        result.entities.push_back(factory.createStaticMesh(
            mesh, config_.deckMaterial, glm::mat4(1.0f), true));
    }
    if (config_.addCollider) {
        for (const auto& c : pendingColliders) {
            config_.addCollider(c.center, c.halfExtents, c.yaw);
        }
    }

    SDL_Log("BridgeDeckGenerator: built %d bridge decks", result.bridgesBuilt);
    return result;
}
