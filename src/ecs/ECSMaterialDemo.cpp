#include "ECSMaterialDemo.h"
#include "MaterialRegistry.h"
#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include <SDL3/SDL_log.h>
#include <cmath>
#include <unordered_set>

namespace ecs {

std::unique_ptr<ECSMaterialDemo> ECSMaterialDemo::create(const InitInfo& info) {
    auto instance = std::make_unique<ECSMaterialDemo>(ConstructToken{});
    if (!instance->initInternal(info)) {
        return nullptr;
    }
    return instance;
}

bool ECSMaterialDemo::initInternal(const InitInfo& info) {
    if (!info.world) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ECSMaterialDemo: World is required");
        return false;
    }
    if (!info.cubeMesh || !info.sphereMesh) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ECSMaterialDemo: Meshes are required");
        return false;
    }
    if (!info.materialRegistry) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ECSMaterialDemo: MaterialRegistry is required");
        return false;
    }

    world_ = info.world;
    terrainHeightFunc_ = info.getTerrainHeight;
    sceneOrigin_ = info.sceneOrigin;

    // Create demo entities for each feature
    createPBRDemoEntities(info);
    createOverlayDemoEntities(info);
    createSelectionDemoEntities(info);

    initialized_ = true;
    SDL_Log("ECSMaterialDemo: Created %zu demo entities", demoEntities_.size());
    return true;
}

float ECSMaterialDemo::getTerrainHeight(float x, float z) const {
    if (terrainHeightFunc_) {
        return terrainHeightFunc_(x, z);
    }
    return 0.0f;
}

void ECSMaterialDemo::createPBRDemoEntities(const InitInfo& info) {
    EntityFactory factory(*world_);

    const float originX = sceneOrigin_.x;
    const float originZ = sceneOrigin_.y;

    // Demo row offset from main scene objects
    const float demoRowZ = -10.0f;

    // Get material IDs from registry
    MaterialId metalId = info.materialRegistry->getMaterialId("metal");
    MaterialId crateId = info.materialRegistry->getMaterialId("crate");

    if (metalId == INVALID_MATERIAL_ID) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ECSMaterialDemo: 'metal' material not found");
    }
    if (crateId == INVALID_MATERIAL_ID) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ECSMaterialDemo: 'crate' material not found");
    }

    // -------------------------------------------------------------------------
    // PBR Demo: Varying roughness gradient (5 spheres)
    // -------------------------------------------------------------------------
    SDL_Log("ECSMaterialDemo: Creating PBR roughness gradient demo");
    for (int i = 0; i < 5; ++i) {
        float x = originX - 4.0f + i * 2.0f;
        float z = originZ + demoRowZ;
        float y = getTerrainHeight(x, z) + 0.5f;

        float roughness = 0.1f + i * 0.2f;  // 0.1, 0.3, 0.5, 0.7, 0.9

        Entity entity = factory.createStaticMesh(
            info.sphereMesh,
            metalId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true  // castsShadow
        );

        // Add custom PBR properties
        PBRProperties pbr;
        pbr.roughness = roughness;
        pbr.metallic = 1.0f;  // Fully metallic
        pbr.emissiveIntensity = 0.0f;
        world_->add<PBRProperties>(entity, pbr);
        world_->add<DebugName>(entity, "PBR_Roughness_Demo");

        demoEntities_.push_back(entity);
        pbrDemoEntities_.push_back(entity);
    }

    // -------------------------------------------------------------------------
    // PBR Demo: Varying metallic gradient (5 cubes)
    // -------------------------------------------------------------------------
    SDL_Log("ECSMaterialDemo: Creating PBR metallic gradient demo");
    for (int i = 0; i < 5; ++i) {
        float x = originX - 4.0f + i * 2.0f;
        float z = originZ + demoRowZ - 2.5f;
        float y = getTerrainHeight(x, z) + 0.5f;

        float metallic = i * 0.25f;  // 0.0, 0.25, 0.5, 0.75, 1.0

        Entity entity = factory.createStaticMesh(
            info.cubeMesh,
            crateId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        PBRProperties pbr;
        pbr.roughness = 0.3f;
        pbr.metallic = metallic;
        world_->add<PBRProperties>(entity, pbr);
        world_->add<DebugName>(entity, "PBR_Metallic_Demo");

        demoEntities_.push_back(entity);
        pbrDemoEntities_.push_back(entity);
    }

    // -------------------------------------------------------------------------
    // PBR Demo: Emissive gradient (5 spheres, different colors)
    // -------------------------------------------------------------------------
    SDL_Log("ECSMaterialDemo: Creating PBR emissive demo");
    const glm::vec3 emissiveColors[] = {
        glm::vec3(1.0f, 0.0f, 0.0f),  // Red
        glm::vec3(1.0f, 0.5f, 0.0f),  // Orange
        glm::vec3(1.0f, 1.0f, 0.0f),  // Yellow
        glm::vec3(0.0f, 1.0f, 0.0f),  // Green
        glm::vec3(0.0f, 0.5f, 1.0f),  // Blue
    };

    for (int i = 0; i < 5; ++i) {
        float x = originX - 4.0f + i * 2.0f;
        float z = originZ + demoRowZ - 5.0f;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.sphereMesh,
            metalId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            false  // Don't cast shadows from emissive objects
        );

        PBRProperties pbr;
        pbr.roughness = 0.2f;
        pbr.metallic = 0.0f;
        pbr.emissiveIntensity = 2.0f + i * 1.5f;  // 2.0, 3.5, 5.0, 6.5, 8.0
        pbr.emissiveColor = emissiveColors[i];
        world_->add<PBRProperties>(entity, pbr);
        world_->add<DebugName>(entity, "PBR_Emissive_Demo");

        demoEntities_.push_back(entity);
        pbrDemoEntities_.push_back(entity);
    }

    SDL_Log("ECSMaterialDemo: Created %zu PBR demo entities", pbrDemoEntities_.size());
}

void ECSMaterialDemo::createTreeDemoEntities(const InitInfo& info) {
    // Tree entities require the TreeSystem to be set up properly
    // For now, we just demonstrate the ECS TreeData component structure
    // Full tree integration happens via TreeSystem
    SDL_Log("ECSMaterialDemo: Tree demo entities use TreeSystem - skipping standalone creation");
}

void ECSMaterialDemo::createOverlayDemoEntities(const InitInfo& info) {
    EntityFactory factory(*world_);

    const float originX = sceneOrigin_.x;
    const float originZ = sceneOrigin_.y;
    const float demoRowZ = -15.0f;

    MaterialId metalId = info.materialRegistry->getMaterialId("metal");
    MaterialId crateId = info.materialRegistry->getMaterialId("crate");

    // -------------------------------------------------------------------------
    // Wetness Overlay Demo: Row of cubes with varying wetness
    // -------------------------------------------------------------------------
    SDL_Log("ECSMaterialDemo: Creating wetness overlay demo");
    for (int i = 0; i < 5; ++i) {
        float x = originX - 4.0f + i * 2.0f;
        float z = originZ + demoRowZ;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.cubeMesh,
            crateId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        // Add wetness overlay with varying initial values
        float initialWetness = i * 0.25f;  // 0.0, 0.25, 0.5, 0.75, 1.0
        world_->add<WetnessOverlay>(entity, initialWetness);
        world_->add<DebugName>(entity, "Wetness_Demo");

        demoEntities_.push_back(entity);
        wetEntities_.push_back(entity);
    }

    // -------------------------------------------------------------------------
    // Damage Overlay Demo: Row of spheres with varying damage
    // -------------------------------------------------------------------------
    SDL_Log("ECSMaterialDemo: Creating damage overlay demo");
    for (int i = 0; i < 5; ++i) {
        float x = originX - 4.0f + i * 2.0f;
        float z = originZ + demoRowZ - 2.5f;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.sphereMesh,
            metalId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        // Add damage overlay with varying initial values
        float initialDamage = i * 0.25f;  // 0.0, 0.25, 0.5, 0.75, 1.0
        world_->add<DamageOverlay>(entity, initialDamage);
        world_->add<DebugName>(entity, "Damage_Demo");

        demoEntities_.push_back(entity);
        damagedEntities_.push_back(entity);
    }

    // -------------------------------------------------------------------------
    // Combined Overlay Demo: Cubes with both wetness and damage
    // -------------------------------------------------------------------------
    SDL_Log("ECSMaterialDemo: Creating combined overlay demo");
    for (int i = 0; i < 3; ++i) {
        float x = originX - 2.0f + i * 2.0f;
        float z = originZ + demoRowZ - 5.0f;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.cubeMesh,
            crateId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        // Both overlays active
        world_->add<WetnessOverlay>(entity, 0.5f);
        world_->add<DamageOverlay>(entity, 0.3f);
        world_->add<DebugName>(entity, "Combined_Overlay_Demo");

        demoEntities_.push_back(entity);
        wetEntities_.push_back(entity);
        damagedEntities_.push_back(entity);
    }

    SDL_Log("ECSMaterialDemo: Created %zu wet entities, %zu damaged entities",
            wetEntities_.size(), damagedEntities_.size());
}

void ECSMaterialDemo::createSelectionDemoEntities(const InitInfo& info) {
    EntityFactory factory(*world_);

    const float originX = sceneOrigin_.x;
    const float originZ = sceneOrigin_.y;
    const float demoRowZ = -20.0f;

    MaterialId metalId = info.materialRegistry->getMaterialId("metal");
    MaterialId crateId = info.materialRegistry->getMaterialId("crate");

    // -------------------------------------------------------------------------
    // Selection Outline Demo: Various selection styles
    // -------------------------------------------------------------------------
    SDL_Log("ECSMaterialDemo: Creating selection outline demo");

    // Selected style (golden yellow)
    {
        float x = originX - 3.0f;
        float z = originZ + demoRowZ;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.cubeMesh,
            crateId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        world_->add<SelectionOutline>(entity, SelectionOutline::selected());
        world_->add<DebugName>(entity, "Selection_Selected_Demo");

        demoEntities_.push_back(entity);
        selectedEntities_.push_back(entity);
    }

    // Hovered style (light blue)
    {
        float x = originX;
        float z = originZ + demoRowZ;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.sphereMesh,
            metalId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        world_->add<SelectionOutline>(entity, SelectionOutline::hovered());
        world_->add<DebugName>(entity, "Selection_Hovered_Demo");

        demoEntities_.push_back(entity);
        selectedEntities_.push_back(entity);
    }

    // Error style (red, pulsing)
    {
        float x = originX + 3.0f;
        float z = originZ + demoRowZ;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.cubeMesh,
            crateId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        world_->add<SelectionOutline>(entity, SelectionOutline::error());
        world_->add<DebugName>(entity, "Selection_Error_Demo");

        demoEntities_.push_back(entity);
        selectedEntities_.push_back(entity);
    }

    // Custom style (green, thick)
    {
        float x = originX - 1.5f;
        float z = originZ + demoRowZ - 2.5f;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.sphereMesh,
            metalId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        SelectionOutline customOutline(glm::vec3(0.0f, 1.0f, 0.2f), 3.5f, 0.0f);
        world_->add<SelectionOutline>(entity, customOutline);
        world_->add<DebugName>(entity, "Selection_Custom_Demo");

        demoEntities_.push_back(entity);
        selectedEntities_.push_back(entity);
    }

    // Pulsing style (cyan, slow pulse)
    {
        float x = originX + 1.5f;
        float z = originZ + demoRowZ - 2.5f;
        float y = getTerrainHeight(x, z) + 0.5f;

        Entity entity = factory.createStaticMesh(
            info.cubeMesh,
            metalId,
            glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)),
            true
        );

        SelectionOutline pulsingOutline(glm::vec3(0.0f, 0.8f, 1.0f), 2.5f, 1.0f);
        world_->add<SelectionOutline>(entity, pulsingOutline);
        world_->add<DebugName>(entity, "Selection_Pulsing_Demo");

        demoEntities_.push_back(entity);
        selectedEntities_.push_back(entity);
    }

    SDL_Log("ECSMaterialDemo: Created %zu selection demo entities", selectedEntities_.size());
}

void ECSMaterialDemo::update(float deltaTime, float totalTime) {
    if (!initialized_ || !world_) return;

    // -------------------------------------------------------------------------
    // Update wetness cycle (sine wave 0-1 over 4 seconds)
    // -------------------------------------------------------------------------
    wetnessCycleTime_ += deltaTime;
    float wetnessPhase = std::sin(wetnessCycleTime_ * 1.57f) * 0.5f + 0.5f;  // π/2 rad/s = 4s cycle

    for (Entity entity : wetEntities_) {
        if (world_->has<WetnessOverlay>(entity)) {
            auto& overlay = world_->get<WetnessOverlay>(entity);
            // Each entity has a phase offset based on its index
            size_t idx = &entity - wetEntities_.data();
            float offset = static_cast<float>(idx) * 0.4f;
            float wetness = std::fmod(wetnessPhase + offset, 1.0f);
            overlay.wetness = wetness;
        }
    }

    // -------------------------------------------------------------------------
    // Update damage cycle (slower, 8 second cycle)
    // -------------------------------------------------------------------------
    damageCycleTime_ += deltaTime;
    float damagePhase = std::sin(damageCycleTime_ * 0.785f) * 0.5f + 0.5f;  // π/4 rad/s = 8s cycle

    for (Entity entity : damagedEntities_) {
        if (world_->has<DamageOverlay>(entity)) {
            auto& overlay = world_->get<DamageOverlay>(entity);
            size_t idx = &entity - damagedEntities_.data();
            float offset = static_cast<float>(idx) * 0.3f;
            float damage = std::fmod(damagePhase + offset, 1.0f);
            overlay.damage = damage;
        }
    }
}

void ECSMaterialDemo::toggleSelection() {
    selectionActive_ = !selectionActive_;

    for (Entity entity : selectedEntities_) {
        if (selectionActive_) {
            // Re-add selection outline if removed
            if (!world_->has<SelectionOutline>(entity)) {
                world_->add<SelectionOutline>(entity, SelectionOutline::selected());
            }
        } else {
            // Remove selection outline
            if (world_->has<SelectionOutline>(entity)) {
                world_->registry().remove<SelectionOutline>(entity);
            }
        }
    }

    SDL_Log("ECSMaterialDemo: Selection %s", selectionActive_ ? "enabled" : "disabled");
}

// =============================================================================
// Helper Functions
// =============================================================================

std::vector<OverlayRenderData> gatherOverlayEntities(const World& world) {
    std::vector<OverlayRenderData> result;

    // Query entities with either overlay
    auto wetView = world.registry().view<const WetnessOverlay>();
    auto damageView = world.registry().view<const DamageOverlay>();

    // Collect all entities with overlays
    std::unordered_set<Entity> processedEntities;

    for (auto entity : wetView) {
        OverlayRenderData data;
        data.entity = entity;
        data.wetness = wetView.get<const WetnessOverlay>(entity).wetness;
        data.damage = 0.0f;

        // Check for damage overlay too
        if (world.registry().all_of<DamageOverlay>(entity)) {
            data.damage = world.registry().get<DamageOverlay>(entity).damage;
        }

        if (data.wetness > 0.001f || data.damage > 0.001f) {
            result.push_back(data);
            processedEntities.insert(entity);
        }
    }

    // Add damage-only entities
    for (auto entity : damageView) {
        if (processedEntities.count(entity)) continue;

        OverlayRenderData data;
        data.entity = entity;
        data.wetness = 0.0f;
        data.damage = damageView.get<const DamageOverlay>(entity).damage;

        if (data.damage > 0.001f) {
            result.push_back(data);
        }
    }

    return result;
}

std::vector<SelectionRenderData> gatherSelectedEntities(const World& world) {
    std::vector<SelectionRenderData> result;

    auto view = world.registry().view<const SelectionOutline>();
    for (auto entity : view) {
        const auto& outline = view.get<const SelectionOutline>(entity);

        SelectionRenderData data;
        data.entity = entity;
        data.color = outline.color;
        data.thickness = outline.thickness;
        data.pulseSpeed = outline.pulseSpeed;

        result.push_back(data);
    }

    return result;
}

} // namespace ecs
