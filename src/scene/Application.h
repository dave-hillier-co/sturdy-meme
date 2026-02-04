#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <optional>
#include <memory>
#include "Renderer.h"
#include "Camera.h"
#include "PlayerState.h"
#include "PhysicsSystem.h"
#include "PhysicsTerrainTileManager.h"
#include "ClothSimulation.h"
#include "GuiSystem.h"
#include "InputSystem.h"
#include "BreadcrumbTracker.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"
#include "ecs/EntityFactory.h"
#include "ecs/ECSMaterialDemo.h"
#include "kitchen/KitchenSystem.h"
#include "gui/GuiKitchenTab.h"

// Kitchen control adapter for GUI
class KitchenControlAdapter : public IKitchenControl {
public:
    KitchenControlAdapter(kitchen::KitchenSystem& ks, ecs::World& w)
        : kitchenSystem_(ks), world_(w) {}

    kitchen::KitchenSystem& getKitchenSystem() override { return kitchenSystem_; }
    ecs::World& getECSWorld() override { return world_; }

    bool isKitchenSimulationEnabled() const override { return enabled_; }
    void setKitchenSimulationEnabled(bool enabled) override { enabled_ = enabled; }
    float getOrderSpawnRate() const override { return spawnRate_; }
    void setOrderSpawnRate(float rate) override { spawnRate_ = rate; }

private:
    kitchen::KitchenSystem& kitchenSystem_;
    ecs::World& world_;
    bool enabled_ = true;
    float spawnRate_ = 10.0f;
};

class Application {
public:
    Application() = default;
    ~Application() = default;

    bool init(const std::string& title, int width, int height);
    void run();
    void shutdown();

    // Access renderer for command line toggle configuration
    Renderer& getRenderer() { return *renderer_; }

private:
    void processEvents();
    void applyInputToCamera();
    std::string getResourcePath();
    void initFlag();
    void updateFlag(float deltaTime);
    void updateCameraOcclusion(float deltaTime);
    void initECS();
    void updateECS(float deltaTime);

    SDL_Window* window = nullptr;
    std::unique_ptr<Renderer> renderer_;
    Camera camera;
    PlayerState player_;  // Player state (transform, movement, grounded)
    std::optional<PhysicsWorld> physics_;
    PhysicsTerrainTileManager physicsTerrainManager_;

    // Helper to access physics (assumes physics is initialized)
    PhysicsWorld& physics() { return *physics_; }

    // Input system
    InputSystem input;

    // Breadcrumb tracker for fast respawn (Ghost of Tsushima optimization)
    // Tracks safe player positions so respawns load most content from cache
    BreadcrumbTracker breadcrumbTracker;

    // Flag simulation
    ClothSimulation clothSim;
    size_t flagClothSceneIndex = 0;
    size_t flagPoleSceneIndex = 0;

    // GUI system (created via factory)
    std::unique_ptr<GuiSystem> gui_;
    float currentFps = 60.0f;
    float lastDeltaTime = 0.016f;

    // Camera occlusion tracking
    std::unordered_set<PhysicsBodyID> occludingBodies;
    static constexpr float occlusionFadeSpeed = 8.0f;
    static constexpr float occludedOpacity = 0.3f;

    // ECS world and entity tracking (entities now stored in SceneBuilder)
    ecs::World ecsWorld_;
    bool ecsWeaponsInitialized_ = false;      // Track if weapon bone attachments are set up
    std::unique_ptr<ecs::ECSMaterialDemo> ecsMaterialDemo_;  // ECS material demo entities

    // Kitchen order system
    kitchen::KitchenSystem kitchenSystem_;
    std::unique_ptr<KitchenControlAdapter> kitchenControl_;
    KitchenTabState kitchenTabState_;

    bool running = false;
    // Walk speed matches animation root motion: 158.42 cm / 1.10s * 0.01 scale = 1.44 m/s
    float moveSpeed = 1.44f;
    // Run speed matches animation root motion: 278.32 cm / 0.70s * 0.01 scale = 3.98 m/s
    float sprintSpeed = 3.98f;
};
