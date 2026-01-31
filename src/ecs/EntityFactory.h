#pragma once

#include "World.h"
#include "Components.h"
#include "core/RenderableBuilder.h"

namespace ecs {

// =============================================================================
// Entity Factory
// =============================================================================
// Provides helpers for creating ECS entities from existing data structures.
// This bridges the gap between the current Renderable-based system and ECS.

class EntityFactory {
public:
    explicit EntityFactory(World& world) : world_(world) {}

    // -------------------------------------------------------------------------
    // Create entity from existing Renderable
    // -------------------------------------------------------------------------
    // Converts a Renderable struct into an ECS entity with appropriate components.
    // This is the primary migration path from the current rendering system.
    //
    // Components added:
    //   - Transform (always)
    //   - MeshRef (always)
    //   - MaterialRef (if valid material ID)
    //   - CastsShadow (if renderable casts shadows)
    //   - PBRProperties (if non-default values)
    //   - HueShift (if non-zero)
    //   - Opacity (if non-default)
    //   - TreeData (if tree-related indices set)

    [[nodiscard]] Entity createFromRenderable(const Renderable& renderable) {
        Entity entity = world_.create();

        // Core components - always present
        world_.add<Transform>(entity, renderable.transform);
        world_.add<MeshRef>(entity, renderable.mesh);

        // Material reference
        if (renderable.materialId != INVALID_MATERIAL_ID) {
            world_.add<MaterialRef>(entity, renderable.materialId);
        }

        // Shadow casting
        if (renderable.castsShadow) {
            world_.add<CastsShadow>(entity);
        }

        // PBR properties - only add if non-default
        if (hasCustomPBR(renderable)) {
            PBRProperties pbr;
            pbr.roughness = renderable.roughness;
            pbr.metallic = renderable.metallic;
            pbr.emissiveIntensity = renderable.emissiveIntensity;
            pbr.emissiveColor = renderable.emissiveColor;
            pbr.alphaTestThreshold = renderable.alphaTestThreshold;
            pbr.pbrFlags = renderable.pbrFlags;
            world_.add<PBRProperties>(entity, pbr);
        }

        // Hue shift for NPCs
        if (renderable.hueShift != 0.0f) {
            world_.add<HueShift>(entity, renderable.hueShift);
        }

        // Opacity for fade effects
        if (renderable.opacity != 1.0f) {
            world_.add<Opacity>(entity, renderable.opacity);
        }

        // Tree-specific data
        if (isTree(renderable)) {
            TreeData treeData;
            treeData.leafInstanceIndex = renderable.leafInstanceIndex;
            treeData.treeInstanceIndex = renderable.treeInstanceIndex;
            treeData.leafTint = renderable.leafTint;
            treeData.autumnHueShift = renderable.autumnHueShift;
            world_.add<TreeData>(entity, treeData);
        }

        return entity;
    }

    // -------------------------------------------------------------------------
    // Batch create entities from vector of Renderables
    // -------------------------------------------------------------------------
    // Returns a vector of entity handles corresponding to each renderable.

    [[nodiscard]] std::vector<Entity> createFromRenderables(
        const std::vector<Renderable>& renderables) {

        std::vector<Entity> entities;
        entities.reserve(renderables.size());

        for (const auto& renderable : renderables) {
            entities.push_back(createFromRenderable(renderable));
        }

        return entities;
    }

    // -------------------------------------------------------------------------
    // Create basic static mesh entity
    // -------------------------------------------------------------------------
    // Simplified factory for common static mesh objects.

    [[nodiscard]] Entity createStaticMesh(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        bool castsShadow = true) {

        Entity entity = world_.create();
        world_.add<Transform>(entity, transform);
        world_.add<MeshRef>(entity, mesh);
        world_.add<MaterialRef>(entity, materialId);

        if (castsShadow) {
            world_.add<CastsShadow>(entity);
        }

        return entity;
    }

    // -------------------------------------------------------------------------
    // Create entity with bounding sphere for culling
    // -------------------------------------------------------------------------

    [[nodiscard]] Entity createWithBounds(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        const glm::vec3& boundCenter,
        float boundRadius,
        bool castsShadow = true) {

        Entity entity = createStaticMesh(mesh, materialId, transform, castsShadow);
        world_.add<BoundingSphere>(entity, boundCenter, boundRadius);

        return entity;
    }

    // -------------------------------------------------------------------------
    // Create NPC entity
    // -------------------------------------------------------------------------
    // NPCs have hue shift for visual variety.

    [[nodiscard]] Entity createNPC(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        float hueShift) {

        Entity entity = createStaticMesh(mesh, materialId, transform, true);
        world_.add<HueShift>(entity, hueShift);

        return entity;
    }

    // -------------------------------------------------------------------------
    // Create tree entity
    // -------------------------------------------------------------------------

    [[nodiscard]] Entity createTree(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        int treeInstanceIndex,
        int leafInstanceIndex,
        const glm::vec3& leafTint = glm::vec3(1.0f),
        float autumnHueShift = 0.0f) {

        Entity entity = createStaticMesh(mesh, materialId, transform, true);

        TreeData treeData;
        treeData.treeInstanceIndex = treeInstanceIndex;
        treeData.leafInstanceIndex = leafInstanceIndex;
        treeData.leafTint = leafTint;
        treeData.autumnHueShift = autumnHueShift;
        world_.add<TreeData>(entity, treeData);

        return entity;
    }

    // -------------------------------------------------------------------------
    // Utility helpers
    // -------------------------------------------------------------------------

private:
    // Check if renderable has non-default PBR properties
    static bool hasCustomPBR(const Renderable& r) {
        return r.roughness != 0.5f ||
               r.metallic != 0.0f ||
               r.emissiveIntensity != 0.0f ||
               r.emissiveColor != glm::vec3(1.0f) ||
               r.alphaTestThreshold != 0.0f ||
               r.pbrFlags != 0;
    }

    // Check if renderable represents a tree
    static bool isTree(const Renderable& r) {
        return r.treeInstanceIndex >= 0 || r.leafInstanceIndex >= 0;
    }

    World& world_;
};

// =============================================================================
// Sync utilities - for keeping ECS in sync during migration
// =============================================================================

// Update ECS Transform from Renderable transform (for objects that still
// use the old system but are mirrored in ECS)
inline void syncTransformFromRenderable(
    World& world,
    Entity entity,
    const Renderable& renderable) {

    if (world.has<Transform>(entity)) {
        world.get<Transform>(entity).matrix = renderable.transform;
    }
}

// Update Renderable transform from ECS (for physics-driven objects)
inline void syncRenderableFromTransform(
    Renderable& renderable,
    const World& world,
    Entity entity) {

    if (world.has<Transform>(entity)) {
        renderable.transform = world.get<Transform>(entity).matrix;
    }
}

} // namespace ecs
