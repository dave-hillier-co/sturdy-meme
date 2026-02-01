#pragma once

#include "World.h"
#include "Components.h"
#include "EntityFactory.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>

class Mesh;
class Texture;
class MaterialRegistry;

namespace ecs {

// =============================================================================
// ECS Material Demo System
// =============================================================================
// Demonstrates ECS material component integration by creating demo entities
// that showcase various material features:
//   - PBRProperties: Custom roughness, metallic, emissive values
//   - TreeData: Tree-specific rendering with withTreeData() builder
//   - WetnessOverlay: Dynamic wetness effects
//   - DamageOverlay: Dynamic damage effects
//   - SelectionOutline: Entity highlighting
//
// This system creates entities during initialization and updates dynamic
// effects (wetness cycling, damage progression, selection toggling) each frame.

class ECSMaterialDemo {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit ECSMaterialDemo(ConstructToken) {}

    // Function type for querying terrain height at world position (x, z)
    using HeightQueryFunc = std::function<float(float, float)>;

    struct InitInfo {
        World* world = nullptr;
        Mesh* cubeMesh = nullptr;
        Mesh* sphereMesh = nullptr;
        Texture* metalTexture = nullptr;
        Texture* crateTexture = nullptr;
        MaterialRegistry* materialRegistry = nullptr;
        HeightQueryFunc getTerrainHeight;
        glm::vec2 sceneOrigin = glm::vec2(0.0f);
    };

    static std::unique_ptr<ECSMaterialDemo> create(const InitInfo& info);
    ~ECSMaterialDemo() = default;

    // Non-copyable, non-movable
    ECSMaterialDemo(const ECSMaterialDemo&) = delete;
    ECSMaterialDemo& operator=(const ECSMaterialDemo&) = delete;
    ECSMaterialDemo(ECSMaterialDemo&&) = delete;
    ECSMaterialDemo& operator=(ECSMaterialDemo&&) = delete;

    // Update dynamic effects (call each frame)
    // deltaTime: Time since last frame in seconds
    // totalTime: Total elapsed time in seconds
    void update(float deltaTime, float totalTime);

    // Toggle selection on demo entities
    void toggleSelection();

    // Get selected entities for rendering
    const std::vector<Entity>& getSelectedEntities() const { return selectedEntities_; }

    // Get all demo entities
    const std::vector<Entity>& getDemoEntities() const { return demoEntities_; }

    // Get entities with wetness overlay
    const std::vector<Entity>& getWetEntities() const { return wetEntities_; }

    // Get entities with damage overlay
    const std::vector<Entity>& getDamagedEntities() const { return damagedEntities_; }

    // Check if demo is initialized
    bool isInitialized() const { return initialized_; }

private:
    bool initInternal(const InitInfo& info);

    // Create demo entities demonstrating each feature
    void createPBRDemoEntities(const InitInfo& info);
    void createTreeDemoEntities(const InitInfo& info);
    void createOverlayDemoEntities(const InitInfo& info);
    void createSelectionDemoEntities(const InitInfo& info);

    // Get terrain height with fallback
    float getTerrainHeight(float x, float z) const;

    World* world_ = nullptr;
    HeightQueryFunc terrainHeightFunc_;
    glm::vec2 sceneOrigin_{0.0f};
    bool initialized_ = false;

    // Demo entity collections
    std::vector<Entity> demoEntities_;
    std::vector<Entity> pbrDemoEntities_;
    std::vector<Entity> treeDemoEntities_;
    std::vector<Entity> wetEntities_;
    std::vector<Entity> damagedEntities_;
    std::vector<Entity> selectedEntities_;

    // Animation state
    bool selectionActive_ = true;
    float wetnessCycleTime_ = 0.0f;
    float damageCycleTime_ = 0.0f;
};

// =============================================================================
// ECS Material Demo Renderer Integration
// =============================================================================
// Helper functions for integrating demo overlays with rendering pipeline

// Gather entities needing wetness/damage shader parameters
struct OverlayRenderData {
    Entity entity;
    float wetness;
    float damage;
};

// Collect overlay data for entities with active overlays
std::vector<OverlayRenderData> gatherOverlayEntities(const World& world);

// Gather entities with selection outlines for outline pass rendering
struct SelectionRenderData {
    Entity entity;
    glm::vec3 color;
    float thickness;
    float pulseSpeed;
};

std::vector<SelectionRenderData> gatherSelectedEntities(const World& world);

} // namespace ecs
