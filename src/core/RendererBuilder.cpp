// RendererBuilder.cpp - Construction-time initialization for Renderer.
// Relocated verbatim from Renderer.cpp / RendererInitPhases.cpp. Behavior-preserving:
// method bodies are unchanged except member access is qualified through 'r.' and
// cross-calls go through RendererBuilder::.

#include "RendererBuilder.h"

#include "Renderer.h"
#include "RendererSystems.h"
#include "MaterialDescriptorFactory.h"
#include "passes/ShadowPassRecorder.h"
#include "passes/HDRPassRecorder.h"
#include "InitProfiler.h"
#include "core/pipeline/PassSchedulerBuilder.h"
#include "loading/AsyncSystemLoader.h"
#include "UBOs.h"

// Subsystem includes (moved from RendererInitPhases.cpp)
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "GodRaysSystem.h"
#include "SkySystem.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "GrassSystem.h"
#include "DisplacementSystem.h"
#include "WeatherSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "LeafSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"
#include "AtmosphereSystemGroup.h"
#include "SnowSystemGroup.h"
#include "GeometrySystemGroup.h"
#include "HiZSystem.h"
#include "GPUSceneBuffer.h"
#include "culling/GPUCullPass.h"
#include "culling/ShadowCullPass.h"
#include "ScatterSystem.h"
#include "ScatterSystemFactory.h"
#include "TreeSystem.h"
#include "ThreadedTreeGenerator.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "VegetationContentGenerator.h"
#include "VegetationSystemGroup.h"
#include "DeferredTerrainObjects.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "OceanFFT.h"
#include "WaterSystemGroup.h"
#include "SSRSystem.h"
#include "DebugLineSystem.h"
#include "Profiler.h"
#include "SkinnedMeshRenderer.h"
#include "npc/NPCRenderer.h"
#include "SceneManager.h"
#include "SceneBuilder.h"
#include "ErosionDataLoader.h"
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "world/SettlementRegistry.h"
#include "world/BiomeMap.h"
#include "ResizeCoordinator.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "UBOBuilder.h"
#include "WindSystem.h"
#include "SystemWiring.h"
#include "CoreResources.h"
#include "ScreenSpaceShadowSystem.h"
#include "TerrainFactory.h"
#include "threading/TaskScheduler.h"

#include <SDL3/SDL.h>
#include <array>
#include <filesystem>

bool RendererBuilder::initInternal(Renderer& r, const Renderer::InitInfo& info) {
    INIT_PROFILE_PHASE("Renderer");

    r.resourcePath = info.resourcePath;
    r.config_ = info.config;
    r.progressCallback_ = info.progressCallback;

    // Helper to report progress (no-op if callback not set)
    auto reportProgress = [&r](float progress, const char* phase) {
        if (r.progressCallback_) {
            r.progressCallback_(progress, phase);
        }
    };

    reportProgress(0.0f, "Initializing...");

    // Create subsystems container
    r.systems_ = std::make_unique<RendererSystems>();

    // Initialize Vulkan context
    // If a pre-initialized context was provided (instance or device already created),
    // take ownership and complete any remaining initialization.
    // Otherwise, create a new context and fully initialize it.
    {
        INIT_PROFILE_PHASE("VulkanContext");
        if (info.vulkanContext) {
            r.vulkanContext_ = std::move(const_cast<std::unique_ptr<VulkanContext>&>(info.vulkanContext));
            // Only call initDevice if device isn't already initialized
            // (LoadingRenderer may have already completed device init)
            if (!r.vulkanContext_->isDeviceReady()) {
                if (!r.vulkanContext_->initDevice(info.window)) {
                    SDL_Log("Failed to complete Vulkan device initialization");
                    return false;
                }
            }
        } else {
            r.vulkanContext_ = std::make_unique<VulkanContext>();
            if (!r.vulkanContext_->init(info.window)) {
                SDL_Log("Failed to initialize Vulkan context");
                return false;
            }
        }
    }

    // Phase 1: Core Vulkan resources (render pass, depth, framebuffers, command pool)
    reportProgress(0.05f, "Creating Vulkan resources");
    {
        INIT_PROFILE_PHASE("CoreVulkanResources");
        if (!initCoreVulkanResources(r)) return false;
    }

    // Initialize asset registry (after command pool is ready)
    reportProgress(0.08f, "Initializing asset registry");
    {
        INIT_PROFILE_PHASE("AssetRegistry");
        r.assetRegistry_.init(
            r.vulkanContext_->getVkDevice(),
            r.vulkanContext_->getVkPhysicalDevice(),
            r.vulkanContext_->getAllocator(),
            r.vulkanContext_->getCommandPool(),
            r.vulkanContext_->getVkGraphicsQueue());
    }

    // Phase 2: Descriptor infrastructure (layouts, pools)
    reportProgress(0.10f, "Creating descriptor infrastructure");
    {
        INIT_PROFILE_PHASE("DescriptorInfrastructure");
        if (!initDescriptorInfrastructure(r)) return false;
    }

    // Build shared InitContext for subsystem initialization
    // Pass pool sizes hint so subsystems can create consistent pools if needed
    InitContext initCtx = InitContext::build(
        *r.vulkanContext_, r.vulkanContext_->getCommandPool(), r.getDescriptorPool(),
        r.resourcePath, Renderer::MAX_FRAMES_IN_FLIGHT, r.config_.descriptorPoolSizes);

    // Phase 3: All subsystems (terrain, grass, weather, snow, water, etc.)
    // This is the heaviest phase, so we pass the progress callback for finer updates
    reportProgress(0.12f, "Initializing subsystems");
    {
        INIT_PROFILE_PHASE("Subsystems");
        if (!initSubsystems(r, initCtx)) return false;
    }

    // Phase 4: Control subsystems (after systems are ready)
    reportProgress(0.95f, "Initializing controls");
    {
        INIT_PROFILE_PHASE("ControlSubsystems");
        initControlSubsystems(r);
    }

    // Phase 5: Resize coordinator registration
    reportProgress(0.96f, "Configuring resize handler");
    {
        INIT_PROFILE_PHASE("ResizeCoordinator");
        initResizeCoordinator(r);
    }

    // Phase 5b: Temporal system registration (for ghost frame prevention)
    {
        INIT_PROFILE_PHASE("TemporalSystems");
        initTemporalSystems(r);
    }

    // Initialize pass recorders (must be after systems_ is set up)
    // Note: These use stateless recording - config is passed to record() each frame
    reportProgress(0.97f, "Creating pass recorders");
    {
        INIT_PROFILE_PHASE("PassRecorders");
        r.shadowPassRecorder_ = std::make_unique<ShadowPassRecorder>(*r.systems_);
        r.createHDRPassRecorder();
    }
    SDL_Log("Pass recorders initialized");

    // Setup frame graph with dependencies
    reportProgress(0.99f, "Configuring frame graph");
    {
        INIT_PROFILE_PHASE("PassScheduler");
        if (!setupPassScheduler(r)) return false;
    }
    SDL_Log("Frame graph configured");

    reportProgress(1.0f, "Ready");

    return true;
}

bool RendererBuilder::setupPassScheduler(Renderer& r) {
    // GUI callback (PostPasses still uses it)
    PassSchedulerBuilder::Callbacks callbacks;
    callbacks.guiRenderCallback = &r.guiRenderCallback;

    // Build state references for frame graph passes. The pass modules build their own
    // Params from these pointers and invoke the recorders directly.
    PassSchedulerBuilder::State state;
    state.lastSunIntensity = &r.lastSunIntensity;
    state.perfToggles = &r.perfToggles;
    state.framebuffers = &r.vulkanContext_->getFramebuffers();
    state.shadowRecorder = r.shadowPassRecorder_.get();
    state.hdrRecorder = r.hdrPassRecorder_.get();
    state.scenePipeline = &r.scenePipeline_;
    state.instancedScenePipeline = &r.instancedScenePipeline_;
    state.vulkanContext = r.vulkanContext_.get();
    state.lastViewProj = &r.lastViewProj;

    // Use PassSchedulerBuilder to configure all passes and dependencies
    if (!PassSchedulerBuilder::build(r.passScheduler_, *r.systems_, callbacks, state)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to build frame graph");
        return false;
    }
    return true;
}

bool RendererBuilder::createDescriptorSets(Renderer& r, SceneManager& scene) {
    vk::Device device = r.vulkanContext_->getVkDevice();

    // Create descriptor sets for all materials via MaterialRegistry
    // This replaces the hardcoded per-material descriptor set allocation
    auto& materialRegistry = scene.getSceneBuilder().getMaterialRegistry();

    // Lambda to build common bindings for a given frame (using GlobalBufferManager)
    SceneManager* scenePtr = &scene;
    auto getCommonBindings = [&r, scenePtr](uint32_t frameIndex) -> MaterialDescriptorFactory::CommonBindings {
        MaterialDescriptorFactory::CommonBindings common{};
        common.uniformBuffer = r.systems_->globalBuffers().uniformBuffers.buffers[frameIndex];
        common.uniformBufferSize = sizeof(UniformBufferObject);
        common.shadowMapView = r.systems_->shadow().getShadowImageView();
        common.shadowMapSampler = r.systems_->shadow().getShadowSampler();
        common.lightBuffer = r.systems_->globalBuffers().lightBuffers.buffers[frameIndex];
        common.lightBufferSize = sizeof(LightBuffer);
        common.emissiveMapView = scenePtr->getSceneBuilder().getDefaultEmissiveMap()->getImageView();
        common.emissiveMapSampler = scenePtr->getSceneBuilder().getDefaultEmissiveMap()->getSampler();
        common.pointShadowView = r.systems_->shadow().getPointShadowArrayView(frameIndex);
        common.pointShadowSampler = r.systems_->shadow().getPointShadowSampler();
        common.spotShadowView = r.systems_->shadow().getSpotShadowArrayView(frameIndex);
        common.spotShadowSampler = r.systems_->shadow().getSpotShadowSampler();
        common.snowMaskView = r.systems_->snowMask().getSnowMaskView();
        common.snowMaskSampler = r.systems_->snowMask().getSnowMaskSampler();
        // Snow and cloud shadow UBOs (bindings 10 and 11)
        common.snowUboBuffer = r.systems_->globalBuffers().snowBuffers.buffers[frameIndex];
        common.snowUboBufferSize = sizeof(SnowUBO);
        common.cloudShadowUboBuffer = r.systems_->globalBuffers().cloudShadowBuffers.buffers[frameIndex];
        common.cloudShadowUboBufferSize = sizeof(CloudShadowUBO);
        // Cloud shadow texture is added later in init() after cloudShadowSystem is initialized
        // Placeholder texture for unused PBR bindings (13-16)
        common.placeholderTextureView = scenePtr->getSceneBuilder().getWhiteTexture()->getImageView();
        common.placeholderTextureSampler = scenePtr->getSceneBuilder().getWhiteTexture()->getSampler();
        if (r.systems_->hasScreenSpaceShadow()) {
            common.screenShadowView = r.systems_->screenSpaceShadow()->getShadowBufferView();
            common.screenShadowSampler = r.systems_->screenSpaceShadow()->getShadowBufferSampler();
        }
        return common;
    };

    materialRegistry.createDescriptorSets(
        device,
        *r.getDescriptorPool(),
        r.scenePipeline_.getVkDescriptorSetLayout(),
        Renderer::MAX_FRAMES_IN_FLIGHT,
        getCommonBindings);

    if (!materialRegistry.hasDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create MaterialRegistry descriptor sets");
        return false;
    }

    // Rock and Detritus descriptor sets are now owned by their respective systems
    // They are created in initPhase2 when the systems are initialized

    return true;
}

// ===== GPU Skinning Implementation =====

bool RendererBuilder::initSkinnedMeshRenderer(Renderer& r, vk::RenderPass hdrRenderPass,
                                              std::unique_ptr<SkinnedMeshRenderer>& outSkinnedMesh,
                                              std::unique_ptr<NPCRenderer>& outNpcRenderer) {
    SkinnedMeshRenderer::InitInfo info{};
    info.device = r.vulkanContext_->getVkDevice();
    info.physicalDevice = r.vulkanContext_->getVkPhysicalDevice();  // For dynamic UBO alignment
    info.raiiDevice = &r.vulkanContext_->getRaiiDevice();
    info.allocator = r.vulkanContext_->getAllocator();
    info.descriptorPool = r.getDescriptorPool();
    info.renderPass = hdrRenderPass;
    info.extent = r.vulkanContext_->getVkSwapchainExtent();
    info.shaderPath = r.resourcePath + "/shaders";
    info.framesInFlight = Renderer::MAX_FRAMES_IN_FLIGHT;
    info.addCommonBindings = [](DescriptorManager::LayoutBuilder& builder) {
        ScenePipeline::addCommonDescriptorBindings(builder);
    };

    auto skinnedMeshRenderer = SkinnedMeshRenderer::create(info);
    if (!skinnedMeshRenderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SkinnedMeshRenderer");
        return false;
    }
    outSkinnedMesh = std::move(skinnedMeshRenderer);

    // Create NPCRenderer (uses SkinnedMeshRenderer for draw calls)
    NPCRenderer::InitInfo npcInfo{};
    npcInfo.skinnedMeshRenderer = outSkinnedMesh.get();
    outNpcRenderer = NPCRenderer::create(npcInfo);
    if (outNpcRenderer) {
        SDL_Log("NPCRenderer created successfully");
    }

    return true;
}

bool RendererBuilder::createSkinnedMeshRendererDescriptorSets(Renderer& r, SceneManager& scene,
                                                              SkinnedMeshRenderer& skinnedMesh) {
    const auto* whiteTexture = scene.getSceneBuilder().getWhiteTexture();
    const auto* emissiveMap = scene.getSceneBuilder().getDefaultEmissiveMap();
    const auto& sceneBuilder = scene.getSceneBuilder();
    const auto& materialRegistry = sceneBuilder.getMaterialRegistry();

    // Build point and spot shadow views for all frames
    std::vector<vk::ImageView> pointShadowViews(Renderer::MAX_FRAMES_IN_FLIGHT);
    std::vector<vk::ImageView> spotShadowViews(Renderer::MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; i++) {
        pointShadowViews[i] = r.systems_->shadow().getPointShadowArrayView(i);
        spotShadowViews[i] = r.systems_->shadow().getSpotShadowArrayView(i);
    }

    // Get the player's actual material from MaterialRegistry based on their materialId
    // This fixes the race condition where player could have different material based on FBX load success
    vk::ImageView playerDiffuseView = whiteTexture->getImageView();
    vk::Sampler playerDiffuseSampler = whiteTexture->getSampler();
    vk::ImageView playerNormalView = whiteTexture->getImageView();
    vk::Sampler playerNormalSampler = whiteTexture->getSampler();

    const ecs::World* ecsWorld = sceneBuilder.getECSWorld();
    ecs::Entity playerEntity = sceneBuilder.getPlayerEntity();
    if (ecsWorld && ecsWorld->valid(playerEntity) && ecsWorld->has<ecs::MaterialRef>(playerEntity)) {
        MaterialId playerMaterialId = ecsWorld->get<ecs::MaterialRef>(playerEntity).id;
        const auto* playerMaterial = materialRegistry.getMaterial(playerMaterialId);
        if (playerMaterial) {
            if (playerMaterial->diffuse) {
                playerDiffuseView = playerMaterial->diffuse->getImageView();
                playerDiffuseSampler = playerMaterial->diffuse->getSampler();
            }
            if (playerMaterial->normal) {
                playerNormalView = playerMaterial->normal->getImageView();
                playerNormalSampler = playerMaterial->normal->getSampler();
            }
            SDL_Log("SkinnedMeshRenderer: Using player material '%s'", playerMaterial->name.c_str());
        }
    }

    SkinnedMeshRenderer::DescriptorResources resources{};
    resources.globalBufferManager = &r.systems_->globalBuffers();
    resources.shadowMapView = r.systems_->shadow().getShadowImageView();
    resources.shadowMapSampler = r.systems_->shadow().getShadowSampler();
    resources.emissiveMapView = emissiveMap->getImageView();
    resources.emissiveMapSampler = emissiveMap->getSampler();
    resources.pointShadowViews = &pointShadowViews;
    resources.pointShadowSampler = r.systems_->shadow().getPointShadowSampler();
    resources.spotShadowViews = &spotShadowViews;
    resources.spotShadowSampler = r.systems_->shadow().getSpotShadowSampler();
    resources.snowMaskView = r.systems_->snowMask().getSnowMaskView();
    resources.snowMaskSampler = r.systems_->snowMask().getSnowMaskSampler();
    resources.whiteTextureView = whiteTexture->getImageView();
    resources.whiteTextureSampler = whiteTexture->getSampler();
    resources.playerDiffuseView = playerDiffuseView;
    resources.playerDiffuseSampler = playerDiffuseSampler;
    resources.playerNormalView = playerNormalView;
    resources.playerNormalSampler = playerNormalSampler;

    return skinnedMesh.createDescriptorSets(resources);
}

// ===== Async Initialization Implementation =====

bool RendererBuilder::initInternalAsync(Renderer& r, const Renderer::InitInfo& info) {
    INIT_PROFILE_PHASE("Renderer");

    r.resourcePath = info.resourcePath;
    r.config_ = info.config;
    r.progressCallback_ = info.progressCallback;

    // Helper to report progress
    auto reportProgress = [&r](float progress, const char* phase) {
        if (r.progressCallback_) {
            r.progressCallback_(progress, phase);
        }
    };

    reportProgress(0.0f, "Initializing...");

    // Create subsystems container
    r.systems_ = std::make_unique<RendererSystems>();

    // Initialize Vulkan context (must be synchronous - needed for everything else)
    {
        INIT_PROFILE_PHASE("VulkanContext");
        if (info.vulkanContext) {
            r.vulkanContext_ = std::move(const_cast<std::unique_ptr<VulkanContext>&>(info.vulkanContext));
            if (!r.vulkanContext_->isDeviceReady()) {
                if (!r.vulkanContext_->initDevice(info.window)) {
                    SDL_Log("Failed to complete Vulkan device initialization");
                    return false;
                }
            }
        } else {
            r.vulkanContext_ = std::make_unique<VulkanContext>();
            if (!r.vulkanContext_->init(info.window)) {
                SDL_Log("Failed to initialize Vulkan context");
                return false;
            }
        }
    }

    // Phase 1: Core Vulkan resources (synchronous - quick)
    reportProgress(0.05f, "Creating Vulkan resources");
    {
        INIT_PROFILE_PHASE("CoreVulkanResources");
        if (!initCoreVulkanResources(r)) return false;
    }

    // Initialize asset registry (synchronous - quick)
    reportProgress(0.08f, "Initializing asset registry");
    {
        INIT_PROFILE_PHASE("AssetRegistry");
        r.assetRegistry_.init(
            r.vulkanContext_->getVkDevice(),
            r.vulkanContext_->getVkPhysicalDevice(),
            r.vulkanContext_->getAllocator(),
            r.vulkanContext_->getCommandPool(),
            r.vulkanContext_->getVkGraphicsQueue());
    }

    // Phase 2: Descriptor infrastructure (synchronous - quick)
    reportProgress(0.10f, "Creating descriptor infrastructure");
    {
        INIT_PROFILE_PHASE("DescriptorInfrastructure");
        if (!initDescriptorInfrastructure(r)) return false;
    }

    // Build InitContext for subsystem initialization and store for async access.
    // The heavy async scaffolding (loader + this stored context) lives off the
    // Renderer in asyncInit_, allocated here and reset() once init completes.
    r.asyncInit_ = std::make_unique<AsyncInitState>();
    r.asyncInit_->ctx = InitContext::build(
        *r.vulkanContext_, r.vulkanContext_->getCommandPool(), r.getDescriptorPool(),
        r.resourcePath, Renderer::MAX_FRAMES_IN_FLIGHT, r.config_.descriptorPoolSizes);

    // Phase 3: Start async subsystem initialization
    reportProgress(0.12f, "Starting async subsystem loading");
    r.asyncInitComplete_ = false;

    // Create async loader and set up tasks
    Loading::AsyncSystemLoader::InitInfo loaderInfo;
    loaderInfo.workerCount = 0;  // Auto-detect

    r.asyncInit_->loader = Loading::AsyncSystemLoader::create(loaderInfo);
    if (!r.asyncInit_->loader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create AsyncSystemLoader");
        // Fall back to synchronous initialization
        if (!initSubsystems(r, r.asyncInit_->ctx)) return false;
        r.asyncInitComplete_ = true;
    } else {
        // Start async subsystem initialization
        if (!initSubsystemsAsync(r)) {
            return false;
        }
        r.asyncInit_->loader->start();
    }

    return true;
}

AsyncInitStatus RendererBuilder::pollAsyncInit(Renderer& r) {
    if (r.asyncInitComplete_) {
        return r.asyncInitFailed_ ? AsyncInitStatus::Failed : AsyncInitStatus::Ready;
    }

    if (!r.asyncInit_ || !r.asyncInit_->loader) {
        r.asyncInitComplete_ = true;
        return AsyncInitStatus::Ready;
    }

    Loading::AsyncSystemLoader& loader = *r.asyncInit_->loader;

    // Poll for completed tasks. The budget keeps main-thread finalize work
    // short enough that the loading screen presents a frame between polls.
    loader.pollCompletions(5.0f);

    // Update progress callback
    if (r.progressCallback_) {
        auto progress = loader.getProgress();
        // Map 0.0-1.0 to 0.12-0.95 (subsystem init range)
        float mappedProgress = 0.12f + progress.progress * 0.83f;
        r.progressCallback_(mappedProgress, progress.currentPhase.c_str());
    }

    // Check if all tasks are complete
    if (loader.isComplete()) {
        if (loader.hasError()) {
            r.asyncInitError_ = loader.getErrorMessage();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Async init failed: %s", r.asyncInitError_.c_str());
            // The loader (and any still-running sibling task) is stopped by
            // Renderer::cleanup when the caller releases the renderer.
            r.asyncInitComplete_ = true;
            r.asyncInitFailed_ = true;
            return AsyncInitStatus::Failed;
        }

        // Finalize initialization (quick synchronous steps)
        SDL_Log("Async subsystem loading complete, finalizing...");

        // Phase 4: Control subsystems
        if (r.progressCallback_) r.progressCallback_(0.95f, "Initializing controls");
        {
            INIT_PROFILE_PHASE("ControlSubsystems");
            initControlSubsystems(r);
        }

        // Phase 5: Resize coordinator
        if (r.progressCallback_) r.progressCallback_(0.96f, "Configuring resize handler");
        {
            INIT_PROFILE_PHASE("ResizeCoordinator");
            initResizeCoordinator(r);
        }

        // Phase 5b: Temporal systems
        {
            INIT_PROFILE_PHASE("TemporalSystems");
            initTemporalSystems(r);
        }

        // Initialize pass recorders
        if (r.progressCallback_) r.progressCallback_(0.97f, "Creating pass recorders");
        {
            INIT_PROFILE_PHASE("PassRecorders");
            r.shadowPassRecorder_ = std::make_unique<ShadowPassRecorder>(*r.systems_);
            r.createHDRPassRecorder();
        }
        SDL_Log("Pass recorders initialized");

        // Setup frame graph
        if (r.progressCallback_) r.progressCallback_(0.99f, "Configuring frame graph");
        {
            INIT_PROFILE_PHASE("PassScheduler");
            if (!setupPassScheduler(r)) {
                r.asyncInitError_ = "Failed to build frame graph";
                // The loader is stopped by Renderer::cleanup when the caller
                // releases the renderer, as on a task failure above.
                r.asyncInitComplete_ = true;
                r.asyncInitFailed_ = true;
                return AsyncInitStatus::Failed;
            }
        }
        SDL_Log("Frame graph configured");

        if (r.progressCallback_) r.progressCallback_(1.0f, "Ready");

        // Clean up async loader and the stored InitContext together. This is
        // the only place asyncInit_ is reset, exactly where the loader was
        // reset before, so the InitContext (read by the tasks) stays alive
        // until every task has completed.
        r.asyncInit_.reset();

        r.asyncInitComplete_ = true;
        SDL_Log("Async initialization complete");
        return AsyncInitStatus::Ready;
    }

    return AsyncInitStatus::Pending;
}

bool RendererBuilder::initSubsystemsAsync(Renderer& r) {
    // Build the shared task definitions and feed them to the async loader.
    // The tasks declare dependencies so AsyncSystemLoader can exploit parallelism.
    auto tasks = buildInitTasks(r, r.asyncInit_->ctx);
    for (auto& task : tasks) {
        r.asyncInit_->loader->addTask(std::move(task));
    }
    return true;
}

// ============================================================================
// High-level initialization phases (relocated from RendererInitPhases.cpp)
// ============================================================================

bool RendererBuilder::initCoreVulkanResources(Renderer& r) {
    // Create swapchain-dependent resources (render pass, depth buffer, framebuffers)
    if (!r.vulkanContext_->createSwapchainResources()) return false;

    // Create command pool and buffers
    if (!r.vulkanContext_->createCommandPoolAndBuffers(Renderer::MAX_FRAMES_IN_FLIGHT)) return false;

    // Initialize multi-threading infrastructure
    {
        INIT_PROFILE_PHASE("ThreadingInfra");

        uint32_t threadCount = TaskScheduler::instance().getThreadCount();

        r.asyncTransferManager_ = AsyncTransferManager::create(*r.vulkanContext_);
        if (!r.asyncTransferManager_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "AsyncTransferManager initialization failed - using synchronous transfers");
        }

        if (threadCount > 0) {
            r.threadedCommandPool_ = ThreadedCommandPool::create(*r.vulkanContext_, threadCount + 1);
            if (!r.threadedCommandPool_) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ThreadedCommandPool initialization failed - using single-threaded recording");
            }
        }

        if (r.asyncTransferManager_) {
            r.asyncTextureUploader_ = Loading::AsyncTextureUploader::create(
                r.vulkanContext_->getVkDevice(),
                r.vulkanContext_->getAllocator(),
                *r.asyncTransferManager_);
        }
        if (!r.asyncTextureUploader_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "AsyncTextureUploader initialization failed - using synchronous uploads");
        }
    }

    return true;
}

bool RendererBuilder::initDescriptorInfrastructure(Renderer& r) {
    // Initialize scene pipeline (descriptor set layout)
    if (!r.scenePipeline_.initLayout(*r.vulkanContext_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize scene pipeline layout");
        return false;
    }

    // Initialize instanced scene pipeline layout (for GPU-driven indirect rendering)
    if (!r.instancedScenePipeline_.initLayout(*r.vulkanContext_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize instanced scene pipeline layout");
        return false;
    }

    // Create descriptor pool (shared resource allocator)
    r.descriptorPool_.emplace(r.vulkanContext_->getRaiiDevice(), r.config_.setsPerPool, r.config_.descriptorPoolSizes);

    return true;
}

bool RendererBuilder::initSubsystems(Renderer& r, const InitContext& initCtx) {
    // Execute the shared task definitions synchronously in dependency order
    auto tasks = buildInitTasks(r, initCtx);
    for (auto& task : tasks) {
        if (task.cpuWork && !task.cpuWork()) return false;
        if (task.gpuWork && !task.gpuWork()) return false;
    }
    return true;
}

// ============================================================================
// buildInitTasks - Single source of truth for subsystem initialization
//
// Returns tasks in valid dependency order. Each task declares its dependencies
// so the AsyncSystemLoader can exploit parallelism when running async.
// The sync path simply executes them sequentially.
//
// Initialization Tiers:
//   Tier 0 (core):         PostProcess, Pipeline, SkinnedMesh, GlobalBuffers, Shadow
//   Tier 1 (terrain):      Terrain system (depends on core)
//   Tier 2a (snow_weather): Snow/weather systems (depends on core, parallel with terrain)
//   Tier 2b (scene):       Scene manager + descriptor sets (depends on terrain + snow_weather)
//   Tier 3 (vegetation):   Vegetation systems (depends on scene)
//   Tier 3b (atmosphere):  Atmosphere systems (depends on scene, parallel with vegetation)
//   Tier 4 (water):        Water systems (depends on vegetation + atmosphere)
//   Tier 5 (finalize):     Wiring, geometry, culling, debug, profiler (depends on water)
// ============================================================================
std::vector<Loading::SystemInitTask> RendererBuilder::buildInitTasks(Renderer& r, const InitContext& initCtx) {
    std::vector<Loading::SystemInitTask> tasks;
    const InitContext* ctxPtr = &initCtx;
    VkFormat swapchainImageFormat = static_cast<VkFormat>(r.vulkanContext_->getVkSwapchainImageFormat());

    // Task bodies run on worker threads (cpuWork), so tasks that can execute
    // concurrently must not share a command pool (pools are externally
    // synchronized). Each task gets its own InitContext copy with a dedicated
    // worker pool; the sync fallback path (no asyncInit_) shares the one
    // context since it runs tasks sequentially.
    auto makeTaskContext = [&r, ctxPtr]() -> const InitContext* {
        if (!r.asyncInit_) return ctxPtr;
        InitContext taskCtx = *ctxPtr;
        vk::CommandPool workerPool = r.vulkanContext_->createWorkerCommandPool();
        if (workerPool) taskCtx.commandPool = workerPool;
        r.asyncInit_->taskContexts.push_back(taskCtx);
        return &r.asyncInit_->taskContexts.back();
    };

    // Shared constants
    // Scene origin sits at Town 1 (settlement id 1, content coords 11000,5200)
    // so the hand-placed green spawns inside the largest procedural town.
    const float halfTerrain = 8192.0f;
    glm::vec2 sceneOrigin(11000.0f - halfTerrain, 5200.0f - halfTerrain);

    // Registry writes happen only in gpuWork (main thread, inside
    // pollCompletions). cpuWork builds every system into per-task staging
    // shared by both lambdas and reads back its own products through those
    // staged pointers; siblings running concurrently only read systems that
    // earlier tasks already registered. The loader schedules a dependent only
    // after its dependencies' gpuWork ran, so a dependent's cpuWork never
    // looks up an unregistered system.

    // ========== TASK: Core Systems (Tier 0) ==========
    {
        struct Staged {
            std::unique_ptr<PostProcessSystem> postProcess;
            std::unique_ptr<BloomSystem> bloom;
            std::unique_ptr<BilateralGridSystem> bilateralGrid;
            std::unique_ptr<GodRaysSystem> godRays;
            std::unique_ptr<SkinnedMeshRenderer> skinnedMesh;
            std::unique_ptr<NPCRenderer> npcRenderer;
            std::unique_ptr<GlobalBufferManager> globalBuffers;
            std::unique_ptr<ShadowSystem> shadow;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "core";
        task.displayName = "Core GPU systems";
        task.weight = 0.1f;
        task.cpuWork = [&r, ctxPtr = makeTaskContext(), swapchainImageFormat, staged]() -> bool {
            // PostProcess (creates HDR render pass needed by almost everything)
            if (r.progressCallback_) r.progressCallback_(0.12f, "Post-processing systems");
            {
                INIT_PROFILE_PHASE("PostProcessing");
                auto bundle = PostProcessSystem::createWithDependencies(*ctxPtr, r.vulkanContext_->getRenderPass(), swapchainImageFormat);
                if (!bundle) return false;
                staged->postProcess = std::move(bundle->postProcess);
                staged->bloom = std::move(bundle->bloom);
                staged->bilateralGrid = std::move(bundle->bilateralGrid);
                staged->godRays = std::move(bundle->godRays);
            }
            vk::RenderPass hdrRenderPass = staged->postProcess->getHDRRenderPass();

            // Graphics pipeline
            if (r.progressCallback_) r.progressCallback_(0.14f, "Graphics pipeline");
            {
                INIT_PROFILE_PHASE("GraphicsPipeline");
                if (!r.scenePipeline_.createGraphicsPipeline(*r.vulkanContext_, hdrRenderPass, r.resourcePath)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
                    return false;
                }
                if (!r.instancedScenePipeline_.createGraphicsPipeline(*r.vulkanContext_, hdrRenderPass, r.resourcePath)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced graphics pipeline");
                    return false;
                }
            }

            // Skinned mesh renderer
            if (r.progressCallback_) r.progressCallback_(0.15f, "Skinned mesh renderer");
            {
                INIT_PROFILE_PHASE("SkinnedMeshRenderer");
                if (!initSkinnedMeshRenderer(r, hdrRenderPass, staged->skinnedMesh, staged->npcRenderer)) return false;
            }

            // Global buffer manager
            if (r.progressCallback_) r.progressCallback_(0.17f, "Global buffers");
            {
                INIT_PROFILE_PHASE("GlobalBufferManager");
                staged->globalBuffers = GlobalBufferManager::create(r.vulkanContext_->getAllocator(),
                    r.vulkanContext_->getVkPhysicalDevice(), Renderer::MAX_FRAMES_IN_FLIGHT);
                if (!staged->globalBuffers) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GlobalBufferManager");
                    return false;
                }
            }

            // Initialize light buffers with empty data
            for (uint32_t i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; i++) {
                LightBuffer emptyBuffer{};
                emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
                staged->globalBuffers->updateLightBuffer(i, emptyBuffer);
            }

            // Shadow system
            if (r.progressCallback_) r.progressCallback_(0.19f, "Shadow system");
            {
                INIT_PROFILE_PHASE("ShadowSystem");
                staged->shadow = ShadowSystem::create(*ctxPtr,
                    r.scenePipeline_.getVkDescriptorSetLayout(),
                    staged->skinnedMesh->getDescriptorSetLayout());
                if (!staged->shadow) return false;
            }

            return true;
        };
        task.gpuWork = [&r, staged]() -> bool {
            r.systems_->setPostProcess(std::move(staged->postProcess));
            r.systems_->setBloom(std::move(staged->bloom));
            r.systems_->setBilateralGrid(std::move(staged->bilateralGrid));
            r.systems_->setGodRays(std::move(staged->godRays));
            r.systems_->setSkinnedMesh(std::move(staged->skinnedMesh));
            if (staged->npcRenderer) r.systems_->setNPCRenderer(std::move(staged->npcRenderer));
            r.systems_->setGlobalBuffers(std::move(staged->globalBuffers));
            r.systems_->setShadow(std::move(staged->shadow));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Terrain System (Tier 1) ==========
    {
        struct Staged {
            std::unique_ptr<TerrainSystem> terrain;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "terrain";
        task.displayName = "Terrain system";
        task.dependencies = {"core"};
        task.weight = 0.15f;
        task.cpuWork = [&r, ctxPtr = makeTaskContext(), staged]() -> bool {
            if (r.progressCallback_) r.progressCallback_(0.20f, "Terrain system");
            INIT_PROFILE_PHASE("TerrainSystem");

            TerrainFactory::Config terrainFactoryConfig{};
            terrainFactoryConfig.hdrRenderPass = r.systems_->postProcess().getHDRRenderPass();
            terrainFactoryConfig.shadowRenderPass = r.systems_->shadow().getShadowRenderPass();
            terrainFactoryConfig.shadowMapSize = r.systems_->shadow().getShadowMapSize();
            terrainFactoryConfig.resourcePath = r.resourcePath;

            // VT feedback needs fragment shader SSBO atomics
            terrainFactoryConfig.useVirtualTexture =
                terrainFactoryConfig.useVirtualTexture &&
                r.vulkanContext_->hasFragmentStoresAndAtomics();

            staged->terrain = TerrainFactory::create(*ctxPtr, terrainFactoryConfig);
            return staged->terrain != nullptr;
        };
        task.gpuWork = [&r, staged]() -> bool {
            r.systems_->setTerrain(std::move(staged->terrain));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Snow/Weather Systems (Tier 2a - parallel with terrain) ==========
    {
        struct Staged {
            std::unique_ptr<SnowMaskSystem> snowMask;
            std::unique_ptr<VolumetricSnowSystem> volumetricSnow;
            std::unique_ptr<WeatherSystem> weather;
            std::unique_ptr<LeafSystem> leaf;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "snow_weather";
        task.displayName = "Snow and weather";
        task.dependencies = {"core"};
        task.weight = 0.05f;
        task.cpuWork = [&r, ctxPtr = makeTaskContext(), staged]() -> bool {
            if (r.progressCallback_) r.progressCallback_(0.28f, "Snow and weather systems");
            INIT_PROFILE_PHASE("SnowWeather");

            vk::RenderPass hdrRenderPass = r.systems_->postProcess().getHDRRenderPass();
            SnowSystemGroup::CreateDeps snowDeps{*ctxPtr, hdrRenderPass};
            auto snowBundle = SnowSystemGroup::createAll(snowDeps);
            if (!snowBundle) return false;

            staged->snowMask = std::move(snowBundle->snowMask);
            staged->volumetricSnow = std::move(snowBundle->volumetricSnow);
            staged->weather = std::move(snowBundle->weather);
            staged->leaf = std::move(snowBundle->leaf);
            return true;
        };
        task.gpuWork = [&r, staged]() -> bool {
            r.systems_->setSnowMask(std::move(staged->snowMask));
            r.systems_->setVolumetricSnow(std::move(staged->volumetricSnow));
            r.systems_->setWeather(std::move(staged->weather));
            r.systems_->setLeaf(std::move(staged->leaf));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Scene Manager (Tier 2b) ==========
    {
        struct Staged {
            std::unique_ptr<SceneManager> scene;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "scene";
        task.displayName = "Scene manager";
        task.dependencies = {"terrain", "snow_weather"};
        task.weight = 0.15f;
        task.cpuWork = [&r, sceneOrigin, staged]() -> bool {
            if (r.progressCallback_) r.progressCallback_(0.32f, "Scene manager");
            INIT_PROFILE_PHASE("SceneManager");

            SceneBuilder::InitInfo sceneInfo{};
            sceneInfo.allocator = r.vulkanContext_->getAllocator();
            sceneInfo.device = r.vulkanContext_->getVkDevice();
            sceneInfo.commandPool = r.vulkanContext_->getCommandPool();
            sceneInfo.graphicsQueue = r.vulkanContext_->getVkGraphicsQueue();
            sceneInfo.physicalDevice = r.vulkanContext_->getVkPhysicalDevice();
            sceneInfo.resourcePath = r.resourcePath;
            sceneInfo.assetRegistry = &r.assetRegistry_;
            sceneInfo.getTerrainHeight = [&r](float x, float z) {
                return r.systems_->terrain().getHeightAt(x, z);
            };
            sceneInfo.sceneOrigin = sceneOrigin;
            sceneInfo.deferRenderables = true;

            staged->scene = SceneManager::create(sceneInfo);
            if (!staged->scene) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SceneManager");
                return false;
            }

            // Create descriptor sets (needs scene + snow systems)
            if (!createDescriptorSets(r, *staged->scene)) return false;
            if (!createSkinnedMeshRendererDescriptorSets(r, *staged->scene, r.systems_->skinnedMesh())) return false;

            return true;
        };
        task.gpuWork = [&r, staged]() -> bool {
            r.systems_->setScene(std::move(staged->scene));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Vegetation Systems (Tier 3) ==========
    {
        struct Staged {
            std::unique_ptr<WindSystem> wind;
            std::unique_ptr<DisplacementSystem> displacement;
            std::unique_ptr<GrassSystem> grass;
            std::unique_ptr<ScatterSystem> rocks;
            std::unique_ptr<TreeSystem> tree;
            std::unique_ptr<TreeRenderer> treeRenderer;
            std::unique_ptr<TreeLODSystem> treeLOD;
            std::unique_ptr<ImpostorCullSystem> impostorCull;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "vegetation";
        task.displayName = "Vegetation systems";
        task.dependencies = {"scene"};
        task.weight = 0.2f;
        task.cpuWork = [&r, ctxPtr = makeTaskContext(), sceneOrigin, staged]() -> bool {
            if (r.progressCallback_) r.progressCallback_(0.45f, "Vegetation systems");
            INIT_PROFILE_PHASE("VegetationSystems");

            CoreResources core = CoreResources::collect(
                r.systems_->postProcess(), r.systems_->shadow(), r.systems_->terrain(), Renderer::MAX_FRAMES_IN_FLIGHT);

            ScatterSystemFactory::RockConfig rockConfig{};
            rockConfig.rockVariations = 6;
            rockConfig.rocksPerVariation = 10;
            rockConfig.minRadius = 0.4f;
            rockConfig.maxRadius = 2.0f;
            rockConfig.placementRadius = 100.0f;
            rockConfig.placementCenter = sceneOrigin;
            rockConfig.minDistanceBetween = 4.0f;
            rockConfig.roughness = 0.35f;
            rockConfig.asymmetry = 0.3f;
            rockConfig.subdivisions = 3;
            rockConfig.materialRoughness = 0.75f;
            rockConfig.materialMetallic = 0.0f;

            VegetationSystemGroup::CreateDeps vegDeps{
                *ctxPtr,
                core.hdr.renderPass,
                core.shadow.renderPass,
                core.shadow.mapSize,
                core.terrain.size,
                core.terrain.getHeightAt,
                rockConfig
            };

            auto vegBundle = VegetationSystemGroup::createAll(vegDeps);
            if (!vegBundle) return false;

            staged->wind = std::move(vegBundle->wind);
            staged->displacement = std::move(vegBundle->displacement);
            staged->grass = std::move(vegBundle->grass);
            staged->rocks = std::move(vegBundle->rocks);
            staged->tree = std::move(vegBundle->tree);
            staged->treeRenderer = std::move(vegBundle->treeRenderer);
            staged->treeLOD = std::move(vegBundle->treeLOD);
            staged->impostorCull = std::move(vegBundle->impostorCull);
            return true;
        };
        task.gpuWork = [&r, staged]() -> bool {
            r.systems_->setWind(std::move(staged->wind));
            r.systems_->setDisplacement(std::move(staged->displacement));
            r.systems_->setGrass(std::move(staged->grass));
            r.systems_->setRocks(std::move(staged->rocks));
            r.systems_->setTree(std::move(staged->tree));
            r.systems_->setTreeRenderer(std::move(staged->treeRenderer));
            if (staged->treeLOD) r.systems_->setTreeLOD(std::move(staged->treeLOD));
            if (staged->impostorCull) r.systems_->setImpostorCull(std::move(staged->impostorCull));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Atmosphere Systems (Tier 3b - parallel with vegetation) ==========
    {
        struct Staged {
            std::unique_ptr<SkySystem> sky;
            std::unique_ptr<FroxelSystem> froxel;
            std::unique_ptr<AtmosphereLUTSystem> atmosphereLUT;
            std::unique_ptr<CloudShadowSystem> cloudShadow;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "atmosphere";
        task.displayName = "Atmosphere systems";
        task.dependencies = {"scene"};
        task.weight = 0.1f;
        task.cpuWork = [&r, ctxPtr = makeTaskContext(), staged]() -> bool {
            if (r.progressCallback_) r.progressCallback_(0.60f, "Atmosphere systems");
            INIT_PROFILE_PHASE("AtmosphereSubsystems");

            CoreResources core = CoreResources::collect(
                r.systems_->postProcess(), r.systems_->shadow(), r.systems_->terrain(), Renderer::MAX_FRAMES_IN_FLIGHT);

            AtmosphereSystemGroup::CreateDeps atmosDeps{
                *ctxPtr,
                core.hdr.renderPass,
                core.shadow.cascadeView,
                core.shadow.sampler,
                r.systems_->globalBuffers().lightBuffers.buffers
            };
            auto atmosBundle = AtmosphereSystemGroup::createAll(atmosDeps);
            if (!atmosBundle) return false;

            staged->sky = std::move(atmosBundle->sky);
            staged->froxel = std::move(atmosBundle->froxel);
            staged->atmosphereLUT = std::move(atmosBundle->atmosphereLUT);
            staged->cloudShadow = std::move(atmosBundle->cloudShadow);

            AtmosphereSystemGroup::wireToPostProcess(*staged->froxel, r.systems_->postProcess());
            return true;
        };
        task.gpuWork = [&r, staged]() -> bool {
            r.systems_->setSky(std::move(staged->sky));
            r.systems_->setFroxel(std::move(staged->froxel));
            r.systems_->setAtmosphereLUT(std::move(staged->atmosphereLUT));
            r.systems_->setCloudShadow(std::move(staged->cloudShadow));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Water Systems (Tier 4) ==========
    // Creates the water systems only. Their configuration (flow-map generation)
    // and descriptor sets go through WaterSystemGroup's RendererSystems-based
    // API, so they run at the start of the finalize task, once these systems
    // are registered.
    {
        struct Staged {
            std::unique_ptr<WaterSystem> water;
            std::unique_ptr<FlowMapGenerator> flowMap;
            std::unique_ptr<WaterDisplacement> displacement;
            std::unique_ptr<FoamBuffer> foam;
            std::unique_ptr<SSRSystem> ssr;
            std::unique_ptr<WaterTileCull> tileCull;
            std::unique_ptr<WaterGBuffer> gBuffer;
            std::unique_ptr<OceanFFT> oceanFFT;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "water";
        task.displayName = "Water systems";
        task.dependencies = {"vegetation", "atmosphere"};
        task.weight = 0.1f;
        task.cpuWork = [&r, ctxPtr = makeTaskContext(), staged]() -> bool {
            if (r.progressCallback_) r.progressCallback_(0.75f, "Water systems");
            INIT_PROFILE_PHASE("WaterSystems");

            CoreResources core = CoreResources::collect(
                r.systems_->postProcess(), r.systems_->shadow(), r.systems_->terrain(), Renderer::MAX_FRAMES_IN_FLIGHT);

            WaterSystemGroup::CreateDeps waterDeps{
                *ctxPtr,
                core.hdr.renderPass,
                65536.0f,  // waterSize - extend well beyond terrain for horizon
                r.resourcePath
            };

            auto waterBundle = WaterSystemGroup::createAll(waterDeps);
            if (!waterBundle) return false;

            staged->water = std::move(waterBundle->system);
            staged->flowMap = std::move(waterBundle->flowMap);
            staged->displacement = std::move(waterBundle->displacement);
            staged->foam = std::move(waterBundle->foam);
            staged->ssr = std::move(waterBundle->ssr);
            staged->tileCull = std::move(waterBundle->tileCull);
            staged->gBuffer = std::move(waterBundle->gBuffer);
            staged->oceanFFT = std::move(waterBundle->oceanFFT);
            return true;
        };
        task.gpuWork = [&r, staged]() -> bool {
            r.systems_->setWater(std::move(staged->water));
            r.systems_->setFlowMap(std::move(staged->flowMap));
            r.systems_->setWaterDisplacement(std::move(staged->displacement));
            r.systems_->setFoam(std::move(staged->foam));
            r.systems_->setSSR(std::move(staged->ssr));
            if (staged->tileCull) r.systems_->setWaterTileCull(std::move(staged->tileCull));
            if (staged->gBuffer) r.systems_->setWaterGBuffer(std::move(staged->gBuffer));
            if (staged->oceanFFT) r.systems_->setOceanFFT(std::move(staged->oceanFFT));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Finalization (Tier 5) ==========
    // Water configuration, wiring, geometry, culling, debug, profiler, roads, UBO builder
    {
        struct Staged {
            std::unique_ptr<ScreenSpaceShadowSystem> screenShadow;
            std::unique_ptr<DeferredTerrainObjects> deferredObjects;
            std::unique_ptr<CatmullClarkSystem> catmullClark;
            std::unique_ptr<HiZSystem> hiZ;
            std::unique_ptr<GPUSceneBuffer> gpuSceneBuffer;
            std::unique_ptr<GPUCullPass> gpuCullPass;
            std::unique_ptr<ShadowCullPass> shadowCullPass;
            std::unique_ptr<Profiler> profiler;
            std::unique_ptr<DebugLineSystem> debugLine;
        };
        auto staged = std::make_shared<Staged>();

        Loading::SystemInitTask task;
        task.id = "finalize";
        task.displayName = "Finalizing systems";
        task.dependencies = {"water"};
        task.weight = 0.15f;
        task.cpuWork = [&r, ctxPtr = makeTaskContext(), sceneOrigin, staged]() -> bool {
            if (r.progressCallback_) r.progressCallback_(0.85f, "Finalizing systems");

            vk::Device device = r.vulkanContext_->getVkDevice();
            CoreResources core = CoreResources::collect(
                r.systems_->postProcess(), r.systems_->shadow(), r.systems_->terrain(), Renderer::MAX_FRAMES_IN_FLIGHT);

            // Configure water subsystems (registered by the water task's gpuWork)
            {
                INIT_PROFILE_PHASE("WaterConfigure");
                TerrainFactory::Config terrainFactoryConfig{};
                terrainFactoryConfig.resourcePath = r.resourcePath;
                TerrainConfig terrainConfig = TerrainFactory::buildTerrainConfig(terrainFactoryConfig);

                if (!WaterSystemGroup::configureSubsystems(*r.systems_, terrainConfig)) return false;
                if (!WaterSystemGroup::createDescriptorSets(*r.systems_,
                        r.systems_->globalBuffers().uniformBuffers.buffers,
                        sizeof(UniformBufferObject), r.systems_->shadow(), r.systems_->terrain(),
                        r.systems_->postProcess(), r.vulkanContext_->getDepthSampler())) return false;
            }

            // Screen-space shadow resolve (pre-compute shadows into buffer)
            // Must be created BEFORE descriptor wiring so terrain/grass/material
            // descriptors bind the real shadow buffer instead of placeholders.
            {
                staged->screenShadow = ScreenSpaceShadowSystem::create(*ctxPtr);
                if (staged->screenShadow) {
                    staged->screenShadow->setDepthSource(core.hdr.depthView, r.vulkanContext_->getDepthSampler());
                    staged->screenShadow->setShadowMapSource(
                        static_cast<vk::ImageView>(r.systems_->shadow().getShadowImageView()),
                        static_cast<vk::Sampler>(r.systems_->shadow().getShadowSampler()));
                    SDL_Log("ScreenSpaceShadowSystem: Initialized for shadow buffer optimization");
                }
            }
            // Raw pointer for bindings built below: stable across the move into
            // the registry, and also used by the runtime-invoked bindings lambda.
            ScreenSpaceShadowSystem* screenShadow = staged->screenShadow.get();

            // Cross-system descriptor set wiring. SystemWiring looks the screen
            // shadow buffer up in the registry, which does not hold it until this
            // task's gpuWork, so hand it to terrain and grass here (both setters
            // only store the handles for their descriptor updates).
            SystemWiring wiring(device, Renderer::MAX_FRAMES_IN_FLIGHT);
            if (screenShadow) {
                r.systems_->terrain().setScreenShadowBuffer(
                    screenShadow->getShadowBufferView(), screenShadow->getShadowBufferSampler());
                r.systems_->grass().setScreenShadowBuffer(
                    screenShadow->getShadowBufferView(), screenShadow->getShadowBufferSampler());
            }
            wiring.wireTerrainDescriptors(*r.systems_);

            // Deferred terrain objects (trees, detritus - generated on first frame)
            {
                DeferredTerrainObjects::Config deferredConfig;
                deferredConfig.resourcePath = r.resourcePath;
                deferredConfig.terrainSize = core.terrain.size;
                deferredConfig.getTerrainHeight = core.terrain.getHeightAt;
                deferredConfig.sceneOrigin = sceneOrigin;
                deferredConfig.forestCenter = glm::vec2(sceneOrigin.x + 200.0f, sceneOrigin.y + 100.0f);
                deferredConfig.forestRadius = 80.0f;
                deferredConfig.maxTrees = 500;
                // Biome-driven vegetation: registry objects are loaded later in
                // init but generation only runs after the first frame
                deferredConfig.biomeMap = &r.systems_->biomeMap();
                deferredConfig.settlements = &r.systems_->settlements().settlements();
                // Cover the whole roamable area between settlements. The
                // candidate pass spreads the budget evenly over the region,
                // so a bigger radius trades local density for coverage;
                // island-wide full density needs streamed/instanced trees
                // (WORLD_GENERATION_PLAN.md Phase 5 follow-up).
                deferredConfig.biomeRegionRadius = 5000.0f;
                deferredConfig.maxBiomeTrees = 12000;
                deferredConfig.uniformBuffers = r.systems_->globalBuffers().uniformBuffers.buffers;
                deferredConfig.shadowView = r.systems_->shadow().getShadowImageView();
                deferredConfig.shadowSampler = r.systems_->shadow().getShadowSampler();
                deferredConfig.device = device;
                deferredConfig.allocator = r.vulkanContext_->getAllocator();
                deferredConfig.commandPool = r.vulkanContext_->getCommandPool();
                deferredConfig.graphicsQueue = r.vulkanContext_->getVkGraphicsQueue();
                deferredConfig.physicalDevice = r.vulkanContext_->getVkPhysicalDevice();
                deferredConfig.descriptorPool = r.getDescriptorPool();
                deferredConfig.descriptorSetLayout = r.scenePipeline_.getVkDescriptorSetLayout();
                deferredConfig.framesInFlight = Renderer::MAX_FRAMES_IN_FLIGHT;

                staged->deferredObjects = DeferredTerrainObjects::create(deferredConfig);
            }

            // Common bindings for descriptor sets
            auto getCommonBindings = [&r, screenShadow](uint32_t frameIndex) -> MaterialDescriptorFactory::CommonBindings {
                MaterialDescriptorFactory::CommonBindings common{};
                common.uniformBuffer = r.systems_->globalBuffers().uniformBuffers.buffers[frameIndex];
                common.uniformBufferSize = sizeof(UniformBufferObject);
                common.shadowMapView = r.systems_->shadow().getShadowImageView();
                common.shadowMapSampler = r.systems_->shadow().getShadowSampler();
                common.lightBuffer = r.systems_->globalBuffers().lightBuffers.buffers[frameIndex];
                common.lightBufferSize = sizeof(LightBuffer);
                common.emissiveMapView = r.systems_->scene().getSceneBuilder().getDefaultEmissiveMap()->getImageView();
                common.emissiveMapSampler = r.systems_->scene().getSceneBuilder().getDefaultEmissiveMap()->getSampler();
                common.pointShadowView = r.systems_->shadow().getPointShadowArrayView(frameIndex);
                common.pointShadowSampler = r.systems_->shadow().getPointShadowSampler();
                common.spotShadowView = r.systems_->shadow().getSpotShadowArrayView(frameIndex);
                common.spotShadowSampler = r.systems_->shadow().getSpotShadowSampler();
                common.snowMaskView = r.systems_->snowMask().getSnowMaskView();
                common.snowMaskSampler = r.systems_->snowMask().getSnowMaskSampler();
                common.placeholderTextureView = r.systems_->scene().getSceneBuilder().getWhiteTexture()->getImageView();
                common.placeholderTextureSampler = r.systems_->scene().getSceneBuilder().getWhiteTexture()->getSampler();
                if (screenShadow) {
                    common.screenShadowView = screenShadow->getShadowBufferView();
                    common.screenShadowSampler = screenShadow->getShadowBufferSampler();
                }
                return common;
            };

            // Rocks descriptor sets
            if (!r.systems_->rocks().createDescriptorSets(
                    device, *r.getDescriptorPool(),
                    r.scenePipeline_.getVkDescriptorSetLayout(),
                    Renderer::MAX_FRAMES_IN_FLIGHT, getCommonBindings)) {
                return false;
            }

            if (staged->deferredObjects) {
                staged->deferredObjects->setCommonBindingsFunc(getCommonBindings);
            }

            // Wire all remaining cross-system descriptors
            wiring.wireSnowSystems(*r.systems_);
            wiring.wireLeafDescriptors(*r.systems_);
            wiring.wireWeatherDescriptors(*r.systems_);
            wiring.wireGrassDescriptors(*r.systems_);
            wiring.wireFroxelToWeather(*r.systems_);
            wiring.wireCloudShadowToTerrain(*r.systems_);
            wiring.wireCloudShadowBindings(*r.systems_);

            // Geometry systems
            {
                INIT_PROFILE_PHASE("GeometrySubsystems");
                GeometrySystemGroup::CreateDeps geomDeps{
                    *ctxPtr,
                    core.hdr.renderPass,
                    r.systems_->globalBuffers().uniformBuffers.buffers,
                    r.resourcePath,
                    core.terrain.getHeightAt
                };
                auto geomBundle = GeometrySystemGroup::createAll(geomDeps);
                if (!geomBundle) return false;
                staged->catmullClark = std::move(geomBundle->catmullClark);
            }

            // Sky descriptor sets
            if (!r.systems_->sky().createDescriptorSets(
                    r.systems_->globalBuffers().uniformBuffers.buffers,
                    sizeof(UniformBufferObject), r.systems_->atmosphereLUT())) return false;

            // Hi-Z occlusion culling. Required: the post/compute passes,
            // DebugControlSubsystem and the resize coordinator all take the
            // system by reference, so init fails when it cannot be created.
            staged->hiZ = HiZSystem::create(*ctxPtr, r.vulkanContext_->getDepthFormat());
            if (!staged->hiZ) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create HiZSystem");
                return false;
            }
            staged->hiZ->setDepthBuffer(core.hdr.depthView, r.vulkanContext_->getDepthSampler());
            {
                // Scene objects are deferred and do not exist at bootstrap; only rocks are
                // present. Per-frame Hi-Z culling of scene objects runs off the ECS-sourced
                // GPUSceneBuffer path, so no scene list is gathered here.
                const std::vector<ecs::RenderData> noSceneObjects;
                staged->hiZ->gatherObjects(
                    noSceneObjects,
                    r.systems_->rocks().getSceneObjects());
            }

            // GPU scene buffer for GPU-driven rendering
            {
                auto gpuSceneBuffer = std::make_unique<GPUSceneBuffer>();
                if (gpuSceneBuffer->init(r.vulkanContext_->getAllocator(), Renderer::MAX_FRAMES_IN_FLIGHT)) {
                    staged->gpuSceneBuffer = std::move(gpuSceneBuffer);
                    SDL_Log("GPUSceneBuffer: Initialized for GPU-driven rendering");
                }
            }

            // GPU culling pass
            if (staged->gpuSceneBuffer) {
                GPUCullPass::InitInfo cullInfo{};
                cullInfo.device = device;
                cullInfo.raiiDevice = &r.vulkanContext_->getRaiiDevice();
                cullInfo.allocator = r.vulkanContext_->getAllocator();
                cullInfo.shaderPath = r.resourcePath + "/shaders";
                cullInfo.framesInFlight = Renderer::MAX_FRAMES_IN_FLIGHT;
                cullInfo.descriptorPool = r.getDescriptorPool();

                auto gpuCullPass = GPUCullPass::create(cullInfo);
                if (gpuCullPass) {
                    if (staged->hiZ->getHiZPyramidView()) {
                        gpuCullPass->setHiZPyramid(
                            staged->hiZ->getHiZPyramidView(),
                            staged->hiZ->getHiZSampler());
                    }
                    const auto* whiteTexture = r.systems_->scene().getSceneBuilder().getWhiteTexture();
                    if (whiteTexture) {
                        gpuCullPass->setPlaceholderImage(
                            whiteTexture->getImageView(),
                            whiteTexture->getSampler());
                    }
                    staged->gpuCullPass = std::move(gpuCullPass);
                    SDL_Log("GPUCullPass: Initialized for frustum culling");
                }
            }

            // Per-cascade shadow culling pass + GPU-driven indirect shadow draw resources.
            // Reuses the scene cull-object buffer (shared with the color pass) and the
            // GPUSceneBuffer instance buffer; produces one indirect command buffer per cascade.
            if (staged->gpuSceneBuffer) {
                ShadowCullPass::InitInfo shadowCullInfo{};
                shadowCullInfo.device = device;
                shadowCullInfo.raiiDevice = &r.vulkanContext_->getRaiiDevice();
                shadowCullInfo.allocator = r.vulkanContext_->getAllocator();
                shadowCullInfo.shaderPath = r.resourcePath + "/shaders";
                shadowCullInfo.framesInFlight = Renderer::MAX_FRAMES_IN_FLIGHT;
                shadowCullInfo.cascadeCount = NUM_SHADOW_CASCADES;
                shadowCullInfo.descriptorPool = r.getDescriptorPool();

                auto shadowCullPass = ShadowCullPass::create(shadowCullInfo);
                if (shadowCullPass) {
                    const auto* whiteTexture = r.systems_->scene().getSceneBuilder().getWhiteTexture();
                    if (whiteTexture) {
                        shadowCullPass->setPlaceholderImage(
                            whiteTexture->getImageView(), whiteTexture->getSampler());
                    }
                    // Write the cull descriptor sets once (buffers are stable for their lifetime).
                    shadowCullPass->prepareDescriptors(staged->gpuSceneBuffer.get());
                    staged->shadowCullPass = std::move(shadowCullPass);

                    // Build the indirect shadow pipeline + instance descriptor sets.
                    if (!r.systems_->shadow().initIndirectShadowPath(*staged->gpuSceneBuffer)) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "ShadowSystem: indirect shadow path unavailable; using instanced path");
                    }
                }
            }

            // Instance descriptor sets (set 1) for the instanced indirect draw pipeline.
            // Created here because it needs the GPUSceneBuffer's instance buffers.
            if (staged->gpuSceneBuffer && r.getDescriptorPool()) {
                r.instancedScenePipeline_.createInstanceDescriptorSets(
                    device, *r.getDescriptorPool(), *staged->gpuSceneBuffer, Renderer::MAX_FRAMES_IN_FLIGHT);
            }

            // Profiler
            staged->profiler = Profiler::create(r.vulkanContext_->getRaiiDevice(), r.vulkanContext_->getVkPhysicalDevice(), Renderer::MAX_FRAMES_IN_FLIGHT);

            // Wire caustics (after water is fully initialized)
            wiring.wireCausticsToTerrain(*r.systems_);

            // FrameExecutor (owns sync objects / TripleBuffering)
            r.frameExecutor_ = FrameExecutor::create(*r.vulkanContext_, Renderer::MAX_FRAMES_IN_FLIGHT);
            if (!r.frameExecutor_) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize FrameExecutor");
                return false;
            }

            // Debug line system
            staged->debugLine = DebugLineSystem::create(*ctxPtr, core.hdr.renderPass);
            if (!staged->debugLine) return false;

            // Road/river data
            {
                std::string terrainDataPath = r.resourcePath + "/terrain_data";
                std::string roadsSubdir = terrainDataPath + "/roads";
                std::string roadsPath = roadsSubdir + "/roads.geojson";
                std::string roadsPathAlt = terrainDataPath + "/roads.geojson";

                if (r.systems_->roadData().loadFromGeoJson(roadsPath)) {
                    SDL_Log("Loaded road network from %s", roadsPath.c_str());
                } else if (r.systems_->roadData().loadFromGeoJson(roadsPathAlt)) {
                    SDL_Log("Loaded road network from %s", roadsPathAlt.c_str());
                }

                std::string settlementsPath =
                    SettlementRegistry::getSettlementsPath(terrainDataPath + "/biome");
                r.systems_->settlements().loadFromJson(settlementsPath);
                r.systems_->settlements().applyTownExtents(terrainDataPath + "/towns");

                std::string biomeMapPath =
                    BiomeMap::getBiomeMapPath(terrainDataPath + "/biome");
                r.systems_->biomeMap().loadFromPng(biomeMapPath, core.terrain.size);

                std::string watershedPath = terrainDataPath + "/watershed";
                ErosionLoadConfig erosionConfig{};
                erosionConfig.cacheDirectory = watershedPath;
                erosionConfig.seaLevel = 0.0f;
                if (r.systems_->erosionData().loadFromCache(erosionConfig)) {
                    SDL_Log("Loaded water placement data from %s", watershedPath.c_str());
                }

                auto& vis = r.systems_->roadRiverVis();
                vis.setWaterData(&r.systems_->erosionData().getWaterData());
                vis.setRoadNetwork(&r.systems_->roadData().getRoadNetwork());
                vis.setSettlements(&r.systems_->settlements().settlements());
                vis.setTerrainTileCache(r.systems_->terrain().getTileCache());

                RoadRiverVisConfig visConfig{};
                visConfig.showRivers = true;
                visConfig.showRoads = true;
                visConfig.coneRadius = 0.5f;
                visConfig.coneLength = 2.0f;
                visConfig.heightAboveGround = 1.0f;
                visConfig.riverConeSpacing = 50.0f;
                visConfig.roadConeSpacing = 50.0f;
                vis.setConfig(visConfig);
            }

            // UBO builder
            UBOBuilder::Systems uboSystems{};
            uboSystems.timeSystem = &r.systems_->time();
            uboSystems.celestialCalculator = &r.systems_->celestial();
            uboSystems.shadowSystem = &r.systems_->shadow();
            uboSystems.windSystem = &r.systems_->wind();
            uboSystems.atmosphereLUTSystem = &r.systems_->atmosphereLUT();
            uboSystems.froxelSystem = &r.systems_->froxel();
            uboSystems.sceneManager = &r.systems_->scene();
            uboSystems.snowMaskSystem = &r.systems_->snowMask();
            uboSystems.volumetricSnowSystem = &r.systems_->volumetricSnow();
            uboSystems.cloudShadowSystem = &r.systems_->cloudShadow();
            uboSystems.environmentSettings = &r.systems_->environmentSettings();
            r.systems_->uboBuilder().setSystems(uboSystems);

            if (r.progressCallback_) r.progressCallback_(0.95f, "Systems ready");
            return true;
        };
        task.gpuWork = [&r, staged]() -> bool {
            if (staged->screenShadow) r.systems_->setScreenSpaceShadow(std::move(staged->screenShadow));
            if (staged->deferredObjects) r.systems_->setDeferredTerrainObjects(std::move(staged->deferredObjects));
            r.systems_->setCatmullClark(std::move(staged->catmullClark));
            r.systems_->setHiZ(std::move(staged->hiZ));
            if (staged->gpuSceneBuffer) r.systems_->setGPUSceneBuffer(std::move(staged->gpuSceneBuffer));
            if (staged->gpuCullPass) r.systems_->setGPUCullPass(std::move(staged->gpuCullPass));
            if (staged->shadowCullPass) r.systems_->setShadowCullPass(std::move(staged->shadowCullPass));
            r.systems_->setProfiler(std::move(staged->profiler));
            r.systems_->setDebugLineSystem(std::move(staged->debugLine));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    return tasks;
}

void RendererBuilder::initResizeCoordinator(Renderer& r) {
    r.resizeCoordinator_ = std::make_unique<ResizeCoordinator>();

    // Register systems with resize coordinator
    // Order matters: render targets first, then systems that depend on them, then viewport-only

    // Render targets that need resize (reallocate GPU resources)
    r.resizeCoordinator_->registerWithSimpleResize(r.systems_->postProcess(), "PostProcessSystem", ResizePriority::RenderTarget);
    r.resizeCoordinator_->registerWithSimpleResize(r.systems_->bloom(), "BloomSystem", ResizePriority::RenderTarget);
    if (r.systems_->hasGodRays()) {
        r.resizeCoordinator_->registerWithSimpleResize(r.systems_->godRays(), "GodRaysSystem", ResizePriority::RenderTarget);
    }
    r.resizeCoordinator_->registerWithSimpleResize(r.systems_->froxel(), "FroxelSystem", ResizePriority::RenderTarget);

    // Culling systems with simple resize (extent only, but reallocates)
    r.resizeCoordinator_->registerWithSimpleResize(r.systems_->hiZ(), "HiZSystem", ResizePriority::Culling);
    r.resizeCoordinator_->registerWithSimpleResize(r.systems_->ssr(), "SSRSystem", ResizePriority::Culling);
    r.resizeCoordinator_->registerWithSimpleResize(r.systems_->waterTileCull(), "WaterTileCull", ResizePriority::Culling);

    // G-buffer systems
    r.resizeCoordinator_->registerWithSimpleResize(r.systems_->waterGBuffer(), "WaterGBuffer", ResizePriority::GBuffer);

    // Viewport-only systems (setExtent)
    r.resizeCoordinator_->registerWithExtent(r.systems_->terrain(), "TerrainSystem");
    r.resizeCoordinator_->registerWithExtent(r.systems_->sky(), "SkySystem");
    r.resizeCoordinator_->registerWithExtent(r.systems_->water(), "WaterSystem");
    r.resizeCoordinator_->registerWithExtent(r.systems_->grass(), "GrassSystem");
    r.resizeCoordinator_->registerWithExtent(r.systems_->weather(), "WeatherSystem");
    r.resizeCoordinator_->registerWithExtent(r.systems_->leaf(), "LeafSystem");
    r.resizeCoordinator_->registerWithExtent(r.systems_->catmullClark(), "CatmullClarkSystem");
    r.resizeCoordinator_->registerWithExtent(r.systems_->skinnedMesh(), "SkinnedMeshRenderer");

    // Register callback for bloom texture rebinding (needed after bloom resize)
    r.resizeCoordinator_->registerCallback("BloomRebind",
        [&r](VkExtent2D) {
            r.systems_->postProcess().setBloomTexture(r.systems_->bloom().getBloomOutput(), r.systems_->bloom().getBloomSampler());
        },
        nullptr,
        ResizePriority::RenderTarget);

    // Register callback for god rays texture rebinding (needed after god rays resize)
    if (r.systems_->hasGodRays()) {
        r.resizeCoordinator_->registerCallback("GodRaysRebind",
            [&r](VkExtent2D) {
                r.systems_->postProcess().setGodRaysTexture(
                    r.systems_->godRays().getGodRaysOutput(), r.systems_->godRays().getSampler());
            },
            nullptr,
            ResizePriority::RenderTarget);
    }

    // Register core resize handler for swapchain, depth buffer, and framebuffers
    r.resizeCoordinator_->setCoreResizeHandler([&r](vk::Device, VmaAllocator) -> VkExtent2D {
        // Recreate swapchain
        if (!r.vulkanContext_->recreateSwapchain()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate swapchain");
            return {0, 0};
        }

        VkExtent2D newExtent = r.vulkanContext_->getVkSwapchainExtent();

        // Handle minimized window (extent = 0)
        if (newExtent.width == 0 || newExtent.height == 0) {
            return {0, 0};
        }

        // Recreate swapchain-dependent resources (depth buffer and framebuffers)
        if (!r.vulkanContext_->recreateSwapchainResources()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate swapchain resources during resize");
            return {0, 0};
        }

        return newExtent;
    });

    SDL_Log("Resize coordinator configured with %zu systems", 17UL);
}

void RendererBuilder::initControlSubsystems(Renderer& r) {
    // Initialize control subsystems in RendererSystems
    // These subsystems implement GUI-facing interfaces directly
    r.systems_->initControlSubsystems(*r.vulkanContext_, r.perfToggles);
}

void RendererBuilder::initTemporalSystems(Renderer& r) {
    // Register all systems that implement ITemporalSystem
    // These systems have temporal state (history buffers, ping-pong buffers, frame counters)
    // that needs to be reset when the window regains focus to prevent ghost frames

    // SSR - has temporal filtering with 90% previous frame blend
    r.systems_->registerTemporalSystem(&r.systems_->ssr());

    // Froxel - has temporal reprojection for volumetric fog
    if (r.systems_->hasFroxel()) {
        r.systems_->registerTemporalSystem(&r.systems_->froxel());
    }

    // Water systems with temporal state
    r.systems_->registerTemporalSystem(&r.systems_->foam());
    r.systems_->registerTemporalSystem(&r.systems_->waterDisplacement());

    // Vegetation systems with temporal state
    if (r.systems_->impostorCull()) {
        r.systems_->registerTemporalSystem(r.systems_->impostorCull());
    }

    SDL_Log("Registered %zu temporal systems for ghost frame prevention",
            r.systems_->getTemporalSystemCount());
}
