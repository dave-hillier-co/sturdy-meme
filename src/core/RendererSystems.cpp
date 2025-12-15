// RendererSystems.cpp - Subsystem lifecycle management
// Groups all rendering subsystems with automatic lifecycle via unique_ptr

#include "RendererSystems.h"
#include "RendererInit.h"
#include "CoreResources.h"

// Include all subsystem headers
#include "SkySystem.h"
#include "GrassSystem.h"
#include "WindSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "TerrainSystem.h"
#include "CatmullClarkSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "RockSystem.h"
#include "CloudShadowSystem.h"
#include "HiZSystem.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "SSRSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "ErosionDataLoader.h"
#include "TreeEditSystem.h"
#include "UBOBuilder.h"
#include "Profiler.h"
#include "DebugLineSystem.h"
#include "ResizeCoordinator.h"
#include "ShadowSystem.h"
#include "SceneManager.h"
#include "GlobalBufferManager.h"
#include "SkinnedMeshRenderer.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#include <SDL3/SDL_log.h>

RendererSystems::RendererSystems()
    // Tier 1
    : postProcessSystem_(std::make_unique<PostProcessSystem>())
    , bloomSystem_(std::make_unique<BloomSystem>())
    , shadowSystem_(std::make_unique<ShadowSystem>())
    , terrainSystem_(std::make_unique<TerrainSystem>())
    // Tier 2 - Sky/Atmosphere
    , skySystem_(std::make_unique<SkySystem>())
    , atmosphereLUTSystem_(std::make_unique<AtmosphereLUTSystem>())
    , froxelSystem_(std::make_unique<FroxelSystem>())
    , cloudShadowSystem_(std::make_unique<CloudShadowSystem>())
    // Tier 2 - Environment
    , grassSystem_(std::make_unique<GrassSystem>())
    , windSystem_(std::make_unique<WindSystem>())
    , weatherSystem_(std::make_unique<WeatherSystem>())
    , leafSystem_(std::make_unique<LeafSystem>())
    // Tier 2 - Snow
    , snowMaskSystem_(std::make_unique<SnowMaskSystem>())
    , volumetricSnowSystem_(std::make_unique<VolumetricSnowSystem>())
    // Tier 2 - Water
    , waterSystem_(std::make_unique<WaterSystem>())
    , waterDisplacement_(std::make_unique<WaterDisplacement>())
    , flowMapGenerator_(std::make_unique<FlowMapGenerator>())
    , foamBuffer_(std::make_unique<FoamBuffer>())
    , ssrSystem_(std::make_unique<SSRSystem>())
    , waterTileCull_(std::make_unique<WaterTileCull>())
    , waterGBuffer_(std::make_unique<WaterGBuffer>())
    // Tier 2 - Geometry
    , catmullClarkSystem_(std::make_unique<CatmullClarkSystem>())
    , rockSystem_(std::make_unique<RockSystem>())
    // Tier 2 - Culling
    , hiZSystem_(std::make_unique<HiZSystem>())
    // Infrastructure
    , sceneManager_(std::make_unique<SceneManager>())
    , globalBufferManager_(std::make_unique<GlobalBufferManager>())
    , erosionDataLoader_(std::make_unique<ErosionDataLoader>())
    , skinnedMeshRenderer_(std::make_unique<SkinnedMeshRenderer>())
    // Tools
    , treeEditSystem_(std::make_unique<TreeEditSystem>())
    , debugLineSystem_(std::make_unique<DebugLineSystem>())
    , profiler_(std::make_unique<Profiler>())
    // Coordination
    , resizeCoordinator_(std::make_unique<ResizeCoordinator>())
    , uboBuilder_(std::make_unique<UBOBuilder>())
    // Time
    , timeSystem_(std::make_unique<TimeSystem>())
    , celestialCalculator_(std::make_unique<CelestialCalculator>())
    , environmentSettings_(std::make_unique<EnvironmentSettings>())
{
}

RendererSystems::~RendererSystems() {
    // unique_ptrs handle destruction automatically in reverse order
    // No manual cleanup needed - this is the benefit of RAII
}

bool RendererSystems::init(const InitContext& initCtx,
                            VkRenderPass swapchainRenderPass,
                            VkFormat swapchainImageFormat,
                            VkDescriptorSetLayout mainDescriptorSetLayout,
                            VkFormat depthFormat,
                            VkSampler depthSampler,
                            const std::string& resourcePath) {
    VkDevice device = initCtx.device;
    VmaAllocator allocator = initCtx.allocator;
    VkPhysicalDevice physicalDevice = initCtx.physicalDevice;
    VkQueue graphicsQueue = initCtx.graphicsQueue;
    uint32_t framesInFlight = initCtx.framesInFlight;

    // ========================================================================
    // Phase 1: Tier-1 systems (PostProcess, Bloom, Shadow, Terrain)
    // ========================================================================

    // Initialize post-processing systems
    if (!RendererInit::initPostProcessing(*postProcessSystem_, *bloomSystem_, initCtx,
                                          swapchainRenderPass, swapchainImageFormat)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize post-processing");
        return false;
    }

    // Initialize skinned mesh rendering (GPU skinning for animated characters)
    if (!skinnedMeshRenderer_->init(initCtx, postProcessSystem_->getHDRRenderPass())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize skinned mesh renderer");
        return false;
    }

    // Initialize sky system
    if (!skySystem_->init(initCtx, postProcessSystem_->getHDRRenderPass())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize sky system");
        return false;
    }

    // Initialize global buffer manager for per-frame shared buffers
    if (!globalBufferManager_->init(allocator, framesInFlight)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GlobalBufferManager");
        return false;
    }

    // Initialize light buffers with empty data
    for (uint32_t i = 0; i < framesInFlight; i++) {
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        globalBufferManager_->updateLightBuffer(i, emptyBuffer);
    }

    // Initialize shadow system
    if (!shadowSystem_->init(initCtx, mainDescriptorSetLayout,
                              skinnedMeshRenderer_->getDescriptorSetLayout())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize shadow system");
        return false;
    }

    // Initialize terrain system
    std::string heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";

    TerrainSystem::TerrainInitParams terrainParams{};
    terrainParams.renderPass = postProcessSystem_->getHDRRenderPass();
    terrainParams.shadowRenderPass = shadowSystem_->getShadowRenderPass();
    terrainParams.shadowMapSize = shadowSystem_->getShadowMapSize();
    terrainParams.texturePath = resourcePath + "/textures";

    TerrainConfig terrainConfig{};
    terrainConfig.size = 16384.0f;
    terrainConfig.maxDepth = 20;
    terrainConfig.minDepth = 5;
    terrainConfig.targetEdgePixels = 16.0f;
    terrainConfig.splitThreshold = 100.0f;
    terrainConfig.mergeThreshold = 50.0f;
    terrainConfig.heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";
    terrainConfig.minAltitude = -15.0f;
    terrainConfig.maxAltitude = 220.0f;
    terrainConfig.tileCacheDir = resourcePath + "/terrain_data";
    terrainConfig.tileLoadRadius = 2000.0f;
    terrainConfig.tileUnloadRadius = 3000.0f;

    if (!terrainSystem_->init(initCtx, terrainParams, terrainConfig)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize terrain system");
        return false;
    }

    // ========================================================================
    // Collect CoreResources from Tier-1 for Tier-2+ systems
    // ========================================================================
    CoreResources core = getCoreResources(framesInFlight);

    // ========================================================================
    // Phase 2: Scene setup
    // ========================================================================

    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = allocator;
    sceneInfo.device = device;
    sceneInfo.commandPool = initCtx.commandPool;
    sceneInfo.graphicsQueue = graphicsQueue;
    sceneInfo.physicalDevice = physicalDevice;
    sceneInfo.resourcePath = resourcePath;
    sceneInfo.getTerrainHeight = [this](float x, float z) {
        return terrainSystem_->getHeightAt(x, z);
    };

    if (!sceneManager_->init(sceneInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize scene manager");
        return false;
    }

    // ========================================================================
    // Phase 3: Tier-2 systems
    // ========================================================================

    // Snow subsystems
    if (!RendererInit::initSnowSubsystems(*snowMaskSystem_, *volumetricSnowSystem_,
                                          initCtx, core.hdr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize snow subsystems");
        return false;
    }

    // Grass and wind subsystems
    if (!RendererInit::initGrassSubsystem(*grassSystem_, *windSystem_, *leafSystem_,
                                          initCtx, core.hdr, core.shadow)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize grass subsystems");
        return false;
    }

    // Rock system
    if (!RendererInit::initRockSystem(*rockSystem_, initCtx, core.terrain)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize rock system");
        return false;
    }

    // Weather and leaf subsystems
    if (!RendererInit::initWeatherSubsystems(*weatherSystem_, *leafSystem_, initCtx, core.hdr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize weather subsystems");
        return false;
    }

    // Connect snow to environment settings
    const EnvironmentSettings* envSettings = &windSystem_->getEnvironmentSettings();
    snowMaskSystem_->setEnvironmentSettings(envSettings);
    volumetricSnowSystem_->setEnvironmentSettings(envSettings);

    // Atmosphere subsystems
    if (!RendererInit::initAtmosphereSubsystems(*froxelSystem_, *atmosphereLUTSystem_,
                                                 *cloudShadowSystem_, *postProcessSystem_,
                                                 initCtx, core.shadow,
                                                 globalBufferManager_->lightBuffers.buffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize atmosphere subsystems");
        return false;
    }

    // Catmull-Clark subdivision
    float suzanneX = 5.0f, suzanneZ = -5.0f;
    glm::vec3 suzannePos(suzanneX, core.terrain.getHeightAt(suzanneX, suzanneZ) + 2.0f, suzanneZ);
    if (!RendererInit::initCatmullClarkSystem(*catmullClarkSystem_, initCtx, core.hdr, suzannePos)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize Catmull-Clark system");
        return false;
    }

    // Hi-Z occlusion culling (optional)
    if (!hiZSystem_->init(initCtx, depthFormat)) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        // Continue without Hi-Z - it's an optional optimization
    } else {
        hiZSystem_->setDepthBuffer(core.hdr.depthView, depthSampler);
    }

    // Profiler (optional)
    if (!profiler_->init(device, physicalDevice, framesInFlight)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Profiler initialization failed - disabled");
    }

    // Water subsystems
    WaterSubsystems waterSubs{*waterSystem_, *waterDisplacement_, *flowMapGenerator_,
                              *foamBuffer_, *ssrSystem_, *waterTileCull_, *waterGBuffer_};
    if (!RendererInit::initWaterSubsystems(waterSubs, initCtx, core.hdr.renderPass,
                                            *shadowSystem_, *terrainSystem_, terrainConfig,
                                            *postProcessSystem_, depthSampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize water subsystems");
        return false;
    }

    // Tree edit system
    if (!RendererInit::initTreeEditSystem(*treeEditSystem_, initCtx, core.hdr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize tree edit system");
        return false;
    }

    // Debug line system
    if (!RendererInit::initDebugLineSystem(*debugLineSystem_, initCtx, core.hdr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize debug line system");
        return false;
    }

    // ========================================================================
    // Phase 4: Wire up UBOBuilder
    // ========================================================================
    UBOBuilder::Systems uboSystems{};
    uboSystems.timeSystem = timeSystem_.get();
    uboSystems.celestialCalculator = celestialCalculator_.get();
    uboSystems.shadowSystem = shadowSystem_.get();
    uboSystems.windSystem = windSystem_.get();
    uboSystems.atmosphereLUTSystem = atmosphereLUTSystem_.get();
    uboSystems.froxelSystem = froxelSystem_.get();
    uboSystems.sceneManager = sceneManager_.get();
    uboSystems.snowMaskSystem = snowMaskSystem_.get();
    uboSystems.volumetricSnowSystem = volumetricSnowSystem_.get();
    uboSystems.cloudShadowSystem = cloudShadowSystem_.get();
    uboSystems.environmentSettings = environmentSettings_.get();
    uboBuilder_->setSystems(uboSystems);

    initialized_ = true;
    SDL_Log("RendererSystems initialized successfully");
    return true;
}

void RendererSystems::destroy(VkDevice device, VmaAllocator allocator) {
    if (!initialized_) {
        return;
    }

    SDL_Log("RendererSystems::destroy starting");

    // Destroy in reverse dependency order
    // Tier 2+ first, then Tier 1

    debugLineSystem_->shutdown();
    treeEditSystem_->destroy(device, allocator);

    // Water
    waterGBuffer_->destroy();
    waterTileCull_->destroy();
    ssrSystem_->destroy();
    foamBuffer_->destroy();
    flowMapGenerator_->destroy(device, allocator);
    waterDisplacement_->destroy();
    waterSystem_->destroy(device, allocator);

    profiler_->shutdown();
    hiZSystem_->destroy();

    catmullClarkSystem_->destroy(device, allocator);
    rockSystem_->destroy(allocator, device);

    // Atmosphere
    cloudShadowSystem_->destroy();
    atmosphereLUTSystem_->destroy(device, allocator);
    froxelSystem_->destroy(device, allocator);

    // Weather/Snow
    leafSystem_->destroy(device, allocator);
    weatherSystem_->destroy(device, allocator);
    volumetricSnowSystem_->destroy(device, allocator);
    snowMaskSystem_->destroy(device, allocator);

    // Grass/Wind
    windSystem_->destroy(device, allocator);
    grassSystem_->destroy(device, allocator);

    sceneManager_->destroy(allocator, device);
    globalBufferManager_->destroy(allocator);

    // Tier 1
    skySystem_->destroy(device, allocator);
    terrainSystem_->destroy(device, allocator);
    shadowSystem_->destroy();
    skinnedMeshRenderer_->destroy();
    bloomSystem_->destroy(device, allocator);
    postProcessSystem_->destroy(device, allocator);

    initialized_ = false;
    SDL_Log("RendererSystems::destroy complete");
}

CoreResources RendererSystems::getCoreResources(uint32_t framesInFlight) const {
    return CoreResources::collect(*postProcessSystem_, *shadowSystem_,
                                  *terrainSystem_, framesInFlight);
}

#ifdef JPH_DEBUG_RENDERER
void RendererSystems::createPhysicsDebugRenderer(const InitContext& ctx, VkRenderPass hdrRenderPass) {
    physicsDebugRenderer_ = std::make_unique<PhysicsDebugRenderer>();
    if (!physicsDebugRenderer_->init(ctx, hdrRenderPass)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Physics debug renderer init failed");
        physicsDebugRenderer_.reset();
    }
}
#endif
