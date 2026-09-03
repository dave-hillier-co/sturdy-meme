#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>
#include <future>
#include "Renderer.h"
#include "Camera.h"
#include "PlayerState.h"
#include "PhysicsSystem.h"
#include "ArticulatedBody.h"
#include "PhysicsTerrainTileManager.h"
#include "ClothSimulation.h"
#include "GuiSystem.h"
#include "InputSystem.h"
#include "BreadcrumbTracker.h"
#include "gui/GuiDebugTab.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"
#include "ecs/EntityFactory.h"
#include "ecs/ECSMaterialDemo.h"
#include "ml/unicon/Controller.h"
#include "ml/unicon/RagdollRenderer.h"
#include "world/SettlementBlockoutGenerator.h"
#include "world/RibbonMeshGenerator.h"
#include "world/TownLinearFeatures.h"
#include "world/BridgeDeckGenerator.h"
#include "terrain/RoadNetworkLoader.h"
#include "controls/DebugCommands.h"

class SceneBuilder;

class Application {
public:
    Application() = default;
    ~Application() { shutdown(); }  // shutdown() is idempotent and safe after partial init

    bool init(const std::string& title, int width, int height);
    void run();
    void shutdown();

    // Access renderer for command line toggle configuration
    Renderer& getRenderer() { return *renderer_; }

private:
    // Heavy world setup after renderer init (terrain preloads, physics tiles,
    // colliders, ECS, deferred content kick-off). Runs on a worker thread
    // while the loading screen presents; publishProgress reports [0,1] bar
    // progress and must be thread-safe.
    bool setupWorld(std::future<std::optional<PhysicsWorld>> physicsFuture,
                    const std::function<void(float)>& publishProgress);

    void processEvents();
    void applyInputToCamera();
    std::string getResourcePath();
    void initFlag();
    void updateFlag(float deltaTime);
    void updateCameraOcclusion(float deltaTime);
    void initECS();
    void updateECS(float deltaTime);
    void spawnRagdoll();
    void buildDebugCommands();
    void runTerrainHeightDiagnostic();
    void teleportTo(float worldX, float worldZ);
    void stepSettlementGeneration();
    void stepWorldFeatureGeneration();
    void queueLinearWorldFeatures(SceneBuilder& sceneBuilder);

    SDL_Window* window = nullptr;
    std::unique_ptr<Renderer> renderer_;
    Camera camera;
    PlayerState player_;  // Player state (transform, movement, grounded)
    std::optional<PhysicsWorld> physics_;
    // Declared after physics_ so it is destroyed first (removes its bodies).
    // Null when terrain streaming is unavailable.
    std::unique_ptr<PhysicsTerrainTileManager> physicsTerrainManager_;

    // Helper to access physics (assumes physics is initialized)
    PhysicsWorld& physics() { return *physics_; }

    // Settlement buildings generate one settlement per frame (nearest first)
    // once deferred terrain generation completes, so the startup cost is
    // amortized instead of stalling a single frame.
    std::unique_ptr<SettlementBlockoutGenerator> settlementGen_;
    std::vector<Settlement> settlementQueue_;
    size_t settlementQueueNext_ = 0;

    // Linear world features generated alongside settlements: draped road and
    // river ribbons plus per-town street ribbons and wall runs.
    std::unique_ptr<RibbonMeshGenerator> ribbonGen_;
    std::unique_ptr<TownLinearFeatures> townFeatures_;

    // Bridges and road/river ribbons drape over terrain (forcing tile loads),
    // so they generate one item per frame after settlements finish instead of
    // all at once in the completion callback.
    std::unique_ptr<BridgeDeckGenerator> bridgeGen_;
    std::vector<WaterCrossing> bridgeQueue_;
    size_t bridgeQueueNext_ = 0;
    std::vector<RibbonMeshGenerator::Ribbon> ribbonQueue_;
    std::vector<RibbonMeshGenerator::SkipZone> ribbonSkipZones_;
    size_t ribbonQueueNext_ = 0;

    // Input system
    InputSystem input;

    // Debug keybinding command table (built once at init, dispatched from
    // processEvents, rendered as the GUI cheatsheet)
    std::vector<DebugCommand> debugCommands_;

    // Breadcrumb tracker for fast respawn (Ghost of Tsushima optimization)
    // Tracks safe player positions so respawns load most content from cache
    BreadcrumbTracker breadcrumbTracker;

    // Teleport destinations for the debug World section (built from settlements)
    std::vector<GuiDebugTab::TeleportTarget> teleportTargets_;

    // Flag simulation
    ClothSimulation clothSim;
    size_t flagClothSceneIndex = 0;
    size_t flagPoleSceneIndex = 0;

    // GUI system (created via factory)
    std::unique_ptr<GuiSystem> gui_;
    float currentFps = 60.0f;
    float lastDeltaTime = 0.016f;

    // Camera occlusion parameters (tracking via OccludingCamera ECS tag)
    static constexpr float occlusionFadeSpeed = 8.0f;
    static constexpr float occludedOpacity = 0.3f;

    // ECS world and entity tracking (entities now stored in SceneBuilder)
    ecs::World ecsWorld_;
    bool ecsWeaponsInitialized_ = false;      // Track if weapon bone attachments are set up
    std::unique_ptr<ecs::ECSMaterialDemo> ecsMaterialDemo_;  // ECS material demo entities

    // Ragdoll test instances
    std::vector<ArticulatedBody> ragdolls_;

    // UniCon ML policy controller for ragdolls
    ml::unicon::Controller uniconController_;

    // Ragdoll renderer (shares player mesh, uses physics bone matrices)
    ml::unicon::RagdollRenderer ragdollRenderer_;

    bool running = false;
    // Walk speed matches animation root motion: 158.42 cm / 1.10s * 0.01 scale = 1.44 m/s
    float moveSpeed = 1.44f;
    // Run speed matches animation root motion: 278.32 cm / 0.70s * 0.01 scale = 3.98 m/s
    float sprintSpeed = 3.98f;
};
