#include "Application.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_set>
#include "core/vulkan/VulkanContext.h"
#include "core/LoadingRenderer.h"
#include "loading/LoadJobQueue.h"
#include "loading/LoadJobFactory.h"
#include "core/threading/TaskScheduler.h"
#include "InitProfiler.h"
#include "Profiler.h"

#include "TerrainSystem.h"
#include "TerrainTileCache.h"
#include "ScatterSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeCollision.h"
#include "DeferredTerrainObjects.h"
#include "SceneManager.h"
#include "WaterSystem.h"
#include "WindSystem.h"
#include "core/RendererSystems.h"
#include "world/SettlementRegistry.h"
#include "world/SettlementBlockoutGenerator.h"
#include "world/BridgeDeckGenerator.h"
#include "world/GeneratedMeshUtil.h"
#include "world/WorldCoords.h"
#include "terrain/RoadNetworkLoader.h"
#include "terrain/ErosionDataLoader.h"
#include "world/KitBuildingAssembler.h"
#include "EnvironmentSettings.h"
#include "TimeSystem.h"
#include "core/interfaces/ITimeSystem.h"
#include "core/interfaces/IDebugControl.h"
#include "core/interfaces/IEnvironmentControl.h"
#include "core/interfaces/IWeatherState.h"
#include "core/interfaces/IPlayerControl.h"
#include "DebugLineSystem.h"
#include "npc/NPCSimulation.h"
#include "ml/unicon/Controller.h"
#include "Texture.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

bool Application::init(const std::string& title, int width, int height) {
    // Reset and start init profiler
    InitProfiler::get().reset();

    {
        INIT_PROFILE_PHASE("SDL");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
            return false;
        }
    }

    // Initialize task scheduler early for multi-threaded operations
    {
        INIT_PROFILE_PHASE("TaskScheduler");
        TaskScheduler::instance().initialize();
    }

    // Early Vulkan initialization: create instance BEFORE window
    // This allows validation layers and dispatcher to start earlier
    std::unique_ptr<VulkanContext> vulkanContext;
    {
        INIT_PROFILE_PHASE("VulkanInstance");
        vulkanContext = std::make_unique<VulkanContext>();
        if (!vulkanContext->initInstance()) {
            SDL_Log("Failed to initialize Vulkan instance (early init)");
            SDL_Quit();
            return false;
        }
    }

    // InputSystem initializes itself in constructor (RAII)

    {
        INIT_PROFILE_PHASE("Window");
        window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_Log("Failed to create window: %s", SDL_GetError());
            SDL_Quit();
            return false;
        }
    }

    // Load persistent player settings once, before the frame loop exists.
    // Sensitivity, invert-Y, and fullscreen apply immediately; quality-toggle
    // overrides apply later, once async renderer init has finished.
    {
        INIT_PROFILE_PHASE("GameSettings");
        settingsPath_ = GameSettings::defaultFilePath();
        settings_ = GameSettings::loadFromFile(settingsPath_);
        input.setMouseSensitivity(settings_.mouseSensitivity);
        input.setInvertMouseY(settings_.invertMouseY);
        if (settings_.fullscreen) {
            SDL_SetWindowFullscreen(window, true);
        }
    }

    std::string resourcePath = getResourcePath();

    // Complete Vulkan device initialization (surface, device, swapchain)
    // This must happen before LoadingRenderer can be created
    {
        INIT_PROFILE_PHASE("VulkanDevice");
        if (!vulkanContext->initDevice(window)) {
            SDL_Log("Failed to initialize Vulkan device");
            SDL_DestroyWindow(window);
            SDL_Quit();
            return false;
        }
    }

    // Create loading screen renderer - kept alive during full renderer initialization
    std::unique_ptr<LoadingRenderer> loadingRenderer;
    {
        INIT_PROFILE_PHASE("LoadingScreen");
        LoadingRenderer::InitInfo loadingInfo{};
        loadingInfo.vulkanContext = vulkanContext.get();
        loadingInfo.shaderPath = resourcePath + "/shaders";

        loadingRenderer = LoadingRenderer::create(loadingInfo);
        if (loadingRenderer) {
            // Show initial loading screen while we start initialization
            loadingRenderer->setProgress(0.0f);
            loadingRenderer->render();
            SDL_PumpEvents();
        } else {
            SDL_Log("Warning: LoadingRenderer creation failed, initialization will proceed without visual feedback");
        }
    }
    // Presentation exists from here on; main-thread budget warnings arm now
    // (creating the loading renderer itself cannot stall a frame - there was
    // nothing presenting yet).
    if (loadingRenderer) {
        InitProfiler::get().setPresentingActive(true);
    }

    // Create full renderer with progress callback to update loading screen
    // The loading screen stays visible and animated during subsystem initialization
    Renderer::InitInfo rendererInfo{};
    rendererInfo.window = window;
    rendererInfo.resourcePath = resourcePath;
    rendererInfo.vulkanContext = std::move(vulkanContext);  // Transfer ownership
    rendererInfo.asyncInit = true;  // Enable async subsystem loading

    // Progress callbacks only publish state; they may be invoked from worker
    // threads and must never render or pump events (see CLAUDE.md: Threading
    // and Loading Design Principles). The loading loop below owns the frame
    // cadence and reads this state each frame.
    struct ProgressState {
        std::mutex mutex;
        float progress = 0.0f;
    };
    auto progressState = std::make_shared<ProgressState>();
    rendererInfo.progressCallback = [progressState](float progress, const char* phase) {
        std::lock_guard<std::mutex> lock(progressState->mutex);
        // Async subsystem init owns the first 75% of the bar; the world setup
        // worker below fills the remainder.
        progressState->progress = std::max(progressState->progress, progress * 0.75f);
    };

    // Physics world creation is CPU-only and renderer-independent; build it
    // concurrently with renderer initialization so it does not delay the
    // first frame after loading completes.
    auto physicsFuture = std::async(std::launch::async, [] { return PhysicsWorld::create(); });

    renderer_ = Renderer::create(rendererInfo);

    // If async init is enabled, poll for completion while rendering loading screen
    if (renderer_ && !renderer_->isAsyncInitComplete()) {
        SDL_Log("Async initialization started, running loading loop...");

        while (!renderer_->pollAsyncInit()) {
            // Render loading screen; present pacing (vsync) throttles the loop
            if (loadingRenderer) {
                {
                    std::lock_guard<std::mutex> lock(progressState->mutex);
                    loadingRenderer->setProgress(progressState->progress);
                }
                loadingRenderer->render();
            }

            // Keep window responsive
            SDL_PumpEvents();

            // Check for quit events during loading
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    SDL_Log("Quit requested during loading");
                    if (loadingRenderer) {
                        loadingRenderer->cleanup();
                    }
                    return false;
                }
            }
        }

        SDL_Log("Async initialization complete");
    }

    if (!renderer_) {
        SDL_Log("Failed to initialize renderer");
        if (loadingRenderer) {
            loadingRenderer->cleanup();
            loadingRenderer.reset();
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    camera.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));

    // World setup (terrain preloads, physics terrain tiles, colliders, ECS,
    // deferred content kick-off) runs on a worker thread while the loading
    // screen keeps presenting, so the window never freezes between "renderer
    // ready" and the first main-loop frame. GPU uploads inside go through
    // CommandScope, which takes GraphicsQueueLock, so they safely overlap the
    // loading renderer's queue submits.
    auto publishProgress = [progressState](float p) {
        std::lock_guard<std::mutex> lock(progressState->mutex);
        progressState->progress = std::max(progressState->progress, p);
    };

    auto worldSetupFuture = std::async(std::launch::async,
        [this, publishProgress, physicsFuture = std::move(physicsFuture)]() mutable {
            return setupWorld(std::move(physicsFuture), publishProgress);
        });

    // Keep presenting the loading screen while world setup runs on the worker
    bool quitDuringSetup = false;
    while (worldSetupFuture.wait_for(std::chrono::milliseconds(
               loadingRenderer ? 0 : 50)) != std::future_status::ready) {
        if (loadingRenderer) {
            {
                std::lock_guard<std::mutex> lock(progressState->mutex);
                loadingRenderer->setProgress(progressState->progress);
            }
            loadingRenderer->render();
        }
        SDL_PumpEvents();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                SDL_Log("Quit requested during world setup");
                // The worker cannot be cancelled; note the request and exit
                // once it completes.
                quitDuringSetup = true;
            }
        }
    }
    const bool worldSetupOk = worldSetupFuture.get();

    // Cleanup loading renderer now that world setup is done. Between here and
    // the first main-loop frame nothing presents, so main-thread budget
    // warnings pause until the render loop takes over.
    InitProfiler::get().setPresentingActive(false);
    if (loadingRenderer) {
        loadingRenderer->cleanup();
        loadingRenderer.reset();
    }

    if (quitDuringSetup || !worldSetupOk) {
        return false;
    }

    // Initialize GUI system via factory. Panels bind their dependencies from
    // RendererSystems at construction; Application-level debug actions and the
    // debug command cheatsheet (stable member addresses, populated later) are
    // injected here.
    {
        INIT_PROFILE_PHASE("GUI");
        GuiDebugTab::Hooks debugHooks;
        debugHooks.spawnRagdoll = [this]() { spawnRagdoll(); };
        debugHooks.ragdollCount = [this]() -> int { return static_cast<int>(ragdolls_.size()); };
        debugHooks.teleport = [this](float x, float z) { teleportTo(x, z); };
        debugHooks.teleportTargets =
            [this]() -> const std::vector<GuiDebugTab::TeleportTarget>& { return teleportTargets_; };

        const VulkanContext& vkCtx = renderer_->getVulkanContext();
        gui_ = GuiSystem::create(window, vkCtx.getVkInstance(), vkCtx.getVkPhysicalDevice(),
                                  vkCtx.getVkDevice(), vkCtx.getGraphicsQueueFamily(),
                                  vkCtx.getVkGraphicsQueue(), vkCtx.getRenderPass(),
                                  vkCtx.getSwapchainImageCount(),
                                  renderer_->getSystems(), std::move(debugHooks),
                                  &physicsTerrainManager_, &debugCommands_);
        if (!gui_) {
            SDL_Log("Failed to initialize GUI system");
            return false;
        }
    }

    // Set GUI render callback
    renderer_->setGuiRenderCallback([this](vk::CommandBuffer cmd) {
        gui_->endFrame(cmd);
    });

    // Wire the player-facing pause menu. Performance toggles are looked up
    // per frame (systems come up asynchronously and may rebind).
    {
        GameMenu::Hooks menuHooks;
        menuHooks.input = &input;
        menuHooks.window = window;
        menuHooks.settings = &settings_;
        menuHooks.settingsChanged = [this] { settings_.saveToFile(settingsPath_); };
        menuHooks.performanceToggles = [this]() -> PerformanceToggles* {
            return renderer_ ? &renderer_->getSystems().performanceToggles() : nullptr;
        };
        menuHooks.requestQuit = [this] { running = false; };
        gui_->gameMenu().setHooks(std::move(menuHooks));
    }

    // Apply persisted quality-toggle overrides now that async init has
    // completed and PerformanceToggles is wired (it does not exist at the
    // point settings are loaded). Unknown names are ignored with a warning
    // so renamed toggles never crash a stale settings file.
    {
        PerformanceToggles& perfToggles = renderer_->getSystems().performanceToggles();
        auto toggles = perfToggles.getAllToggles();
        for (const auto& [name, enabled] : settings_.qualityOverrides) {
            auto it = std::find_if(toggles.begin(), toggles.end(),
                                   [&name](const PerformanceToggles::Toggle& t) {
                                       return t.name == name;
                                   });
            if (it != toggles.end()) {
                *it->value = enabled;
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "GameSettings: unknown quality toggle '%s' ignored",
                            name.c_str());
            }
        }
    }

    // Collect settlements as teleport destinations for the debug panel
    for (const auto& settlement : renderer_->getSystems().settlements().settlements()) {
        GuiDebugTab::TeleportTarget target;
        target.name = settlement.displayName();
        target.worldX = settlement.worldPos.x;
        target.worldZ = settlement.worldPos.y;
        target.radius = settlement.radius;
        teleportTargets_.push_back(std::move(target));
    }

    // Configure ragdoll renderer if player character is available
    {
        auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
        if (sceneBuilder.hasCharacter()) {
            ragdollRenderer_.configure(sceneBuilder.getAnimatedCharacter().getSkeleton());
        }
    }

    // Set ragdoll draw callback for rendering physics-driven ragdolls
    renderer_->setRagdollDrawCallback([this](vk::CommandBuffer cmd, uint32_t frameIndex) {
        if (!ragdollRenderer_.isConfigured() || ragdolls_.empty() || !physics_) return;
        auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
        if (!sceneBuilder.hasCharacter()) return;

        // Upload ragdoll bone matrices (done here because we need the frame index)
        ragdollRenderer_.updateBoneMatrices(ragdolls_, physics(),
                                             renderer_->getSystems().skinnedMesh(),
                                             frameIndex);

        // Record draw commands using player character's mesh
        ragdollRenderer_.recordDrawCommands(cmd, frameIndex,
                                             sceneBuilder.getAnimatedCharacter(),
                                             renderer_->getSystems().skinnedMesh());
    });

    // Set up input system with GUI reference for input blocking
    input.setGuiSystem(gui_.get());
    input.setMoveSpeed(moveSpeed);

    // Finalize init profiler and log results
    InitProfiler::get().finalize();

    // Capture init timing to flamegraph
    renderer_->getSystems().profiler().captureInitFlamegraph();

    running = true;
    return true;
}

bool Application::setupWorld(std::future<std::optional<PhysicsWorld>> physicsFuture,
                             const std::function<void(float)>& publishProgress) {

    // Position camera at a settlement (Town 1: market town with coastal/agricultural features)
    // Settlement coords are 0-16384, world coords are centered (-8192 to +8192)
    {
        const float settlementX = 11000.0f;  // Town 1 in 0-16384 space
        const float settlementZ = 5200.0f;
        const float halfTerrain = 8192.0f;
        float cameraX = settlementX - halfTerrain;
        float cameraZ = settlementZ - halfTerrain;
        float terrainY = 50.0f;  // Default height if terrain unavailable
        if (auto* terrainPtr = renderer_->getSystems().terrainPtr()) {
            // Pre-load tiles before querying height (tiles only preloaded around origin by default)
            if (auto* tileCachePtr = terrainPtr->getTileCache()) {
                tileCachePtr->preloadTilesAround(cameraX, cameraZ, 600.0f);
            }
            terrainY = terrainPtr->getHeightAt(cameraX, cameraZ);
        }
        camera.setPosition(glm::vec3(cameraX, terrainY + 2.0f, cameraZ));  // Eye level above ground
        camera.setYaw(45.0f);    // Look roughly northeast
        camera.setPitch(0.0f);   // Level view
    }
    publishProgress(0.80f);

    if (!renderer_->getSystems().hasTerrain()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Terrain system failed to initialize");
        return false;
    }

    // Adopt the physics world built concurrently with renderer init
    {
        INIT_PROFILE_PHASE("Physics");
        physics_ = physicsFuture.get();
        if (!physics_) {
            SDL_Log("Failed to initialize physics system");
            return false;
        }

        // Initialize UniCon ML controller for ragdoll physics
        uniconController_.init(20, 1); // 20-body humanoid, tau=1
        if (!uniconController_.loadPolicy("generated/unicon/policy_weights.bin")) {
            SDL_Log("No trained UniCon weights found, using random policy");
            uniconController_.initRandomPolicy();
        }
    }
    publishProgress(0.82f);

    // Create terrain hole at well entrance location
    // This must be done before terrain physics is initialized
    if (auto* terrainSys = renderer_->getSystems().terrainPtr()) {
        if (renderer_->getSystems().scenePtr() && renderer_->getSystems().scene().hasSceneBuilder()) {
            const auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
            float wellX = sceneBuilder.getWellEntranceX();
            float wellZ = sceneBuilder.getWellEntranceZ();
            terrainSys->addHoleCircle(wellX, wellZ, SceneBuilder::WELL_HOLE_RADIUS);
            terrainSys->uploadHoleMaskToGPU();
            SDL_Log("Created terrain hole at well entrance (%.1f, %.1f) radius %.1f",
                    wellX, wellZ, SceneBuilder::WELL_HOLE_RADIUS);
        }
    }

    // Get terrain pointer for spawning objects (may be null if preprocessing was skipped)
    auto* terrain = renderer_->getSystems().terrainPtr();

    // Initialize tiled physics terrain manager
    // Uses high-resolution terrain tiles (~1m spacing) within 1000m of player
    // instead of a single coarse heightfield (~32m spacing)
    if (terrain) {
        TerrainTileCache* tileCache = terrain->getTileCache();
        if (tileCache) {
            PhysicsTerrainTileManager::Config config;
            config.loadRadius = 1000.0f;
            config.unloadRadius = 1200.0f;
            config.maxTilesPerFrame = 2;
            config.terrainSize = terrain->getConfig().size;
            config.heightScale = terrain->getConfig().heightScale;

            if (physicsTerrainManager_.init(physics(), *tileCache, config)) {
                SDL_Log("Physics terrain tile manager initialized");

                // Pre-load physics terrain tiles around scene origin (where objects are placed)
                // Scene is at Town 1: settlement coords (11000, 5200) -> world coords (2808, -2992)
                const float halfTerrain = 8192.0f;
                glm::vec3 sceneSpawnPos(11000.0f - halfTerrain, 0.0f, 5200.0f - halfTerrain);
                for (int i = 0; i < 50; i++) {  // Load up to 50 tiles synchronously
                    physicsTerrainManager_.update(sceneSpawnPos);
                }
                SDL_Log("Pre-loaded physics terrain tiles around scene origin (%.0f, %.0f)",
                        sceneSpawnPos.x, sceneSpawnPos.z);
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize physics terrain tile manager!");
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Terrain tile cache not available for physics!");
        }
    }
    publishProgress(0.87f);

    // Initialize scene physics (dynamic objects)
    if (renderer_->getSystems().scenePtr()) {
        renderer_->getSystems().scene().initPhysics(physics());
    }

    // Note: Rock and detritus physics colliders are created after ECS instance entities
    // are populated (see createScatterPhysicsFromECS below)

    // Create compound capsule colliders for trees (trunk + major branches)
    // Track how many trees currently have colliders for deferred generation
    size_t treesWithColliders = 0;
    if (TreeSystem* treeSystem = renderer_->getSystems().tree()) {
        const auto& treeInstances = treeSystem->getTreeInstances();
        TreeCollision::Config treeCollisionConfig;
        treeCollisionConfig.maxBranchLevel = 2;  // Trunk + first 2 levels of branches
        treeCollisionConfig.minBranchRadius = 0.05f;

        for (size_t i = 0; i < treeInstances.size(); ++i) {
            const auto& tree = treeInstances[i];
            auto capsules = treeSystem->getTreeCollisionCapsules(static_cast<uint32_t>(i), treeCollisionConfig);

            if (!capsules.empty()) {
                physics().createStaticCompoundCapsules(tree.position(), capsules, tree.rotation());
            }
        }
        treesWithColliders = treeInstances.size();
        SDL_Log("Created %zu tree compound capsule colliders", treeInstances.size());
    }
    publishProgress(0.89f);

    // Register callback for deferred tree generation (forest/woods trees)
    // These trees are generated after terrain is ready, so we need to create colliders then
    if (auto* deferred = renderer_->getSystems().deferredTerrainObjects()) {
        deferred->setOnTreesGeneratedCallback([this, treesWithColliders](TreeSystem& treeSystem) {
            const auto& treeInstances = treeSystem.getTreeInstances();

            // Only create colliders for trees added after initial setup
            if (treeInstances.size() <= treesWithColliders) {
                return;  // No new trees to process
            }

            TreeCollision::Config treeCollisionConfig;
            treeCollisionConfig.maxBranchLevel = 2;
            treeCollisionConfig.minBranchRadius = 0.05f;

            size_t newColliders = 0;
            for (size_t i = treesWithColliders; i < treeInstances.size(); ++i) {
                const auto& tree = treeInstances[i];
                auto capsules = treeSystem.getTreeCollisionCapsules(static_cast<uint32_t>(i), treeCollisionConfig);

                if (!capsules.empty()) {
                    physics().createStaticCompoundCapsules(tree.position(), capsules, tree.rotation());
                    ++newColliders;
                }
            }
            SDL_Log("Created %zu tree colliders for deferred forest generation", newColliders);
        });

        // Once terrain-dependent generation completes, place settlement blockout
        // buildings (heights are reliable at that point)
        deferred->setOnGeneratedCallback([this]() {
            auto& systems = renderer_->getSystems();
            ecs::World* world = systems.ecsWorld();
            auto* terrainSys = systems.terrainPtr();
            if (!world || !terrainSys || !systems.scenePtr()) return;
            auto& sceneBuilder = systems.scene().getSceneBuilder();
            if (!sceneBuilder.getCubeMesh()) return;

            SettlementBlockoutGenerator::Config cfg;
            cfg.getTerrainHeight = [terrainSys](float x, float z) {
                return terrainSys->getHeightAt(x, z);
            };
            cfg.preloadTiles = [terrainSys](float x, float z, float radius) {
                if (auto* cache = terrainSys->getTileCache()) {
                    cache->preloadTilesAround(x, z, radius);
                }
            };
            cfg.buildingMesh = sceneBuilder.getCubeMesh();
            cfg.materialId = sceneBuilder.getWhiteMaterialId();
            // Buildings assemble from modular kit wall pieces; plots the kit
            // can't take (steep, over budget) fall back to extruded prisms in
            // matching kit materials: [0] foundation plinth, then floor bands.
            cfg.kitModelsDir = getResourcePath() + "/assets/models/buildings";
            cfg.kitMaterial = [sb = &sceneBuilder](const std::string& name) {
                return sb->getKitMaterialId(name);
            };
            cfg.layerMaterials = {
                sceneBuilder.getKitMaterialId("MI_RockTrim"),     // foundation
                sceneBuilder.getKitMaterialId("MI_Plaster"),      // floor 1
                sceneBuilder.getKitMaterialId("MI_UnevenBrick"),  // floor 2
            };
            // Each settlement's meshes upload as one batch (single staging
            // buffer + submit). Buildings never feed physics from mesh CPU
            // copies (colliders use separate prism buffers), so the batch
            // path releases CPU geometry after upload.
            cfg.createMeshes = [sb = &sceneBuilder](std::vector<MeshGeometry> batch) {
                return sb->addGeneratedMeshes(std::move(batch));
            };
            // Static box collider per building for the random-box fallback path
            cfg.addCollider = [this](const glm::vec3& center, const glm::vec3& halfExtents,
                                     float yaw) {
                physics().createStaticBox(center, halfExtents,
                                          glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
            };
            // One triangle-mesh collider per settlement matching the exact
            // footprint prisms (so L-shaped/concave plots aren't over-filled)
            cfg.addMeshCollider = [this](const std::vector<Vertex>& verts,
                                         const std::vector<uint32_t>& inds) {
                std::vector<glm::vec3> positions;
                positions.reserve(verts.size());
                for (const auto& v : verts) positions.push_back(v.position);
                physics().createStaticMesh(positions.data(), positions.size(),
                                           inds.data(), inds.size());
            };
            cfg.seaLevel = 23.0f;
            cfg.townsDir = getResourcePath() + "/terrain_data/towns";
            cfg.streetsDir = getResourcePath() + "/terrain_data/roads";
            // Keep clear of the hand-placed content at the scene origin: spawn,
            // well, crates, NPCs (within 5m) and the four demo trees (up to 58m
            // out). The empty core reads as a village green.
            cfg.exclusionCenter = glm::vec2(11000.0f - 8192.0f, 5200.0f - 8192.0f);
            cfg.exclusionRadius = 70.0f;

            // Queue settlements nearest-first from the player and generate
            // one per frame (stepSettlementGeneration) so the ~20-settlement
            // build cost never lands in a single frame.
            settlementQueue_ = systems.settlements().settlements();
            const glm::vec2 playerXZ(player_.transform.position.x,
                                     player_.transform.position.z);
            std::sort(settlementQueue_.begin(), settlementQueue_.end(),
                      [playerXZ](const Settlement& a, const Settlement& b) {
                float da = glm::distance(a.worldPos, playerXZ);
                float db = glm::distance(b.worldPos, playerXZ);
                if (da != db) return da < db;
                return a.id < b.id;
            });
            settlementQueueNext_ = 0;
            settlementGen_ = std::make_unique<SettlementBlockoutGenerator>(std::move(cfg));

            // Bridge decks at the road network's river crossings (fords are
            // baked into the terrain texture; bridges need walkable geometry).
            // Draping forces terrain tile loads, so decks generate one per
            // frame (stepWorldFeatureGeneration) instead of all at once here.
            const auto& crossings = systems.roadData().getRoadNetwork().crossings;
            if (!crossings.empty()) {
                BridgeDeckGenerator::Config bridgeCfg;
                bridgeCfg.getTerrainHeight = [terrainSys](float x, float z) {
                    return terrainSys->getHeightAt(x, z);
                };
                bridgeCfg.preloadTiles = [terrainSys](float x, float z, float radius) {
                    if (auto* cache = terrainSys->getTileCache()) {
                        cache->preloadTilesAround(x, z, radius);
                    }
                };
                bridgeCfg.createMeshes = [sb = &sceneBuilder](std::vector<MeshGeometry> batch) {
                    return sb->addGeneratedMeshes(std::move(batch));
                };
                bridgeCfg.addCollider = [this](const glm::vec3& center,
                                               const glm::vec3& halfExtents, float yaw) {
                    physics().createStaticBox(center, halfExtents,
                                              glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
                };
                bridgeCfg.deckMaterial = sceneBuilder.getKitMaterialId("MI_RockTrim");
                bridgeGen_ = std::make_unique<BridgeDeckGenerator>(std::move(bridgeCfg));
                bridgeQueue_ = crossings;
                bridgeQueueNext_ = 0;
            }

            // Shared ribbon generator for roads, rivers and town streets
            RibbonMeshGenerator::Config ribbonCfg;
            ribbonCfg.getTerrainHeight = [terrainSys](float x, float z) {
                return terrainSys->getHeightAt(x, z);
            };
            ribbonCfg.preloadTiles = [terrainSys](float x, float z, float radius) {
                if (auto* cache = terrainSys->getTileCache()) {
                    cache->preloadTilesAround(x, z, radius);
                }
            };
            ribbonCfg.createMeshes = [sb = &sceneBuilder](std::vector<MeshGeometry> batch) {
                return sb->addGeneratedMeshes(std::move(batch));
            };
            ribbonGen_ = std::make_unique<RibbonMeshGenerator>(std::move(ribbonCfg));

            queueLinearWorldFeatures(sceneBuilder);

            // Town street/wall geometry generates per settlement alongside the
            // buildings (stepSettlementGeneration).
            TownLinearFeatures::Config townCfg;
            townCfg.townsDir = getResourcePath() + "/terrain_data/towns";
            townCfg.streetsDir = getResourcePath() + "/terrain_data/roads";
            townCfg.streetMaterial = sceneBuilder.getKitMaterialId("MI_RockTrim");
            townCfg.wallMaterial = sceneBuilder.getKitMaterialId("MI_UnevenBrick");
            townCfg.getTerrainHeight = [terrainSys](float x, float z) {
                return terrainSys->getHeightAt(x, z);
            };
            townCfg.createMeshes = [sb = &sceneBuilder](std::vector<MeshGeometry> batch) {
                return sb->addGeneratedMeshes(std::move(batch));
            };
            townCfg.addCollider = [this](const glm::vec3& center,
                                         const glm::vec3& halfExtents, float yaw) {
                physics().createStaticBox(center, halfExtents,
                                          glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
            };
            townFeatures_ = std::make_unique<TownLinearFeatures>(std::move(townCfg));
        });
    }

    // Create player entity and character controller
    // Spawn at Town 1 settlement location (same as camera and scene origin)
    const float halfTerrain = 8192.0f;
    const float settlementX = 11000.0f;  // Town 1 in 0-16384 space
    const float settlementZ = 5200.0f;
    float playerSpawnX = settlementX - halfTerrain;
    float playerSpawnZ = settlementZ - halfTerrain;

    // Pre-load high-res tiles around spawn before querying height
    // This ensures we get LOD0 height data instead of low-res base LOD fallback
    float playerSpawnY = 0.1f;
    if (terrain) {
        if (auto* tileCachePtr = terrain->getTileCache()) {
            tileCachePtr->preloadTilesAround(playerSpawnX, playerSpawnZ, 600.0f);
        }

        playerSpawnY = terrain->getHeightAt(playerSpawnX, playerSpawnZ) + 0.1f;

        // Debug: Sample terrain height at spawn position using different methods
        float heightFromTerrainSystem = terrain->getHeightAt(playerSpawnX, playerSpawnZ);
        float heightFromTileCache = 0.0f;
        bool tileHasHeight = false;
        if (auto* tileCachePtr = terrain->getTileCache()) {
            tileHasHeight = tileCachePtr->getHeightAt(playerSpawnX, playerSpawnZ, heightFromTileCache);
        }
        SDL_Log("DEBUG Height at spawn (%.1f, %.1f):", playerSpawnX, playerSpawnZ);
        SDL_Log("  TerrainSystem.getHeightAt(): %.2f", heightFromTerrainSystem);
        SDL_Log("  TileCache.getHeightAt(): %.2f (found=%d)", heightFromTileCache, tileHasHeight ? 1 : 0);
        SDL_Log("  Player spawn Y (height + 0.1): %.2f", playerSpawnY);
    }

    // Initialize player state
    player_.transform = PlayerTransform::withYaw(glm::vec3(playerSpawnX, playerSpawnY, playerSpawnZ), 0.0f);
    player_.grounded = false;

    physics().createCharacter(glm::vec3(playerSpawnX, playerSpawnY, playerSpawnZ),
                              PlayerMovement::CAPSULE_HEIGHT, PlayerMovement::CAPSULE_RADIUS);

    SDL_Log("Physics initialized with %d active bodies", physics().getActiveBodyCount());
    publishProgress(0.92f);

    // Configure breadcrumb tracker for safe respawn positions
    // Safety check: not in water, not in terrain holes
    breadcrumbTracker.setSafetyCheck([this](const glm::vec3& pos) {
        // Check if position is above water level (with margin)
        float waterLevel = renderer_->getSystems().water().getWaterLevel();
        if (pos.y < waterLevel + 0.5f) {
            return false;  // In or near water
        }
        // Check if position is in a terrain hole
        if (renderer_->getSystems().terrain().isHole(pos.x, pos.z)) {
            return false;  // In terrain hole (cave entrance, etc.)
        }
        return true;
    });
    breadcrumbTracker.setMinDistance(5.0f);  // Breadcrumb every 5 meters
    breadcrumbTracker.setMaxBreadcrumbs(200);  // Keep last 200 positions (~1km of travel)
    SDL_Log("Breadcrumb tracker configured for respawn optimization");

    // Initialize flag simulation
    initFlag();

    // Initialize ECS world with scene entities
    initECS();
    publishProgress(0.94f);

    // Wire up ECS world for lighting (SceneManager's world was already set in initECS()).
    renderer_->setECSWorld(&ecsWorld_);
    renderer_->getSystems().scene().initializeECSLights();

    // Create ECS area entities for scatter systems (rocks, detritus)
    {
        ecs::EntityFactory factory(ecsWorld_);
        auto& systems = renderer_->getSystems();

        // Rock area entity
        auto& rockSystem = systems.rocks();
        ecs::RockGenerationParams rockGenParams{};
        ecs::ScatterMaterialParams rockMatParams{};
        // Note: exact config values were set in RendererInitPhases; area entity captures
        // the placement center/radius from the system name for identification
        auto rockArea = factory.createRockArea(
            glm::vec2(0.0f), 100.0f, 4.0f, 0.5f,
            rockMatParams, rockGenParams);
        rockSystem.setAreaEntity(rockArea);
        rockSystem.createInstanceEntities(ecsWorld_, true);
        rockSystem.rebuildFromECS(ecsWorld_);

        // Detritus area entity (if detritus system exists)
        if (auto* detritusSystem = systems.detritus()) {
            ecs::DetritusGenerationParams detGenParams{};
            ecs::ScatterMaterialParams detMatParams{0.85f, 0.0f};
            auto detArea = factory.createDetritusArea(
                glm::vec2(0.0f), 80.0f, 1.0f, 24.0f,
                detMatParams, detGenParams);
            detritusSystem->setAreaEntity(detArea);
            detritusSystem->createInstanceEntities(ecsWorld_, false);
            detritusSystem->rebuildFromECS(ecsWorld_);
        }
    }

    // Create physics colliders for scatter objects from ECS entities
    {
        size_t rockColliders = 0;
        for (auto [entity, transform, meshRef, variation] :
             ecsWorld_.view<ecs::Transform, ecs::MeshRef, ecs::RockTag, ecs::MeshVariation>(
                 entt::exclude<ecs::Children>).each()) {
            if (!meshRef.mesh) continue;
            const auto& vertices = meshRef.mesh->getVertices();

            std::vector<glm::vec3> positions;
            positions.reserve(vertices.size());
            for (const auto& v : vertices) {
                positions.push_back(v.position);
            }

            // Extract position and scale from the transform matrix
            glm::vec3 pos = transform.position();

            // Approximate uniform scale from first column length
            float scale = glm::length(glm::vec3(transform.matrix[0]));

            // Extract rotation quaternion (normalize columns first)
            glm::mat3 rotMat(
                glm::vec3(transform.matrix[0]) / scale,
                glm::vec3(transform.matrix[1]) / scale,
                glm::vec3(transform.matrix[2]) / scale
            );
            glm::quat rotation = glm::quat_cast(rotMat);

            physics().createStaticConvexHull(pos, positions.data(), positions.size(),
                                           scale, rotation);
            rockColliders++;
        }
        SDL_Log("Created %zu rock convex hull colliders (from ECS)", rockColliders);

        size_t detritusColliders = 0;
        for (auto [entity, transform, meshRef, variation] :
             ecsWorld_.view<ecs::Transform, ecs::MeshRef, ecs::DetritusTag, ecs::MeshVariation>(
                 entt::exclude<ecs::Children>).each()) {
            if (!meshRef.mesh) continue;
            const auto& vertices = meshRef.mesh->getVertices();

            std::vector<glm::vec3> positions;
            positions.reserve(vertices.size());
            for (const auto& v : vertices) {
                positions.push_back(v.position);
            }

            glm::vec3 pos = transform.position();
            float scale = glm::length(glm::vec3(transform.matrix[0]));
            glm::mat3 rotMat(
                glm::vec3(transform.matrix[0]) / scale,
                glm::vec3(transform.matrix[1]) / scale,
                glm::vec3(transform.matrix[2]) / scale
            );
            glm::quat rotation = glm::quat_cast(rotMat);

            physics().createStaticConvexHull(pos, positions.data(), positions.size(),
                                           scale, rotation);
            detritusColliders++;
        }
        if (detritusColliders > 0) {
            SDL_Log("Created %zu detritus convex hull colliders (from ECS)", detritusColliders);
        }
    }
    publishProgress(0.97f);

    // Kick off deferred terrain-dependent content (scene renderables, the
    // biome forest scan, settlement/bridge/ribbon queues) here so its one-time
    // setup cost lands behind the loading screen instead of inside the first
    // rendered frame. Tree meshes keep streaming in budgeted batches per frame
    // after startup - only the kick-off is front-loaded.
    {
        auto& systems = renderer_->getSystems();
        if (auto* deferredObjects = systems.deferredTerrainObjects()) {
            std::unique_ptr<ScatterSystem> detritusSystem;
            bool generatedNow = deferredObjects->tryGenerate(
                &systems.scene(), systems.tree(), systems.treeLOD(),
                systems.impostorCull(), systems.treeRenderer(), &systems.rocks(),
                detritusSystem, true);
            if (generatedNow && detritusSystem) {
                systems.setDetritus(std::move(detritusSystem));
            }
        }
    }
    publishProgress(1.0f);

    return true;
}

void Application::buildDebugCommands() {
    debugCommands_.clear();
    auto& sys = renderer_->getSystems();

    // Application
    debugCommands_.push_back({"app.pauseMenu", "Pause menu", "Application", SDL_SCANCODE_ESCAPE,
        [this] { gui_->gameMenu().handleEscape(); }});
    debugCommands_.push_back({"app.toggleGui", "Toggle GUI", "Application", SDL_SCANCODE_F1,
        [this] { gui_->toggleVisibility(); }});
    debugCommands_.push_back({"app.screenshot", "Save screenshot", "Application", SDL_SCANCODE_F12,
        [this] { renderer_->requestScreenshot(); }});
    debugCommands_.push_back({"app.toggleHud", "Toggle HUD", "Application", SDL_SCANCODE_H,
        [this] { gui_->gameHud().toggleVisible(); }});

    // Time
    debugCommands_.push_back({"time.sunrise", "Set time to sunrise", "Time", SDL_SCANCODE_1,
        [&sys] { sys.time().setTimeOfDay(0.25f); }});
    debugCommands_.push_back({"time.noon", "Set time to noon", "Time", SDL_SCANCODE_2,
        [&sys] { sys.time().setTimeOfDay(0.5f); }});
    debugCommands_.push_back({"time.sunset", "Set time to sunset", "Time", SDL_SCANCODE_3,
        [&sys] { sys.time().setTimeOfDay(0.75f); }});
    debugCommands_.push_back({"time.midnight", "Set time to midnight", "Time", SDL_SCANCODE_4,
        [&sys] { sys.time().setTimeOfDay(0.0f); }});
    debugCommands_.push_back({"time.faster", "Speed up time (2x)", "Time", SDL_SCANCODE_EQUALS,
        [&sys] { sys.time().setTimeScale(sys.time().getTimeScale() * 2.0f); }});
    debugCommands_.push_back({"time.slower", "Slow down time (0.5x)", "Time", SDL_SCANCODE_MINUS,
        [&sys] { sys.time().setTimeScale(sys.time().getTimeScale() * 0.5f); }});
    debugCommands_.push_back({"time.resume", "Resume auto time (real-time)", "Time", SDL_SCANCODE_R,
        [&sys] {
            sys.time().resumeAutoTime();
            sys.time().setTimeScale(1.0f);
        }});

    // Weather
    debugCommands_.push_back({"weather.intensityDown", "Decrease weather intensity", "Weather", SDL_SCANCODE_Z,
        [&sys] {
            float currentIntensity = sys.weatherState().getIntensity();
            sys.weatherState().setIntensity(std::max(0.0f, currentIntensity - 0.1f));
            SDL_Log("Weather intensity: %.1f", sys.weatherState().getIntensity());
        }});
    debugCommands_.push_back({"weather.intensityUp", "Increase weather intensity", "Weather", SDL_SCANCODE_X,
        [&sys] {
            float currentIntensity = sys.weatherState().getIntensity();
            sys.weatherState().setIntensity(std::min(1.0f, currentIntensity + 0.1f));
            SDL_Log("Weather intensity: %.1f", sys.weatherState().getIntensity());
        }});
    debugCommands_.push_back({"weather.cycle", "Cycle weather (Clear/Rain/Snow)", "Weather", SDL_SCANCODE_C,
        [&sys] {
            uint32_t currentType = sys.weatherState().getWeatherType();
            if (sys.weatherState().getIntensity() == 0.0f && currentType == 0) {
                sys.weatherState().setWeatherType(0);
                sys.weatherState().setIntensity(0.5f);
            } else if (currentType == 0) {
                sys.weatherState().setWeatherType(1);
                sys.weatherState().setIntensity(0.5f);
            } else if (currentType == 1) {
                sys.weatherState().setWeatherType(0);
                sys.weatherState().setIntensity(0.0f);
            }

            std::string weatherStatus = "Clear";
            if (sys.weatherState().getIntensity() > 0.0f) {
                if (sys.weatherState().getWeatherType() == 0) {
                    weatherStatus = "Rain";
                } else if (sys.weatherState().getWeatherType() == 1) {
                    weatherStatus = "Snow";
                }
            }
            SDL_Log("Weather type: %s, Intensity: %.1f", weatherStatus.c_str(), sys.weatherState().getIntensity());
        }});
    debugCommands_.push_back({"weather.snowDown", "Decrease snow amount", "Weather", SDL_SCANCODE_COMMA,
        [&sys] {
            float snow = sys.environmentSettings().snowAmount;
            sys.environmentSettings().snowAmount = std::max(0.0f, snow - 0.1f);
            SDL_Log("Snow amount: %.1f", sys.environmentSettings().snowAmount);
        }});
    debugCommands_.push_back({"weather.snowUp", "Increase snow amount", "Weather", SDL_SCANCODE_PERIOD,
        [&sys] {
            float snow = sys.environmentSettings().snowAmount;
            sys.environmentSettings().snowAmount = std::min(1.0f, snow + 0.1f);
            SDL_Log("Snow amount: %.1f", sys.environmentSettings().snowAmount);
        }});
    debugCommands_.push_back({"weather.snowToggle", "Toggle snow (0.0/1.0)", "Weather", SDL_SCANCODE_SLASH,
        [&sys] {
            float snow = sys.environmentSettings().snowAmount;
            sys.environmentSettings().snowAmount = (snow < 0.5f ? 1.0f : 0.0f);
            SDL_Log("Snow amount: %.1f", sys.environmentSettings().snowAmount);
        }});

    // Environment
    debugCommands_.push_back({"env.cloudStyle", "Toggle cloud style", "Environment", SDL_SCANCODE_V,
        [&sys] {
            sys.environmentControl().toggleCloudStyle();
            SDL_Log("Cloud style: %s", sys.environmentControl().isUsingParaboloidClouds() ? "Paraboloid LUT Hybrid" : "Procedural");
        }});
    debugCommands_.push_back({"env.fogDown", "Decrease fog density", "Environment", SDL_SCANCODE_LEFTBRACKET,
        [&sys] {
            float density = sys.environmentControl().getFogDensity();
            sys.environmentControl().setFogDensity(std::max(0.0f, density - 0.0025f));
            SDL_Log("Fog density: %.3f", sys.environmentControl().getFogDensity());
        }});
    debugCommands_.push_back({"env.fogUp", "Increase fog density", "Environment", SDL_SCANCODE_RIGHTBRACKET,
        [&sys] {
            float density = sys.environmentControl().getFogDensity();
            sys.environmentControl().setFogDensity(std::min(0.2f, density + 0.0025f));
            SDL_Log("Fog density: %.3f", sys.environmentControl().getFogDensity());
        }});
    debugCommands_.push_back({"env.fogToggle", "Toggle fog", "Environment", SDL_SCANCODE_BACKSLASH,
        [&sys] {
            sys.environmentControl().setFogEnabled(!sys.environmentControl().isFogEnabled());
            SDL_Log("Fog: %s", sys.environmentControl().isFogEnabled() ? "ON" : "OFF");
        }});
    debugCommands_.push_back({"env.confetti", "Spawn confetti at player", "Environment", SDL_SCANCODE_F,
        [this, &sys] {
            glm::vec3 playerPos = player_.transform.position;
            sys.environmentControl().spawnConfetti(playerPos, 8.0f, 100.0f, 0.5f);
            SDL_Log("Confetti!");
        }});

    // Physics
    debugCommands_.push_back({"physics.spawnRagdoll", "Spawn ragdoll", "Physics", SDL_SCANCODE_G,
        [this] { spawnRagdoll(); }});

    // Debug visualization
    debugCommands_.push_back({"debug.cascades", "Toggle cascade debug visualization", "Debug", SDL_SCANCODE_6,
        [&sys] {
            sys.debugControl().toggleCascadeDebug();
            SDL_Log("Cascade debug visualization: %s", sys.debugControl().isShowingCascadeDebug() ? "ON" : "OFF");
        }});
    debugCommands_.push_back({"debug.snowDepth", "Toggle snow depth debug visualization", "Debug", SDL_SCANCODE_7,
        [&sys] {
            sys.debugControl().toggleSnowDepthDebug();
            SDL_Log("Snow depth debug visualization: %s", sys.debugControl().isShowingSnowDepthDebug() ? "ON" : "OFF");
        }});
    debugCommands_.push_back({"debug.hiZ", "Toggle Hi-Z occlusion culling", "Debug", SDL_SCANCODE_8,
        [&sys] {
            sys.debugControl().setHiZCullingEnabled(!sys.debugControl().isHiZCullingEnabled());
            SDL_Log("Hi-Z occlusion culling: %s", sys.debugControl().isHiZCullingEnabled() ? "ON" : "OFF");
        }});
    debugCommands_.push_back({"debug.terrainHeight", "Terrain height diagnostic", "Debug", SDL_SCANCODE_9,
        [this] { runTerrainHeightDiagnostic(); }});

    // Terrain
    debugCommands_.push_back({"terrain.wireframe", "Toggle terrain wireframe", "Terrain", SDL_SCANCODE_T,
        [&sys] {
            sys.terrain().toggleTerrainWireframe();
            SDL_Log("Terrain wireframe: %s", sys.terrain().isTerrainWireframeMode() ? "ON" : "OFF");
        }});
}

void Application::runTerrainHeightDiagnostic() {
    // Terrain height diagnostic - compare CPU height vs physics raycast
    // Samples on a grid at integer positions (should align with physics samples)
    auto& sys = renderer_->getSystems();
    auto& debugLines = sys.debugControl().getDebugLineSystem();
    debugLines.clearPersistentLines();

    // Use scene origin (where objects are placed) as center for diagnostic
    // Scene is at Town 1: settlement coords (11000, 5200) -> world coords (2808, -2992)
    const float halfTerrain = 8192.0f;
    float centerX = 11000.0f - halfTerrain;
    float centerZ = 5200.0f - halfTerrain;

    int gridSize = 5;  // 5x5 grid of samples at 1m spacing
    float rayStartY = 500.0f;

    SDL_Log("=== Terrain Height Diagnostic (center=%.0f,%.0f grid=%dx%d) ===",
            centerX, centerZ, gridSize, gridSize);
    SDL_Log("Format: (x,z) cpu=H phys=H diff=D [tile info] hits=N");

    for (int gz = -gridSize/2; gz <= gridSize/2; gz++) {
        for (int gx = -gridSize/2; gx <= gridSize/2; gx++) {
            float x = centerX + gx;
            float z = centerZ + gz;

            // CPU height with debug info
            auto cpuInfo = sys.terrain().getHeightAtDebug(x, z);
            float cpuH = cpuInfo.height;

            // Physics height via raycast - get ALL hits to see overlapping tiles
            glm::vec3 rayFrom(x, rayStartY, z);
            glm::vec3 rayTo(x, -100.0f, z);
            auto hits = physics().castRayAllHits(rayFrom, rayTo);

            // Sort by Y to get highest hit
            float physicsH = cpuH;  // Default if no hit
            bool hasPhysicsHit = false;
            size_t numHits = hits.size();

            // Log all hits if multiple (indicates overlapping tiles)
            if (numHits > 1) {
                SDL_Log("  (%.0f, %.0f) MULTIPLE HITS (%zu):", x, z, numHits);
                for (size_t i = 0; i < numHits; i++) {
                    SDL_Log("    hit[%zu] y=%.3f bodyId=%u",
                            i, hits[i].position.y, hits[i].bodyId);
                }
            }

            for (const auto& hit : hits) {
                if (hit.hit && hit.position.y > physicsH - 50.0f) {
                    physicsH = hit.position.y;
                    hasPhysicsHit = true;
                    break;
                }
            }

            float diff = physicsH - cpuH;
            SDL_Log("  (%.0f, %.0f) cpu=%.3f phys=%.3f diff=%.4f [%s LOD%u tile(%d,%d)] hits=%zu%s",
                    x, z, cpuH, physicsH, diff,
                    cpuInfo.source, cpuInfo.lod, cpuInfo.tileX, cpuInfo.tileZ,
                    numHits, hasPhysicsHit ? "" : " (no hit)");

            // Add debug sphere at CPU height (green)
            debugLines.addSphere(glm::vec3(x, cpuH, z), 0.3f, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), 8);

            // Add debug sphere at physics height (red) - only if different
            if (std::abs(diff) > 0.001f && hasPhysicsHit) {
                debugLines.addSphere(glm::vec3(x, physicsH, z), 0.25f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 8);
            }
        }
    }

    SDL_Log("=== End Diagnostic (Green=CPU, Red=Physics) ===");
    SDL_Log("Press 9 again to re-run, spheres visible in debug mode");
}

void Application::run() {
    buildDebugCommands();

    auto lastTime = std::chrono::high_resolution_clock::now();
    float smoothedFps = 60.0f;

    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Store for GUI
        lastDeltaTime = deltaTime;
        if (deltaTime > 0.0f) {
            currentFps = currentFps * 0.95f + (1.0f / deltaTime) * 0.05f;
        }

        processEvents();

        auto& systems = renderer_->getSystems();

        // While the pause menu is open the world freezes but the app stays
        // responsive: rendering, GUI, streaming and deferred generation
        // continue below. TimeSystem::update() runs on the render path, so
        // it holds time-of-day/elapsed itself while paused.
        const bool simPaused = gui_->gameMenu().isOpen();
        systems.time().setPaused(simPaused);

        // Update input system
        input.update(deltaTime, camera.getForward());

        // Apply input to camera
        applyInputToCamera();

        // Process movement input for third-person mode
        glm::vec3 desiredVelocity(0.0f);
        auto& playerTransform = player_.transform;
        auto& playerMovement = player_.movement;
        bool isJumping = false;
        glm::vec3 physicsVelocity(0.0f);

        // Gameplay simulation freezes while the pause menu is open: no
        // physics stepping, character/ragdoll/NPC motion, cloth, or player
        // state changes. Presentation and streaming below keep running.
        if (!simPaused) {
            if (input.isThirdPersonMode()) {
                // Handle orientation lock toggle
                if (input.wantsOrientationLockToggle()) {
                    playerMovement.orientationLocked = !playerMovement.orientationLocked;
                    if (playerMovement.orientationLocked) {
                        playerMovement.lockedYaw = playerTransform.getYaw();
                    }
                    SDL_Log("Orientation lock: %s", playerMovement.orientationLocked ? "ON" : "OFF");
                }

                // Temporarily lock orientation if holding trigger/middle mouse
                bool effectiveLock = playerMovement.orientationLocked || input.isOrientationLockHeld();

                // Get facing mode settings
                auto& playerSettings = gui_->getPlayerSettings();
                FacingMode facingMode = playerSettings.facingMode;
                bool guiStrafeEnabled = (facingMode != FacingMode::FollowMovement);

                // Handle FollowTarget mode - place target if not set
                if (facingMode == FacingMode::FollowTarget && !playerSettings.hasTarget) {
                    // Place target 5m in front of player
                    glm::vec3 forward = playerTransform.getForward();
                    playerSettings.targetPosition = playerTransform.position + forward * 5.0f;
                    playerSettings.hasTarget = true;
                    SDL_Log("Target placed at (%.1f, %.1f, %.1f)",
                        playerSettings.targetPosition.x,
                        playerSettings.targetPosition.y,
                        playerSettings.targetPosition.z);
                }

                glm::vec3 moveDir = input.getMovementDirection();
                float moveLen = glm::length(moveDir);
                if (moveLen > 0.001f) {
                    // Clamp rather than normalize so analog stick magnitude scales speed
                    if (moveLen > 1.0f) moveDir /= moveLen;
                    float currentSpeed = input.isSprinting() ? sprintSpeed : moveSpeed;
                    desiredVelocity = moveDir * currentSpeed;

                    // Only rotate player to face movement direction if not locked and not in GUI strafe mode
                    if (!effectiveLock && !guiStrafeEnabled) {
                        float newYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
                        float currentYaw = playerTransform.getYaw();
                        float yawDiff = newYaw - currentYaw;
                        // Normalize yaw difference
                        while (yawDiff > 180.0f) yawDiff -= 360.0f;
                        while (yawDiff < -180.0f) yawDiff += 360.0f;
                        // Use slower rotation when motion matching is active so the trajectory
                        // predictor has time to show direction changes to the matcher.
                        // Fast rotation (10x) makes every query look like "moving forward" in
                        // local space, causing idle selection during turns.
                        auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
                        float yawRate = (sceneBuilder.hasCharacter() &&
                                         sceneBuilder.getAnimatedCharacter().isUsingMotionMatching())
                                        ? 4.0f : 10.0f;
                        float smoothedYaw = currentYaw + yawDiff * (1.0f - std::exp(-yawRate * deltaTime));
                        // Keep yaw in reasonable range
                        while (smoothedYaw > 360.0f) smoothedYaw -= 360.0f;
                        while (smoothedYaw < 0.0f) smoothedYaw += 360.0f;
                        playerTransform.setYaw(smoothedYaw);
                    }
                }

                // Handle strafe/lock-on facing modes
                if (guiStrafeEnabled) {
                    glm::vec3 targetDir;
                    if (facingMode == FacingMode::FollowCamera) {
                        // Face camera direction
                        targetDir = camera.getForward();
                    } else if (facingMode == FacingMode::FollowTarget && playerSettings.hasTarget) {
                        // Face target position
                        targetDir = playerSettings.targetPosition - playerTransform.position;
                    } else {
                        targetDir = playerTransform.getForward();
                    }

                    targetDir.y = 0.0f;
                    if (glm::length(targetDir) > 0.001f) {
                        targetDir = glm::normalize(targetDir);
                        float targetYaw = glm::degrees(atan2(targetDir.x, targetDir.z));
                        float currentYaw = playerTransform.getYaw();
                        float yawDiff = targetYaw - currentYaw;
                        while (yawDiff > 180.0f) yawDiff -= 360.0f;
                        while (yawDiff < -180.0f) yawDiff += 360.0f;
                        // Faster rotation for strafe mode responsiveness
                        float smoothedYaw = currentYaw + yawDiff * (1.0f - std::exp(-15.0f * deltaTime));
                        while (smoothedYaw > 360.0f) smoothedYaw -= 360.0f;
                        while (smoothedYaw < 0.0f) smoothedYaw += 360.0f;
                        playerTransform.setYaw(smoothedYaw);
                    }
                }
            }

            // Detect jump BEFORE physics update (character is still grounded when jump is requested)
            bool wasGrounded = physics().isCharacterOnGround();
            bool wantsJump = input.wantsJump();
            isJumping = wantsJump && wasGrounded;

            // If starting a jump, compute trajectory for animation sync
            if (isJumping) {
                glm::vec3 startPos = physics().getCharacterPosition();
                // Velocity: horizontal from input + jump impulse (5.0 m/s up, matching PhysicsSystem)
                glm::vec3 jumpVelocity = desiredVelocity;
                jumpVelocity.y = 5.0f;
                renderer_->getSystems().scene().getSceneBuilder().startCharacterJump(startPos, jumpVelocity, 9.81f, &physics());
            }

            // Always update physics character controller (handles gravity, jumping, and movement)
            physics().updateCharacter(deltaTime, desiredVelocity, wantsJump);

            // Apply ML policy torques to ragdolls before physics step
            uniconController_.update(ragdolls_, physics(), deltaTime);

            // Update physics simulation
            physics().update(deltaTime);

            // Detect and destroy ragdolls with NaN state (constraint solver diverged)
            ragdolls_.erase(
                std::remove_if(ragdolls_.begin(), ragdolls_.end(),
                    [this](ArticulatedBody& ragdoll) {
                        if (ragdoll.hasNaNState(physics())) {
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                        "Destroying ragdoll with NaN physics state");
                            ragdoll.destroy(physics());
                            return true;
                        }
                        return false;
                    }),
                ragdolls_.end());

            glm::vec3 playerPos = physics().getCharacterPosition();

            // Sync player entity position from physics character controller
            physicsVelocity = physics().getCharacterVelocity();
            playerTransform.position = playerPos;
            player_.grounded = physics().isCharacterOnGround();

            // Update breadcrumb tracker (Ghost of Tsushima respawn optimization)
            // Only track positions when player is grounded and not in water/hazards
            if (player_.grounded) {
                breadcrumbTracker.update(playerPos);
            }

            // Update scene object transforms from physics
            renderer_->getSystems().scene().update(physics());
        }

        // Physics terrain tile streaming continues while paused (it only
        // loads/unloads static collision tiles around the player).
        physicsTerrainManager_.update(playerTransform.position);

        // Update ECS systems (visibility culling, LOD)
        stepSettlementGeneration();
        stepWorldFeatureGeneration();
        updateECS(simPaused ? 0.0f : deltaTime);

        // Update player state in PlayerControlSubsystem for grass/snow/leaf interaction
        renderer_->getSystems().playerControl().setPlayerState(playerTransform.position, physicsVelocity, PlayerMovement::CAPSULE_RADIUS);

        // Wait for previous frame's GPU work to complete before updating dynamic meshes.
        // This prevents race conditions where we destroy mesh buffers while the GPU
        // is still reading them from the previous frame.
        renderer_->waitForPreviousFrame();

        // Sync settings from GUI
        renderer_->getSystems().scene().getSceneBuilder().setCapeEnabled(gui_->getPlayerSettings().capeEnabled);
        renderer_->getSystems().scene().getSceneBuilder().setShowSword(gui_->getPlayerSettings().showSword);
        renderer_->getSystems().scene().getSceneBuilder().setShowShield(gui_->getPlayerSettings().showShield);
        renderer_->getSystems().scene().getSceneBuilder().setShowWeaponAxes(gui_->getPlayerSettings().showWeaponAxes);

        if (!simPaused) {
            // Update flag cloth simulation
            updateFlag(deltaTime);

            // Update animated character (skeletal animation)
            // Calculate movement speed from desired velocity for animation state machine
            float movementSpeed = glm::length(glm::vec2(desiredVelocity.x, desiredVelocity.z));
            bool isGrounded = physics().isCharacterOnGround();

            // Pass motion matching parameters: position, facing direction, and input direction
            glm::vec3 inputDirection = glm::vec3(desiredVelocity.x, 0.0f, desiredVelocity.z);
            glm::vec3 facingDirection = playerTransform.getForward();

            // Determine strafe mode (GUI-enabled or orientation lock is active)
            auto& settings = gui_->getPlayerSettings();
            bool strafeMode = (settings.facingMode != FacingMode::FollowMovement) ||
                (input.isThirdPersonMode() &&
                 (playerMovement.orientationLocked || input.isOrientationLockHeld()));

            // Get facing direction for strafe mode
            glm::vec3 strafeFacingDirection;
            if (settings.facingMode == FacingMode::FollowTarget && settings.hasTarget) {
                // Face toward target
                strafeFacingDirection = settings.targetPosition - playerTransform.position;
            } else {
                // Face camera direction
                strafeFacingDirection = camera.getForward();
            }
            strafeFacingDirection.y = 0.0f;  // Horizontal only
            if (glm::length(strafeFacingDirection) > 0.001f) {
                strafeFacingDirection = glm::normalize(strafeFacingDirection);
            } else {
                strafeFacingDirection = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            renderer_->getSystems().scene().getSceneBuilder().updateAnimatedCharacter(
                deltaTime, renderer_->getVulkanContext().getAllocator(), renderer_->getVulkanContext().getVkDevice(),
                renderer_->getCommandPool(), renderer_->getVulkanContext().getVkGraphicsQueue(),
                movementSpeed, isGrounded, isJumping,
                playerTransform.position, facingDirection, inputDirection,
                strafeMode, strafeFacingDirection);

            // Feed animation-driven root yaw into character facing.
            // For walk/run clips this is near-zero (no visible effect). For turn-in-place
            // clips the extracted yaw delta drives the character's actual rotation, so the
            // turn animation produces real world-space rotation.
            {
                auto& sb = renderer_->getSystems().scene().getSceneBuilder();
                if (sb.hasCharacter() && sb.getAnimatedCharacter().isUsingMotionMatching()) {
                    float yawDelta = sb.getAnimatedCharacter()
                        .getMotionMatchingController().getExtractedRootYawDelta();
                    if (std::abs(yawDelta) > 0.001f) {
                        float currentYaw = playerTransform.getYaw();
                        float newYaw = currentYaw + glm::degrees(yawDelta);
                        while (newYaw > 360.0f) newYaw -= 360.0f;
                        while (newYaw < 0.0f) newYaw += 360.0f;
                        playerTransform.setYaw(newYaw);
                    }
                }
            }

            // Draw debug target indicator when in FollowTarget mode
            if (settings.facingMode == FacingMode::FollowTarget && settings.hasTarget) {
                auto& debugLines = renderer_->getSystems().debugControl().getDebugLineSystem();
                glm::vec3 targetPos = settings.targetPosition;

                // Draw a small sphere at target position
                debugLines.addSphere(targetPos, 0.3f, glm::vec4(1.0f, 0.3f, 0.3f, 1.0f), 12);

                // Draw a vertical line to make it more visible
                debugLines.addLine(targetPos, targetPos + glm::vec3(0.0f, 2.0f, 0.0f),
                                   glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));

                // Draw line from player to target
                debugLines.addLine(playerTransform.position + glm::vec3(0.0f, 1.0f, 0.0f),
                                   targetPos + glm::vec3(0.0f, 1.0f, 0.0f),
                                   glm::vec4(1.0f, 0.5f, 0.0f, 0.5f));
            }

            // Update NPC animations with LOD based on camera position
            renderer_->getSystems().scene().getSceneBuilder().updateNPCs(
                deltaTime, camera.getPosition());
        }

        // Update camera and player based on mode
        if (!simPaused && input.isThirdPersonMode()) {
            camera.setThirdPersonTarget(playerMovement.getFocusPoint(playerTransform.position));
            camera.updateThirdPerson(deltaTime);
            renderer_->getSystems().scene().updatePlayerTransform(playerMovement.getModelMatrix(playerTransform));

            // Dynamic FOV: widen during sprinting for sense of speed
            float targetFov = 45.0f;  // Base FOV
            if (input.isSprinting() && glm::length(desiredVelocity) > 0.1f) {
                targetFov = 55.0f;  // Sprint FOV
            }
            camera.setTargetFov(targetFov);

            // Update camera occlusion (fade objects between camera and player)
            updateCameraOcclusion(deltaTime);
        }

        camera.setAspectRatio(static_cast<float>(renderer_->getWidth()) / static_cast<float>(renderer_->getHeight()));

        // Build the GUI after input and simulation so panels show this frame's
        // state rather than one-frame-stale data. beginFrame starts the ImGui
        // frame; renderer_->render() ends it via the draw callback, and
        // cancelFrame handles skipped frames below.
        gui_->beginFrame();
        gui_->render(camera, lastDeltaTime, currentFps);

        // Update physics debug visualization (before render)
#ifdef JPH_DEBUG_RENDERER
        renderer_->updatePhysicsDebug(physics(), camera.getPosition());
#endif

        // Render frame - if skipped (window minimized/suspended), cancel GUI frame
        if (!renderer_->render(camera)) {
            gui_->cancelFrame();
        }

        // Update window title with FPS, time of day, and camera mode
        if (deltaTime > 0.0f) {
            smoothedFps = smoothedFps * 0.95f + (1.0f / deltaTime) * 0.05f;
        }
        float timeOfDay = systems.time().getTimeOfDay();
        int hours = static_cast<int>(timeOfDay * 24.0f);
        int minutes = static_cast<int>((timeOfDay * 24.0f - hours) * 60.0f);
        char title[96];
        const char* modeStr = input.isThirdPersonMode() ? "3rd Person" : "Free Cam";
        snprintf(title, sizeof(title), "Vulkan Game - FPS: %.0f | Time: %02d:%02d | %s (Tab to toggle)",
                 smoothedFps, hours, minutes, modeStr);
        SDL_SetWindowTitle(window, title);
    }

    renderer_->waitIdle();
}

void Application::shutdown() {
    // Catch-all settings save: sync values that other paths (debug GUI, OS
    // fullscreen button) may have changed live, then persist. Quality
    // overrides are only ever recorded by the settings page, so they are
    // already current.
    if (!settingsPath_.empty()) {
        settings_.mouseSensitivity = input.getMouseSensitivity();
        settings_.invertMouseY = input.getInvertMouseY();
        if (window) {
            settings_.fullscreen =
                (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        }
        settings_.saveToFile(settingsPath_);
    }

    renderer_->waitIdle();
    gui_.reset();  // RAII cleanup via destructor
    // InputSystem cleanup handled by destructor (RAII)

    // Destroy ragdolls before physics world
    for (auto& ragdoll : ragdolls_) {
        ragdoll.destroy(physics());
    }
    ragdolls_.clear();

    physicsTerrainManager_.cleanup();
    physics_.reset();  // RAII cleanup via optional reset
    renderer_.reset();  // RAII cleanup via unique_ptr reset

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    // Shutdown task scheduler (waits for all tasks to complete)
    TaskScheduler::instance().shutdown();

    SDL_Quit();
}

void Application::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Pass events to GUI first
        gui_->processEvent(event);

        // Pass events to input system
        input.processEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                renderer_->notifyWindowResized();
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_OCCLUDED:
                // Window minimized or hidden (e.g., macOS screen lock)
                SDL_Log("Window suspended (minimized/hidden/occluded)");
                renderer_->notifyWindowSuspended();
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // Window lost focus (user clicked on another app) - macOS-specific handling
                // On macOS, this can cause compositor caching issues with ghost frames
                SDL_Log("Window focus lost");
                renderer_->notifyWindowFocusLost();
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                // Window regained focus
                SDL_Log("Window focus gained");
                renderer_->notifyWindowFocusGained();
                break;
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_EXPOSED:
                // Window restored (e.g., macOS screen unlock)
                if (renderer_->isWindowSuspended()) {
                    SDL_Log("Window restored, recreating swapchain");
                    renderer_->notifyWindowRestored();
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                DebugCommands::dispatchKey(debugCommands_, event.key.scancode);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                auto& time = renderer_->getSystems().time();
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    time.setTimeOfDay(0.25f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
                    time.setTimeOfDay(0.5f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                    time.setTimeOfDay(0.75f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                    time.setTimeOfDay(0.0f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    time.resumeAutoTime();
                    time.setTimeScale(1.0f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    running = false;
                }
                break;
            }
            default:
                break;
        }
    }

    // Handle camera mode switch initialization
    if (input.wasModeSwitchedThisFrame()) {
        if (input.isThirdPersonMode()) {
            // Update player Y position to match terrain height (fixes spawn below terrain)
            auto& playerTransform = player_.transform;
            float terrainY = renderer_->getSystems().terrain().getHeightAt(
                playerTransform.position.x, playerTransform.position.z);
            float newY = terrainY + 0.1f;  // Slightly above terrain

            // Only update if significantly different (avoid jitter)
            if (std::abs(playerTransform.position.y - newY) > 1.0f) {
                playerTransform.position.y = newY;
                // Also update physics character position
                physics().setCharacterPosition(glm::vec3(
                    playerTransform.position.x, newY, playerTransform.position.z));
                SDL_Log("Player height corrected to %.2f (terrain=%.2f)", newY, terrainY);
            }

            // Initialize third-person camera from current free camera position
            // This ensures smooth transition instead of snapping to origin
            auto& playerMovement = player_.movement;
            camera.initializeThirdPersonFromCurrentPosition(playerMovement.getFocusPoint(playerTransform.position));
        } else {
            // Switching to free camera - just reset smoothing
            camera.resetSmoothing();
        }
    }
}

void Application::applyInputToCamera() {
    if (input.isThirdPersonMode()) {
        // Third-person: orbit camera around player
        camera.orbitYaw(input.getCameraYawInput());
        camera.orbitPitch(input.getCameraPitchInput());
        camera.adjustDistance(input.getCameraZoomInput());
    } else {
        // Free camera: direct movement and rotation
        camera.moveForward(input.getFreeCameraForward());
        camera.moveRight(input.getFreeCameraRight());
        camera.moveUp(input.getFreeCameraUp());
        camera.rotateYaw(input.getCameraYawInput());
        camera.rotatePitch(input.getCameraPitchInput());
    }

    // Handle gamepad time scale input
    float timeScaleInput = input.getTimeScaleInput();
    if (timeScaleInput != 0.0f) {
        auto& time = renderer_->getSystems().time();
        time.setTimeScale(time.getTimeScale() * timeScaleInput);
    }
}

std::string Application::getResourcePath() {
#ifdef __APPLE__
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if (mainBundle) {
        CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
        if (resourcesURL) {
            char path[PATH_MAX];
            if (CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8*)path, PATH_MAX)) {
                CFRelease(resourcesURL);
                return std::string(path);
            }
            CFRelease(resourcesURL);
        }
    }
    return ".";
#else
    return ".";
#endif
}

void Application::initFlag() {
    // Create cloth simulation: 20x15 grid, 0.15m spacing
    const int clothWidth = 20;
    const int clothHeight = 15;
    const float particleSpacing = 0.15f;

    // Position the cloth at the top of the pole
    // Flag pole is at (5, 0) with center at terrain + 1.5m (3m tall pole)
    // Top of pole is at terrain height + 1.5 + 1.5 = terrain + 3.0
    const float flagPoleX = 5.0f;
    const float flagPoleZ = 0.0f;
    float terrainHeight = renderer_->getSystems().terrain().getHeightAt(flagPoleX, flagPoleZ);
    float poleTopY = terrainHeight + 3.0f;  // Pole center is 1.5m above terrain, pole is 3m tall
    glm::vec3 clothTopLeft(flagPoleX - 0.1f, poleTopY, flagPoleZ);  // Slightly to the left of pole center

    clothSim.create(clothWidth, clothHeight, particleSpacing, clothTopLeft);

    // Pin the left edge of the cloth to the pole
    for (int y = 0; y < clothHeight; ++y) {
        clothSim.pinParticle(0, y);  // Pin left edge
    }

    // Create initial mesh geometry and upload to GPU
    auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
    clothSim.createMesh(sceneBuilder.getFlagClothMesh());
    sceneBuilder.uploadFlagClothMesh(
        renderer_->getVulkanContext().getAllocator(), renderer_->getVulkanContext().getVkDevice(),
        renderer_->getCommandPool(), renderer_->getVulkanContext().getVkGraphicsQueue());

    SDL_Log("Flag initialized with %dx%d cloth simulation", clothWidth, clothHeight);
}

// Generate settlement buildings one settlement per frame (queued nearest-first
// by the deferred-generation callback) so the cost amortizes across frames.
void Application::stepSettlementGeneration() {
    if (!settlementGen_ || settlementQueueNext_ >= settlementQueue_.size()) return;
    auto& systems = renderer_->getSystems();
    ecs::World* world = systems.ecsWorld();
    if (!world || !systems.scenePtr()) return;
    auto& sceneBuilder = systems.scene().getSceneBuilder();

    const Settlement& settlement = settlementQueue_[settlementQueueNext_++];
    for (ecs::Entity e : settlementGen_->generateSettlement(*world, settlement)) {
        sceneBuilder.addExternalSceneEntity(e);
    }

    // Street ribbons and wall runs from the same town layout (previously
    // exported but unrendered LineStrings).
    if (townFeatures_ && ribbonGen_) {
        auto townResult = townFeatures_->generateForSettlement(*world, *ribbonGen_, settlement);
        for (ecs::Entity e : townResult.entities) {
            sceneBuilder.addExternalSceneEntity(e);
        }
    }

    if (settlementQueueNext_ >= settlementQueue_.size()) {
        settlementGen_->logSummary();
        settlementGen_.reset();
        townFeatures_.reset();
        settlementQueue_.clear();
        settlementQueueNext_ = 0;
    }
}

void Application::stepWorldFeatureGeneration() {
    // Settlements (nearest-first) get the per-frame budget until done
    if (settlementGen_) return;
    if (!bridgeGen_ && ribbonQueueNext_ >= ribbonQueue_.size()) return;

    auto& systems = renderer_->getSystems();
    ecs::World* world = systems.ecsWorld();
    if (!world || !systems.scenePtr()) return;
    auto& sceneBuilder = systems.scene().getSceneBuilder();

    // One bridge deck per frame
    if (bridgeGen_) {
        if (bridgeQueueNext_ < bridgeQueue_.size()) {
            std::vector<WaterCrossing> one{bridgeQueue_[bridgeQueueNext_++]};
            auto result = bridgeGen_->generate(*world, one);
            for (ecs::Entity e : result.entities) {
                sceneBuilder.addExternalSceneEntity(e);
            }
            return;
        }
        bridgeGen_.reset();
        bridgeQueue_.clear();
        bridgeQueueNext_ = 0;
    }

    // One road/river ribbon per frame
    if (ribbonGen_ && ribbonQueueNext_ < ribbonQueue_.size()) {
        std::vector<RibbonMeshGenerator::Ribbon> one;
        one.push_back(std::move(ribbonQueue_[ribbonQueueNext_++]));
        auto result = ribbonGen_->generate(*world, one, ribbonSkipZones_);
        for (ecs::Entity e : result.entities) {
            sceneBuilder.addExternalSceneEntity(e);
        }
        if (ribbonQueueNext_ >= ribbonQueue_.size()) {
            ribbonQueue_.clear();
            ribbonSkipZones_.clear();
            ribbonQueueNext_ = 0;
            SDL_Log("Application: Linear world features complete");
        }
    }
}

void Application::queueLinearWorldFeatures(SceneBuilder& sceneBuilder) {
    auto& systems = renderer_->getSystems();
    ecs::World* world = systems.ecsWorld();
    if (!world) return;

    // Roads: draped ribbons following the terrain. Bridge crossings are
    // skipped (the deck geometry covers them); ford crossings stay draped so
    // the road dips through the shallow water. Draping loads terrain tiles,
    // so ribbons are queued here and generated one per frame.
    const RoadNetwork& roadNetwork = systems.roadData().getRoadNetwork();
    ribbonSkipZones_.clear();
    for (const auto& crossing : roadNetwork.crossings) {
        if (!crossing.isBridge) continue;
        glm::vec2 w = WorldCoords::contentToWorld(crossing.position);
        ribbonSkipZones_.push_back({w, crossing.span * 0.5f + 5.0f});
    }

    ribbonQueue_.clear();
    ribbonQueueNext_ = 0;
    for (const auto& road : roadNetwork.roads) {
        if (road.controlPoints.size() < 2) continue;
        RibbonMeshGenerator::Ribbon ribbon;
        ribbon.material = sceneBuilder.getKitMaterialId("MI_RockTrim");
        ribbon.followTerrain = true;
        // Paved types read as pale stone; tracks and paths read as dirt.
        switch (road.type) {
            case RoadType::MainRoad:
            case RoadType::Road:
                ribbon.color = glm::vec4(0.85f, 0.82f, 0.75f, 1.0f);
                break;
            case RoadType::Lane:
                ribbon.color = glm::vec4(0.72f, 0.62f, 0.48f, 1.0f);
                break;
            default:
                ribbon.color = glm::vec4(0.60f, 0.50f, 0.38f, 1.0f);
                break;
        }
        for (size_t i = 0; i < road.controlPoints.size(); ++i) {
            glm::vec2 w = WorldCoords::contentToWorld(road.controlPoints[i].position);
            ribbon.points.emplace_back(w.x, 0.0f, w.y);
            ribbon.widths.push_back(road.getWidthAt(i));
        }
        ribbonQueue_.push_back(std::move(ribbon));
    }

    // Rivers: ribbons at the water surface height from the spline data,
    // tinted as water (the real water shader integration is tracked in
    // WORLD_GENERATION_PLAN.md Phase 3 follow-up).
    const WaterPlacementData& waterData = systems.erosionData().getWaterData();
    for (const auto& river : waterData.rivers) {
        if (river.controlPoints.size() < 2) continue;
        RibbonMeshGenerator::Ribbon ribbon;
        ribbon.material = sceneBuilder.getWhiteMaterialId();
        ribbon.followTerrain = false;
        ribbon.color = glm::vec4(0.16f, 0.32f, 0.45f, 1.0f);
        ribbon.points = river.controlPoints;
        ribbon.widths = river.widths;
        ribbonQueue_.push_back(std::move(ribbon));
    }

    // Lakes: flat water discs at each lake's fill level (no terrain draping,
    // cheap enough to build immediately)
    if (!waterData.lakes.empty()) {
        MeshGeometry lakeGeo;
        for (const auto& lake : waterData.lakes) {
            GeneratedMeshUtil::appendDisc(
                lakeGeo, glm::vec3(lake.position.x, lake.waterLevel + 0.05f, lake.position.y),
                lake.radius, 48, glm::vec4(0.16f, 0.32f, 0.45f, 1.0f));
        }
        if (!lakeGeo.vertices.empty()) {
            std::vector<MeshGeometry> batch;
            batch.push_back(std::move(lakeGeo));
            std::vector<Mesh*> meshes = sceneBuilder.addGeneratedMeshes(std::move(batch));
            ecs::EntityFactory factory(*world);
            for (Mesh* mesh : meshes) {
                if (!mesh) continue;
                ecs::Entity e = factory.createStaticMesh(
                    mesh, sceneBuilder.getWhiteMaterialId(), glm::mat4(1.0f), false);
                sceneBuilder.addExternalSceneEntity(e);
            }
        }
    }
}

void Application::updateCameraOcclusion(float deltaTime) {
    // Only in third-person mode
    if (!input.isThirdPersonMode()) return;

    // Raycast from player focus point to camera position
    const auto& playerTransform = player_.transform;
    const auto& playerMovement = player_.movement;
    glm::vec3 playerFocus = playerMovement.getFocusPoint(playerTransform.position);
    glm::vec3 cameraPos = camera.getPosition();

    std::vector<RaycastHit> hits = physics().castRayAllHits(playerFocus, cameraPos);

    // If there are any hits, apply camera collision to pull camera closer
    if (!hits.empty()) {
        // Use the closest hit for camera collision
        float closestHitDistance = hits[0].distance;
        camera.applyCollisionDistance(closestHitDistance);
    }

    // Build set of currently occluding body IDs (for opacity fading)
    std::unordered_set<PhysicsBodyID> currentlyOccluding;
    for (const auto& hit : hits) {
        currentlyOccluding.insert(hit.bodyId);
    }

    // Update opacities and occlusion tags using ECS queries
    for (auto [entity, physicsBody] : ecsWorld_.view<ecs::PhysicsBody>().each()) {
        if (ecsWorld_.has<ecs::PlayerTag>(entity)) continue;

        PhysicsBodyID bodyID = static_cast<PhysicsBodyID>(physicsBody.bodyId);
        if (bodyID == INVALID_BODY_ID) continue;

        bool isOccluding = currentlyOccluding.count(bodyID) > 0;

        // Update OccludingCamera tag
        if (isOccluding && !ecsWorld_.has<ecs::OccludingCamera>(entity)) {
            ecsWorld_.add<ecs::OccludingCamera>(entity);
        } else if (!isOccluding && ecsWorld_.has<ecs::OccludingCamera>(entity)) {
            ecsWorld_.remove<ecs::OccludingCamera>(entity);
        }

        // Update opacity via ECS component and sync to renderable
        float targetOpacity = isOccluding ? occludedOpacity : 1.0f;
        if (!ecsWorld_.has<ecs::Opacity>(entity)) {
            ecsWorld_.add<ecs::Opacity>(entity, 1.0f);
        }
        auto& opacity = ecsWorld_.get<ecs::Opacity>(entity);
        float fadeFactor = 1.0f - std::exp(-occlusionFadeSpeed * deltaTime);
        opacity.value += (targetOpacity - opacity.value) * fadeFactor;
        // ecs::Opacity is authoritative; extractRenderData feeds it to every render path.
    }
}

void Application::updateFlag(float deltaTime) {
    // Clear previous frame collisions
    clothSim.clearCollisions();

    // Add player collision sphere
    glm::vec3 playerPos = physics().getCharacterPosition();
    float playerRadius = PlayerMovement::CAPSULE_RADIUS;
    float playerHeight = PlayerMovement::CAPSULE_HEIGHT;

    // Add collision spheres for the player capsule (one at bottom, middle, and top)
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerRadius, 0), playerRadius);
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerHeight * 0.5f, 0), playerRadius);
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerHeight - playerRadius, 0), playerRadius);

    // Add collision spheres for dynamic physics objects using ECS query
    for (auto [entity, physicsBody] : ecsWorld_.view<ecs::PhysicsBody>().each()) {
        // Skip player, flag pole, and flag cloth
        if (ecsWorld_.has<ecs::PlayerTag>(entity)) continue;
        if (ecsWorld_.has<ecs::FlagPoleTag>(entity)) continue;
        if (ecsWorld_.has<ecs::FlagClothTag>(entity)) continue;

        PhysicsBodyID bodyID = static_cast<PhysicsBodyID>(physicsBody.bodyId);
        if (bodyID == INVALID_BODY_ID) continue;

        PhysicsBodyInfo info = physics().getBodyInfo(bodyID);

        // Add approximate collision spheres for physics objects
        // For simplicity, use a sphere of radius 0.5 for all objects
        clothSim.addSphereCollision(info.position, 0.5f);
    }

    // Update cloth simulation with wind
    clothSim.update(deltaTime, &renderer_->getSystems().wind());

    // Update the mesh vertices from cloth particles and re-upload to GPU
    auto& flagSceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
    clothSim.updateMesh(flagSceneBuilder.getFlagClothMesh());
    flagSceneBuilder.uploadFlagClothMesh(
        renderer_->getVulkanContext().getAllocator(), renderer_->getVulkanContext().getVkDevice(),
        renderer_->getCommandPool(), renderer_->getVulkanContext().getVkGraphicsQueue());
}

void Application::initECS() {
    INIT_PROFILE_PHASE("ECS");

    auto& sceneManager = renderer_->getSystems().scene();
    SceneBuilder& sceneBuilder = sceneManager.getSceneBuilder();

    // Create ECS entities from renderables (tags and components assigned by SceneBuilder)
    sceneBuilder.setECSWorld(&ecsWorld_);
    // SceneManager needs its own ECS world pointer before ensureScenePhysics() below (and
    // before its physics/light queries). This is the single wiring point for it.
    sceneManager.setECSWorld(&ecsWorld_);
    sceneBuilder.createEntitiesFromRenderables();

    // Connect ECS world to renderer systems for direct entity queries
    renderer_->getSystems().setECSWorld(&ecsWorld_);

    // Wire ECS world to tree systems for entity creation and rendering
    if (auto* treeSystem = renderer_->getSystems().tree()) {
        treeSystem->setECSWorld(&ecsWorld_);
    }
    if (auto* treeRenderer = renderer_->getSystems().treeRenderer()) {
        treeRenderer->setECSWorld(&ecsWorld_);
    }

    // Now that the ECS world is set and scene entities exist, ensure every entity with a
    // PhysicsShapeInfo has a physics body. Idempotent: skips entities already created by the
    // deferred callback. Physics bodies live as ecs::PhysicsBody components (no parallel array).
    const auto& sceneEntities = sceneBuilder.getSceneEntities();
    sceneManager.ensureScenePhysics();

    SDL_Log("ECS initialized with %zu entities from scene", sceneEntities.size());

    // Initialize ECS Material Demo to showcase material components
    if (sceneBuilder.hasRenderables()) {
        ecs::ECSMaterialDemo::InitInfo demoInfo{};
        demoInfo.world = &ecsWorld_;
        demoInfo.cubeMesh = sceneBuilder.getCubeMesh();
        demoInfo.sphereMesh = sceneBuilder.getSphereMesh();
        demoInfo.metalTexture = const_cast<Texture*>(sceneBuilder.getMetalTexture());
        demoInfo.crateTexture = const_cast<Texture*>(sceneBuilder.getCrateTexture());
        demoInfo.materialRegistry = &sceneManager.getSceneBuilder().getMaterialRegistry();
        demoInfo.sceneOrigin = glm::vec2(
            sceneBuilder.getWellEntranceX() - 20.0f,
            sceneBuilder.getWellEntranceZ() - 20.0f
        );

        // Use terrain height function if available from renderer
        auto& terrainSystem = renderer_->getSystems().terrain();
        demoInfo.getTerrainHeight = [&terrainSystem](float x, float z) {
            return terrainSystem.getHeightAt(x, z);
        };

        ecsMaterialDemo_ = ecs::ECSMaterialDemo::create(demoInfo);
        if (ecsMaterialDemo_) {
            SDL_Log("ECS Material Demo: Initialized with demo entities");
        }
    }
}

void Application::updateECS(float deltaTime) {

    auto& sceneManager = renderer_->getSystems().scene();
    auto& sceneBuilder = sceneManager.getSceneBuilder();

    // Get scene entities from SceneBuilder (Phase 6: entities managed by SceneBuilder)
    const auto& sceneEntities = sceneBuilder.getSceneEntities();

    // Lazy initialization: if entities not yet created but renderables are available (deferred mode)
    if (sceneEntities.empty() && sceneBuilder.hasRenderables()) {
        if (sceneBuilder.getECSWorld() == nullptr) {
            sceneBuilder.setECSWorld(&ecsWorld_);
        }
        sceneBuilder.createEntitiesFromRenderables();
        if (!sceneBuilder.getSceneEntities().empty()) {
            SDL_Log("ECS: Populated %zu entities from deferred renderables", sceneBuilder.getSceneEntities().size());
        }
    }

    // Re-fetch after potential creation
    const auto& currentEntities = sceneBuilder.getSceneEntities();

    // Mark weapons as initialized once entities are populated
    // (BoneAttachments and hierarchy are set up in createEntitiesFromRenderables)
    if (!ecsWeaponsInitialized_ && !currentEntities.empty() && sceneBuilder.hasWeapons()) {
        ecsWeaponsInitialized_ = true;
    }

    // ECS Transform is now written directly at each per-frame source: physics objects by
    // SceneManager::updatePhysicsToScene, the player by updatePlayerTransform, NPCs by
    // updateNPCs, bone-attached weapons/axes by updateWeaponTransforms, hierarchy children
    // (cape) by updateWorldTransforms, and static objects once at creation. No blanket
    // Renderable->ECS sync loop is required.

    // Update ECS material demo (wetness/damage cycling)
    static float totalTime = 0.0f;
    totalTime += deltaTime;
    if (ecsMaterialDemo_) {
        ecsMaterialDemo_->update(deltaTime, totalTime);
    }

    // Update hierarchical world transforms (parent * local -> world)
    // This must run before visibility culling so world transforms are current
    ecs::systems::updateWorldTransforms(ecsWorld_);

    // Update visibility culling based on camera frustum.
    // The Visible tag this sets is consumed by the ECS light culling queries
    // (LightSystem). The scene-object draw path culls independently on the GPU.
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    ecs::Frustum frustum = ecs::Frustum::fromViewProjection(viewProj);
    ecs::systems::updateVisibility(ecsWorld_, frustum);
}

void Application::teleportTo(float worldX, float worldZ) {
    auto* terrain = renderer_->getSystems().terrainPtr();
    float terrainY = 50.0f;  // Fallback if terrain unavailable
    if (terrain) {
        // Pre-load high-res tiles so the height query and landing are accurate
        if (auto* tileCache = terrain->getTileCache()) {
            tileCache->preloadTilesAround(worldX, worldZ, 600.0f);
        }
        terrainY = terrain->getHeightAt(worldX, worldZ);
    }

    // Move the player character and its physics body
    player_.transform.position = glm::vec3(worldX, terrainY + 0.1f, worldZ);
    if (physics_) {
        physics().setCharacterPosition(player_.transform.position);
        // Pre-load physics terrain tiles around the destination
        for (int i = 0; i < 50; i++) {
            physicsTerrainManager_.update(player_.transform.position);
        }
    }

    // Move the camera: land slightly above and behind the destination
    camera.setPosition(glm::vec3(worldX, terrainY + 2.0f, worldZ));
    if (input.isThirdPersonMode()) {
        camera.initializeThirdPersonFromCurrentPosition(
            player_.movement.getFocusPoint(player_.transform.position));
    } else {
        camera.resetSmoothing();
    }

    SDL_Log("Teleported to (%.1f, %.1f, %.1f)", worldX, terrainY, worldZ);
}

void Application::spawnRagdoll() {
    if (!physics_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot spawn ragdoll: physics not initialized");
        return;
    }

    auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();

    // Build humanoid config from the player's skeleton if available,
    // otherwise use a default config with generic proportions
    ArticulatedBodyConfig config;
    if (sceneBuilder.hasCharacter()) {
        const Skeleton& skeleton = sceneBuilder.getAnimatedCharacter().getSkeleton();
        config = createHumanoidConfig(skeleton);
    } else {
        // Fallback: create a minimal config without skeleton mapping
        config = createHumanoidConfig(Skeleton{});
    }

    // Spawn 5m above the player position
    glm::vec3 spawnPos = player_.transform.position + glm::vec3(0.0f, 5.0f, 0.0f);

    ArticulatedBody ragdoll;
    if (ragdoll.create(physics(), config, spawnPos)) {
        ragdolls_.push_back(std::move(ragdoll));
        SDL_Log("Spawned ragdoll at (%.1f, %.1f, %.1f) - total ragdolls: %zu",
                spawnPos.x, spawnPos.y, spawnPos.z, ragdolls_.size());
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to spawn ragdoll");
    }
}
