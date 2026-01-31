#pragma once

#include "World.h"
#include "Components.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace ecs {

// =============================================================================
// Frustum for culling operations
// =============================================================================

struct Frustum {
    // Plane: ax + by + cz + d = 0, stored as vec4(a, b, c, d)
    // Planes: left, right, bottom, top, near, far
    std::array<glm::vec4, 6> planes;

    // Extract frustum planes from view-projection matrix
    static Frustum fromViewProjection(const glm::mat4& vp) {
        Frustum f;
        // Left plane
        f.planes[0] = glm::vec4(
            vp[0][3] + vp[0][0],
            vp[1][3] + vp[1][0],
            vp[2][3] + vp[2][0],
            vp[3][3] + vp[3][0]
        );
        // Right plane
        f.planes[1] = glm::vec4(
            vp[0][3] - vp[0][0],
            vp[1][3] - vp[1][0],
            vp[2][3] - vp[2][0],
            vp[3][3] - vp[3][0]
        );
        // Bottom plane
        f.planes[2] = glm::vec4(
            vp[0][3] + vp[0][1],
            vp[1][3] + vp[1][1],
            vp[2][3] + vp[2][1],
            vp[3][3] + vp[3][1]
        );
        // Top plane
        f.planes[3] = glm::vec4(
            vp[0][3] - vp[0][1],
            vp[1][3] - vp[1][1],
            vp[2][3] - vp[2][1],
            vp[3][3] - vp[3][1]
        );
        // Near plane
        f.planes[4] = glm::vec4(
            vp[0][3] + vp[0][2],
            vp[1][3] + vp[1][2],
            vp[2][3] + vp[2][2],
            vp[3][3] + vp[3][2]
        );
        // Far plane
        f.planes[5] = glm::vec4(
            vp[0][3] - vp[0][2],
            vp[1][3] - vp[1][2],
            vp[2][3] - vp[2][2],
            vp[3][3] - vp[3][2]
        );

        // Normalize planes
        for (auto& plane : f.planes) {
            float length = glm::length(glm::vec3(plane));
            if (length > 0.0f) {
                plane /= length;
            }
        }
        return f;
    }

    // Test if sphere is inside or intersecting frustum
    [[nodiscard]] bool containsSphere(const glm::vec3& center, float radius) const {
        for (const auto& plane : planes) {
            float distance = glm::dot(glm::vec3(plane), center) + plane.w;
            if (distance < -radius) {
                return false;  // Completely outside
            }
        }
        return true;  // Inside or intersecting
    }

    // Test if AABB is inside or intersecting frustum
    [[nodiscard]] bool containsAABB(const glm::vec3& min, const glm::vec3& max) const {
        for (const auto& plane : planes) {
            glm::vec3 p = min;
            if (plane.x >= 0) p.x = max.x;
            if (plane.y >= 0) p.y = max.y;
            if (plane.z >= 0) p.z = max.z;

            if (glm::dot(glm::vec3(plane), p) + plane.w < 0) {
                return false;  // Completely outside
            }
        }
        return true;  // Inside or intersecting
    }
};

// =============================================================================
// Visibility Culling System
// =============================================================================
// Updates the Visible tag component based on frustum culling.
// Entities must have Transform and BoundingSphere/BoundingBox.

namespace systems {

// CPU-based frustum culling using bounding spheres
// Adds/removes Visible tag component based on frustum test
inline void updateVisibility(World& world, const Frustum& frustum) {
    // Process entities with bounding spheres
    for (auto [entity, transform, bounds] :
         world.view<Transform, BoundingSphere>().each()) {

        // Transform bounds center to world space
        glm::vec3 worldCenter = glm::vec3(transform.matrix * glm::vec4(bounds.center, 1.0f));

        // Approximate scale from transform matrix for radius
        float scaleX = glm::length(glm::vec3(transform.matrix[0]));
        float scaleY = glm::length(glm::vec3(transform.matrix[1]));
        float scaleZ = glm::length(glm::vec3(transform.matrix[2]));
        float maxScale = glm::max(scaleX, glm::max(scaleY, scaleZ));
        float worldRadius = bounds.radius * maxScale;

        if (frustum.containsSphere(worldCenter, worldRadius)) {
            if (!world.has<Visible>(entity)) {
                world.add<Visible>(entity);
            }
        } else {
            if (world.has<Visible>(entity)) {
                world.remove<Visible>(entity);
            }
        }
    }

    // Process entities with bounding boxes (that don't have spheres)
    for (auto [entity, transform, bounds] :
         world.view<Transform, BoundingBox>(entt::exclude<BoundingSphere>).each()) {

        // Transform AABB to world space (approximate - uses transformed corners)
        glm::vec3 corners[8] = {
            glm::vec3(bounds.min.x, bounds.min.y, bounds.min.z),
            glm::vec3(bounds.max.x, bounds.min.y, bounds.min.z),
            glm::vec3(bounds.min.x, bounds.max.y, bounds.min.z),
            glm::vec3(bounds.max.x, bounds.max.y, bounds.min.z),
            glm::vec3(bounds.min.x, bounds.min.y, bounds.max.z),
            glm::vec3(bounds.max.x, bounds.min.y, bounds.max.z),
            glm::vec3(bounds.min.x, bounds.max.y, bounds.max.z),
            glm::vec3(bounds.max.x, bounds.max.y, bounds.max.z)
        };

        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());

        for (const auto& corner : corners) {
            glm::vec3 worldCorner = glm::vec3(transform.matrix * glm::vec4(corner, 1.0f));
            worldMin = glm::min(worldMin, worldCorner);
            worldMax = glm::max(worldMax, worldCorner);
        }

        if (frustum.containsAABB(worldMin, worldMax)) {
            if (!world.has<Visible>(entity)) {
                world.add<Visible>(entity);
            }
        } else {
            if (world.has<Visible>(entity)) {
                world.remove<Visible>(entity);
            }
        }
    }
}

// LOD system - updates LOD levels based on distance from camera
inline void updateLOD(World& world, const glm::vec3& cameraPos) {
    for (auto [entity, transform, lod] : world.view<Transform, LODController>().each()) {
        float dist = glm::distance(cameraPos, transform.position());

        // Determine LOD level
        uint8_t newLevel;
        if (dist < lod.thresholds[0]) {
            newLevel = 0;  // High detail
        } else if (dist < lod.thresholds[1]) {
            newLevel = 1;  // Medium detail
        } else {
            newLevel = 2;  // Low detail
        }

        lod.currentLevel = newLevel;

        // Update interval based on LOD (distant objects update less frequently)
        switch (newLevel) {
            case 0: lod.updateInterval = 1; break;    // Every frame
            case 1: lod.updateInterval = 4; break;    // Every 4 frames
            case 2: lod.updateInterval = 16; break;   // Every 16 frames
            default: lod.updateInterval = 1; break;
        }
    }
}

// Physics sync system - updates transforms from physics bodies
// Note: Requires PhysicsSystem pointer to be passed in
template<typename PhysicsSystem>
void syncPhysicsTransforms(World& world, PhysicsSystem& physics) {
    for (auto [entity, body, transform] : world.view<PhysicsBody, Transform>().each()) {
        if (body.valid()) {
            transform.matrix = physics.getBodyTransform(body.bodyId);
        }
    }
}

} // namespace systems

// =============================================================================
// Render Batching Helpers
// =============================================================================
// Helpers for efficient batch rendering based on ECS queries.

namespace render {

// Statistics for visibility culling
struct CullStats {
    size_t totalEntities = 0;
    size_t visibleEntities = 0;
    size_t culledEntities = 0;

    [[nodiscard]] float visibilityRatio() const {
        return totalEntities > 0 ?
            static_cast<float>(visibleEntities) / static_cast<float>(totalEntities) : 0.0f;
    }
};

// Count visible vs total entities for profiling
inline CullStats getCullStats(const World& world) {
    CullStats stats;

    // Count entities with transforms (renderable)
    for ([[maybe_unused]] auto entity : world.view<Transform>()) {
        stats.totalEntities++;
    }

    // Count visible entities
    for ([[maybe_unused]] auto entity : world.view<Transform, Visible>()) {
        stats.visibleEntities++;
    }

    stats.culledEntities = stats.totalEntities - stats.visibleEntities;
    return stats;
}

// Batch key for grouping draw calls
struct BatchKey {
    const void* mesh;      // Mesh pointer for comparison
    MaterialId materialId;

    bool operator==(const BatchKey& other) const {
        return mesh == other.mesh && materialId == other.materialId;
    }

    bool operator<(const BatchKey& other) const {
        if (mesh != other.mesh) return mesh < other.mesh;
        return materialId < other.materialId;
    }
};

} // namespace render

} // namespace ecs
