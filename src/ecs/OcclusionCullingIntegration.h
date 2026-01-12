#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <algorithm>
#include "Components.h"

// Forward declaration for HiZ system types
struct CullObjectData;

// Occlusion Culling ECS Integration
// Bridges ECS entities with the GPU-based Hi-Z occlusion culling system
namespace OcclusionECS {

// ============================================================================
// CullObjectData generation (matches HiZSystem struct)
// ============================================================================

// Generate CullObjectData from ECS entity
inline bool buildCullData(
    entt::registry& registry,
    entt::entity entity,
    CullObjectData& outData)
{
    // Need at least transform and bounds
    if (!registry.all_of<Transform>(entity)) return false;

    auto& transform = registry.get<Transform>(entity);
    glm::vec3 worldPos = transform.position;

    // Get bounding sphere
    float radius = 1.0f;
    glm::vec3 sphereCenter = worldPos;

    if (registry.all_of<CullBoundingSphere>(entity)) {
        auto& sphere = registry.get<CullBoundingSphere>(entity);
        sphereCenter = worldPos + sphere.center;
        radius = sphere.radius;
    } else if (registry.all_of<BoundingSphere>(entity)) {
        radius = registry.get<BoundingSphere>(entity).radius;
    }

    // Get AABB
    glm::vec3 aabbMin = worldPos - glm::vec3(radius);
    glm::vec3 aabbMax = worldPos + glm::vec3(radius);

    if (registry.all_of<AABBBounds>(entity)) {
        auto& bounds = registry.get<AABBBounds>(entity);
        // Apply scale if hierarchy exists
        glm::vec3 scale(1.0f);
        if (registry.all_of<Hierarchy>(entity)) {
            scale = registry.get<Hierarchy>(entity).localScale;
        }
        aabbMin = worldPos + bounds.min * scale;
        aabbMax = worldPos + bounds.max * scale;
    }

    // Fill output data
    outData.boundingSphere = glm::vec4(sphereCenter, radius);
    outData.aabbMin = glm::vec4(aabbMin, 0.0f);
    outData.aabbMax = glm::vec4(aabbMax, 0.0f);
    outData.meshIndex = 0;
    outData.firstIndex = 0;
    outData.indexCount = 0;
    outData.vertexOffset = 0;

    return true;
}

// ============================================================================
// Batch operations
// ============================================================================

// Collect all cullable entities and build object data
inline std::vector<CullObjectData> buildCullObjectList(
    entt::registry& registry,
    std::vector<entt::entity>* outEntities = nullptr)
{
    std::vector<CullObjectData> result;
    result.reserve(1024);

    if (outEntities) {
        outEntities->clear();
        outEntities->reserve(1024);
    }

    uint32_t index = 0;
    auto view = registry.view<OcclusionCullable, Transform>();

    for (auto entity : view) {
        // Skip never-cull entities
        if (registry.all_of<NeverCull>(entity)) continue;

        CullObjectData data{};
        if (buildCullData(registry, entity, data)) {
            data.meshIndex = index;  // Use as entity index

            // Update entity's cull index
            auto& cullable = view.get<OcclusionCullable>(entity);
            cullable.cullIndex = index;

            result.push_back(data);
            if (outEntities) {
                outEntities->push_back(entity);
            }
            index++;
        }
    }

    return result;
}

// Update visibility results from culling system
inline void updateVisibilityResults(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    const std::vector<bool>& visible)
{
    size_t count = std::min(entities.size(), visible.size());

    for (size_t i = 0; i < count; i++) {
        auto entity = entities[i];
        if (!registry.valid(entity)) continue;

        if (registry.all_of<OcclusionCullable>(entity)) {
            auto& cullable = registry.get<OcclusionCullable>(entity);
            bool wasVisible = cullable.wasVisibleLastFrame;
            bool nowVisible = visible[i];

            cullable.wasVisibleLastFrame = nowVisible;

            if (nowVisible) {
                cullable.invisibleFrames = 0;
                // Mark as visible for rendering
                registry.emplace_or_replace<WasVisible>(entity);
            } else {
                cullable.invisibleFrames++;
                registry.remove<WasVisible>(entity);
            }
        }
    }
}

// ============================================================================
// Entity factory functions
// ============================================================================

// Make entity cullable (add occlusion culling components)
inline void makeOcclusionCullable(
    entt::registry& registry,
    entt::entity entity,
    float boundingRadius = 1.0f)
{
    if (!registry.all_of<OcclusionCullable>(entity)) {
        registry.emplace<OcclusionCullable>(entity);
    }

    if (!registry.all_of<CullBoundingSphere>(entity)) {
        CullBoundingSphere sphere;
        sphere.radius = boundingRadius;
        registry.emplace<CullBoundingSphere>(entity, sphere);
    }
}

// Make entity an occluder (will block visibility of other objects)
inline void makeOccluder(
    entt::registry& registry,
    entt::entity entity,
    Occluder::Shape shape = Occluder::Shape::Box)
{
    registry.emplace_or_replace<IsOccluder>(entity);

    if (!registry.all_of<Occluder>(entity)) {
        Occluder occ;
        occ.shape = shape;
        registry.emplace<Occluder>(entity, occ);
    }
}

// Create a visibility cell entity
inline entt::entity createVisibilityCell(
    entt::registry& registry,
    uint32_t cellId,
    const glm::vec3& center,
    const glm::vec3& extents,
    const std::string& name = "VisibilityCell")
{
    auto entity = registry.create();

    VisibilityCell cell;
    cell.cellId = cellId;
    cell.center = center;
    cell.extents = extents;
    registry.emplace<VisibilityCell>(entity, cell);

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name + "_" + std::to_string(cellId);
    info.icon = "C";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create an occlusion portal entity
inline entt::entity createOcclusionPortal(
    entt::registry& registry,
    const std::vector<glm::vec3>& vertices,
    const glm::vec3& position,
    const std::string& name = "Portal")
{
    auto entity = registry.create();

    OcclusionPortal portal;
    portal.vertices = vertices;
    // Calculate normal from first 3 vertices
    if (vertices.size() >= 3) {
        glm::vec3 edge1 = vertices[1] - vertices[0];
        glm::vec3 edge2 = vertices[2] - vertices[0];
        portal.normal = glm::normalize(glm::cross(edge1, edge2));
    }
    registry.emplace<OcclusionPortal>(entity, portal);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    EntityInfo info;
    info.name = name;
    info.icon = "P";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// ============================================================================
// Query functions
// ============================================================================

// Get all cullable entities
inline std::vector<entt::entity> getCullableEntities(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<OcclusionCullable>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all visible entities (passed culling last frame)
inline std::vector<entt::entity> getVisibleEntities(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<WasVisible>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all occluder entities
inline std::vector<entt::entity> getOccluders(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsOccluder>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get entities that have been invisible for N frames
inline std::vector<entt::entity> getLongInvisibleEntities(
    entt::registry& registry,
    uint32_t minInvisibleFrames)
{
    std::vector<entt::entity> result;
    auto view = registry.view<OcclusionCullable>();
    for (auto entity : view) {
        auto& cullable = view.get<OcclusionCullable>(entity);
        if (cullable.invisibleFrames >= minInvisibleFrames) {
            result.push_back(entity);
        }
    }
    return result;
}

// Find visibility cell containing position
inline entt::entity findContainingCell(
    entt::registry& registry,
    const glm::vec3& position)
{
    auto view = registry.view<VisibilityCell, Transform>();
    for (auto entity : view) {
        auto& cell = view.get<VisibilityCell>(entity);
        auto& transform = view.get<Transform>(entity);

        glm::vec3 localPos = position - transform.position;
        if (glm::abs(localPos.x) <= cell.extents.x &&
            glm::abs(localPos.y) <= cell.extents.y &&
            glm::abs(localPos.z) <= cell.extents.z)
        {
            return entity;
        }
    }
    return entt::null;
}

// ============================================================================
// Frustum culling (CPU fallback)
// ============================================================================

// Extract frustum planes from view-projection matrix
inline void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // Left
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]);
    // Right
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]);
    // Bottom
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]);
    // Top
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]);
    // Near
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]);
    // Far
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]);

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }
}

// Test sphere against frustum
inline bool sphereInFrustum(const glm::vec4 planes[6], const glm::vec3& center, float radius) {
    for (int i = 0; i < 6; i++) {
        float dist = glm::dot(glm::vec3(planes[i]), center) + planes[i].w;
        if (dist < -radius) {
            return false;  // Completely outside this plane
        }
    }
    return true;
}

// CPU frustum culling for entities
inline std::vector<entt::entity> frustumCull(
    entt::registry& registry,
    const glm::mat4& viewProjMatrix)
{
    glm::vec4 frustumPlanes[6];
    extractFrustumPlanes(viewProjMatrix, frustumPlanes);

    std::vector<entt::entity> visible;
    auto view = registry.view<OcclusionCullable, Transform>();

    for (auto entity : view) {
        // Skip never-cull
        if (registry.all_of<NeverCull>(entity)) {
            visible.push_back(entity);
            continue;
        }

        auto& transform = view.get<Transform>(entity);

        // Get bounds
        float radius = 1.0f;
        glm::vec3 center = transform.position;

        if (registry.all_of<CullBoundingSphere>(entity)) {
            auto& sphere = registry.get<CullBoundingSphere>(entity);
            center = transform.position + sphere.center;
            radius = sphere.radius;
        } else if (registry.all_of<BoundingSphere>(entity)) {
            radius = registry.get<BoundingSphere>(entity).radius;
        }

        // Apply scale
        if (registry.all_of<Hierarchy>(entity)) {
            float maxScale = glm::compMax(registry.get<Hierarchy>(entity).localScale);
            radius *= maxScale;
        }

        if (sphereInFrustum(frustumPlanes, center, radius)) {
            visible.push_back(entity);
        }
    }

    return visible;
}

// ============================================================================
// Statistics
// ============================================================================

struct OcclusionStats {
    uint32_t totalCullable;
    uint32_t visibleEntities;
    uint32_t occludedEntities;
    uint32_t neverCullEntities;
    uint32_t occluderCount;
    uint32_t portalCount;
    uint32_t cellCount;
};

inline OcclusionStats getOcclusionStats(entt::registry& registry) {
    OcclusionStats stats{};

    stats.totalCullable = static_cast<uint32_t>(registry.view<OcclusionCullable>().size());
    stats.visibleEntities = static_cast<uint32_t>(registry.view<WasVisible>().size());
    stats.occludedEntities = stats.totalCullable > stats.visibleEntities ?
                             stats.totalCullable - stats.visibleEntities : 0;
    stats.neverCullEntities = static_cast<uint32_t>(registry.view<NeverCull>().size());
    stats.occluderCount = static_cast<uint32_t>(registry.view<IsOccluder>().size());
    stats.portalCount = static_cast<uint32_t>(registry.view<OcclusionPortal>().size());
    stats.cellCount = static_cast<uint32_t>(registry.view<VisibilityCell>().size());

    return stats;
}

}  // namespace OcclusionECS
