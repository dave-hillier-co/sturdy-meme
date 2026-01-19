// RendererSystems.cpp - Google Fruit DI-based subsystem management

#include "RendererSystems.h"
#include "CoreResources.h"

// Include all subsystem headers
#include "SkySystem.h"
#include "GrassSystem.h"
#include "WindSystem.h"
#include "DisplacementSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "TerrainSystem.h"
#include "TerrainFactory.h"
#include "CatmullClarkSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "ScatterSystem.h"
#include "scene/SceneMaterial.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DeferredTerrainObjects.h"
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
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "UBOBuilder.h"
#include "Profiler.h"
#include "DebugLineSystem.h"
#include "ResizeCoordinator.h"
#include "ShadowSystem.h"
#include "SceneManager.h"
#include "SceneBuilder.h"
#include "AssetRegistry.h"
#include "DescriptorInfrastructure.h"
#include "GlobalBufferManager.h"
#include "SkinnedMeshRenderer.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "VulkanContext.h"
#include "PerformanceToggles.h"

// Control subsystems
#include "controls/EnvironmentControlSubsystem.h"
#include "controls/WaterControlSubsystem.h"
#include "controls/TreeControlSubsystem.h"
#include "vegetation/GrassControlAdapter.h"
#include "controls/DebugControlSubsystem.h"
#include "controls/PerformanceControlSubsystem.h"
#include "controls/SceneControlSubsystem.h"
#include "controls/PlayerControlSubsystem.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#include "core/vulkan/CommandBufferUtils.h"
#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>

// ============================================================================
// RendererSystems Fruit-Injectable Constructor
// ============================================================================

RendererSystems::RendererSystems(
    PostProcessSystem* postProcess,
    BloomSystem* bloom,
    BilateralGridSystem* bilateralGrid,
    ShadowSystem* shadow,
    TerrainSystem* terrain,
    GlobalBufferManager* globalBuffers,
    SkinnedMeshRenderer* skinnedMesh,
    SkySystem* sky,
    AtmosphereLUTSystem* atmosphereLUT,
    FroxelSystem* froxel,
    CloudShadowSystem* cloudShadow,
    SnowMaskSystem* snowMask,
    VolumetricSnowSystem* volumetricSnow,
    WeatherSystem* weather,
    LeafSystem* leaf,
    WindSystem* wind,
    DisplacementSystem* displacement,
    GrassSystem* grass,
    CatmullClarkSystem* catmullClark,
    HiZSystem* hiZ,
    WaterDisplacement* waterDisplacement,
    FlowMapGenerator* flowMap,
    FoamBuffer* foam,
    SSRSystem* ssr,
    WaterTileCull* waterTileCull,
    WaterGBuffer* waterGBuffer,
    WaterSystem* water,
    SceneManager* scene,
    Profiler* profiler,
    DebugLineSystem* debugLine,
    TimeSystem* time,
    CelestialCalculator* celestial,
    UBOBuilder* uboBuilder,
    ResizeCoordinator* resizeCoordinator,
    ErosionDataLoader* erosionData,
    RoadNetworkLoader* roadData,
    RoadRiverVisualization* roadRiverVis,
    EnvironmentSettings* environmentSettings
) :
    postProcessSystem_(postProcess),
    bloomSystem_(bloom),
    bilateralGridSystem_(bilateralGrid),
    shadowSystem_(shadow),
    terrainSystem_(terrain),
    globalBufferManager_(globalBuffers),
    skinnedMeshRenderer_(skinnedMesh),
    skySystem_(sky),
    atmosphereLUTSystem_(atmosphereLUT),
    froxelSystem_(froxel),
    cloudShadowSystem_(cloudShadow),
    snowMaskSystem_(snowMask),
    volumetricSnowSystem_(volumetricSnow),
    weatherSystem_(weather),
    leafSystem_(leaf),
    windSystem_(wind),
    displacementSystem_(displacement),
    grassSystem_(grass),
    catmullClarkSystem_(catmullClark),
    hiZSystem_(hiZ),
    waterDisplacement_(waterDisplacement),
    flowMapGenerator_(flowMap),
    foamBuffer_(foam),
    ssrSystem_(ssr),
    waterTileCull_(waterTileCull),
    waterGBuffer_(waterGBuffer),
    waterSystem_(water),
    sceneManager_(scene),
    profiler_(profiler),
    debugLineSystem_(debugLine),
    timeSystem_(time),
    celestialCalculator_(celestial),
    uboBuilder_(uboBuilder),
    resizeCoordinator_(resizeCoordinator),
    erosionDataLoader_(erosionData),
    roadNetworkLoader_(roadData),
    roadRiverVisualization_(roadRiverVis),
    environmentSettings_(environmentSettings)
{
    SDL_Log("RendererSystems created via Fruit DI");
}

RendererSystems::~RendererSystems() {
    SDL_Log("RendererSystems destructor called");
}

CoreResources RendererSystems::getCoreResources(uint32_t framesInFlight) const {
    return CoreResources::collect(*postProcessSystem_, *shadowSystem_,
                                  *terrainSystem_, framesInFlight);
}

// ============================================================================
// Late-bound system setters
// ============================================================================

void RendererSystems::setRocks(std::unique_ptr<ScatterSystem> system) {
    if (rocksSystemOwned_) {
        sceneCollection_.unregisterMaterial(&rocksSystemOwned_->getMaterial());
    }
    rocksSystemOwned_ = std::move(system);
    rocksSystem_ = rocksSystemOwned_.get();
    if (rocksSystemOwned_) {
        sceneCollection_.registerMaterial(&rocksSystemOwned_->getMaterial());
    }
}

void RendererSystems::setTree(std::unique_ptr<TreeSystem> system) {
    treeSystemOwned_ = std::move(system);
    treeSystem_ = treeSystemOwned_.get();
}

void RendererSystems::setTreeRenderer(std::unique_ptr<TreeRenderer> renderer) {
    treeRendererOwned_ = std::move(renderer);
    treeRenderer_ = treeRendererOwned_.get();
}

void RendererSystems::setTreeLOD(std::unique_ptr<TreeLODSystem> system) {
    treeLODSystemOwned_ = std::move(system);
    treeLODSystem_ = treeLODSystemOwned_.get();
}

void RendererSystems::setImpostorCull(std::unique_ptr<ImpostorCullSystem> system) {
    impostorCullSystemOwned_ = std::move(system);
    impostorCullSystem_ = impostorCullSystemOwned_.get();
}

void RendererSystems::setDetritus(std::unique_ptr<ScatterSystem> system) {
    if (detritusSystemOwned_) {
        sceneCollection_.unregisterMaterial(&detritusSystemOwned_->getMaterial());
    }
    detritusSystemOwned_ = std::move(system);
    detritusSystem_ = detritusSystemOwned_.get();
    if (detritusSystemOwned_) {
        sceneCollection_.registerMaterial(&detritusSystemOwned_->getMaterial());
    }
}

void RendererSystems::setDeferredTerrainObjects(std::unique_ptr<DeferredTerrainObjects> deferred) {
    deferredTerrainObjectsOwned_ = std::move(deferred);
    deferredTerrainObjects_ = deferredTerrainObjectsOwned_.get();
}

#ifdef JPH_DEBUG_RENDERER
void RendererSystems::createPhysicsDebugRenderer(const InitContext& /*ctx*/, VkRenderPass /*hdrRenderPass*/) {
    physicsDebugRendererOwned_ = std::make_unique<PhysicsDebugRenderer>();
    physicsDebugRendererOwned_->init();
    physicsDebugRenderer_ = physicsDebugRendererOwned_.get();
}
#endif

// ============================================================================
// Control Subsystem Implementation
// ============================================================================

void RendererSystems::initControlSubsystems(VulkanContext& vulkanContext, PerformanceToggles& perfToggles) {
    environmentControl_ = std::make_unique<EnvironmentControlSubsystem>(
        *froxelSystem_, *atmosphereLUTSystem_, *leafSystem_, *cloudShadowSystem_,
        *postProcessSystem_, *environmentSettings_);
    waterControlSubsystem_ = std::make_unique<WaterControlSubsystem>(*waterSystem_, *waterTileCull_);
    treeControlSubsystem_ = std::make_unique<TreeControlSubsystem>(treeSystem_, *this);
    grassControlAdapter_ = std::make_unique<GrassControlAdapter>(*grassSystem_);
    debugControlSubsystem_ = std::make_unique<DebugControlSubsystem>(*debugLineSystem_, *hiZSystem_, *this);
    performanceControlSubsystem_ = std::make_unique<PerformanceControlSubsystem>(perfToggles, nullptr);
    sceneControlSubsystem_ = std::make_unique<SceneControlSubsystem>(*sceneManager_, vulkanContext);
    playerControlSubsystem_ = std::make_unique<PlayerControlSubsystem>(*sceneManager_);

    controlsInitialized_ = true;
    SDL_Log("Control subsystems initialized");
}

void RendererSystems::setPerformanceSyncCallback(std::function<void()> callback) {
    if (performanceControlSubsystem_) {
        performanceControlSubsystem_->setSyncCallback(callback);
    }
}

// Control subsystem accessors
ILocationControl& RendererSystems::locationControl() { return *celestialCalculator_; }
const ILocationControl& RendererSystems::locationControl() const { return *celestialCalculator_; }

IWeatherState& RendererSystems::weatherState() { return *weatherSystem_; }
const IWeatherState& RendererSystems::weatherState() const { return *weatherSystem_; }

IEnvironmentControl& RendererSystems::environmentControl() { return *environmentControl_; }
const IEnvironmentControl& RendererSystems::environmentControl() const { return *environmentControl_; }

IPostProcessState& RendererSystems::postProcessState() { return *postProcessSystem_; }
const IPostProcessState& RendererSystems::postProcessState() const { return *postProcessSystem_; }

ICloudShadowControl& RendererSystems::cloudShadowControl() { return *cloudShadowSystem_; }
const ICloudShadowControl& RendererSystems::cloudShadowControl() const { return *cloudShadowSystem_; }

ITerrainControl& RendererSystems::terrainControl() { return *terrainSystem_; }
const ITerrainControl& RendererSystems::terrainControl() const { return *terrainSystem_; }

IWaterControl& RendererSystems::waterControl() { return *waterControlSubsystem_; }
const IWaterControl& RendererSystems::waterControl() const { return *waterControlSubsystem_; }

ITreeControl& RendererSystems::treeControl() { return *treeControlSubsystem_; }
const ITreeControl& RendererSystems::treeControl() const { return *treeControlSubsystem_; }

IGrassControl& RendererSystems::grassControl() { return *grassControlAdapter_; }
const IGrassControl& RendererSystems::grassControl() const { return *grassControlAdapter_; }

IDebugControl& RendererSystems::debugControl() { return *debugControlSubsystem_; }
const IDebugControl& RendererSystems::debugControl() const { return *debugControlSubsystem_; }
DebugControlSubsystem& RendererSystems::debugControlSubsystem() { return *debugControlSubsystem_; }
const DebugControlSubsystem& RendererSystems::debugControlSubsystem() const { return *debugControlSubsystem_; }

IProfilerControl& RendererSystems::profilerControl() { return *profiler_; }
const IProfilerControl& RendererSystems::profilerControl() const { return *profiler_; }

IPerformanceControl& RendererSystems::performanceControl() { return *performanceControlSubsystem_; }
const IPerformanceControl& RendererSystems::performanceControl() const { return *performanceControlSubsystem_; }

ISceneControl& RendererSystems::sceneControl() { return *sceneControlSubsystem_; }
const ISceneControl& RendererSystems::sceneControl() const { return *sceneControlSubsystem_; }

IPlayerControl& RendererSystems::playerControl() { return *playerControlSubsystem_; }
const IPlayerControl& RendererSystems::playerControl() const { return *playerControlSubsystem_; }

// ============================================================================
// Fruit Component - Individual System Providers
// ============================================================================

fruit::Component<RendererSystems> getRendererSystemsComponent(
    const InitContext& ctx,
    VkRenderPass swapchainRenderPass,
    VkFormat swapchainImageFormat,
    VkDescriptorSetLayout mainLayout,
    VkDescriptorSetLayout skinnedLayout,
    VkFormat depthFormat,
    VkSampler depthSampler,
    const std::string& resourcePath,
    uint32_t framesInFlight,
    AssetRegistry* assetRegistry,
    glm::vec2 sceneOrigin,
    DescriptorManager::Pool* descriptorPool,
    VkExtent2D swapchainExtent,
    vk::raii::Device* raiiDevice
) {
    return fruit::createComponent()
        // Bind configuration values
        .bindInstance(di::SwapchainRenderPass{swapchainRenderPass})
        .bindInstance(di::SwapchainFormat{swapchainImageFormat})
        .bindInstance(di::MainDescriptorSetLayout{mainLayout})
        .bindInstance(di::SkinnedDescriptorSetLayout{skinnedLayout})
        .bindInstance(di::DepthFormat{depthFormat})
        .bindInstance(di::DepthSampler{depthSampler})
        .bindInstance(di::ResourcePath{resourcePath})
        .bindInstance(di::FramesInFlight{framesInFlight})
        .bindInstance(di::SceneOriginConfig{sceneOrigin})
        .bindInstance(di::AssetRegistryPtr{assetRegistry})
        .bindInstance(di::DescriptorPoolPtr{descriptorPool})
        .bindInstance(di::SwapchainExtent{swapchainExtent})
        .bindInstance(di::RaiiDevicePtr{raiiDevice})
        .bindInstance(ctx)

        // PostProcessBundleHolder (creates PostProcess, Bloom, BilateralGrid together)
        .registerProvider([](const InitContext& ctx,
                            const di::SwapchainRenderPass& swapRP,
                            const di::SwapchainFormat& swapFormat) -> di::PostProcessBundleHolder* {
            SDL_Log("FruitDI: Creating PostProcessBundleHolder");
            auto bundle = PostProcessSystem::createWithDependencies(
                ctx, swapRP.renderPass, swapFormat.format);
            if (!bundle) return nullptr;
            auto holder = new di::PostProcessBundleHolder();
            holder->postProcessOwned = std::move(bundle->postProcess);
            holder->bloomOwned = std::move(bundle->bloom);
            holder->bilateralGridOwned = std::move(bundle->bilateralGrid);
            holder->postProcess = holder->postProcessOwned.get();
            holder->bloom = holder->bloomOwned.get();
            holder->bilateralGrid = holder->bilateralGridOwned.get();
            return holder;
        })
        .registerProvider([](di::PostProcessBundleHolder* holder) -> PostProcessSystem* {
            return holder ? holder->postProcess : nullptr;
        })
        .registerProvider([](di::PostProcessBundleHolder* holder) -> BloomSystem* {
            return holder ? holder->bloom : nullptr;
        })
        .registerProvider([](di::PostProcessBundleHolder* holder) -> BilateralGridSystem* {
            return holder ? holder->bilateralGrid : nullptr;
        })
        .registerProvider([](PostProcessSystem* pp) -> di::HDRRenderPass {
            return di::HDRRenderPass{pp ? static_cast<VkRenderPass>(pp->getHDRRenderPass()) : VK_NULL_HANDLE};
        })

        // ShadowSystem
        .registerProvider([](const InitContext& ctx,
                            const di::MainDescriptorSetLayout& mainLayout,
                            const di::SkinnedDescriptorSetLayout& skinnedLayout) -> ShadowSystem* {
            SDL_Log("FruitDI: Creating ShadowSystem");
            auto shadow = ShadowSystem::create(ctx, mainLayout.layout, skinnedLayout.layout);
            return shadow ? shadow.release() : nullptr;
        })
        .registerProvider([](ShadowSystem* shadow) -> di::ShadowRenderPass {
            return di::ShadowRenderPass{shadow ? static_cast<VkRenderPass>(shadow->getShadowRenderPass()) : VK_NULL_HANDLE};
        })

        // GlobalBufferManager
        .registerProvider([](const InitContext& ctx,
                            const di::FramesInFlight& frames) -> GlobalBufferManager* {
            SDL_Log("FruitDI: Creating GlobalBufferManager");
            auto buffers = GlobalBufferManager::create(ctx.allocator, ctx.physicalDevice, frames.count);
            return buffers ? buffers.release() : nullptr;
        })

        // TerrainSystem
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP,
                            const di::ShadowRenderPass& shadowRP,
                            ShadowSystem* shadow,
                            const di::ResourcePath& resourcePath) -> TerrainSystem* {
            SDL_Log("FruitDI: Creating TerrainSystem");
            TerrainFactory::Config config{};
            config.hdrRenderPass = hdrRP.renderPass;
            config.shadowRenderPass = shadowRP.renderPass;
            config.shadowMapSize = shadow ? shadow->getShadowMapSize() : 2048;
            config.resourcePath = resourcePath.path;
            auto terrain = TerrainFactory::create(ctx, config);
            return terrain ? terrain.release() : nullptr;
        })

        // SkinnedMeshRenderer
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP,
                            const di::DescriptorPoolPtr& descPool,
                            const di::SwapchainExtent& extent,
                            const di::RaiiDevicePtr& raiiDev,
                            const di::ResourcePath& resourcePath,
                            const di::FramesInFlight& frames) -> SkinnedMeshRenderer* {
            SDL_Log("FruitDI: Creating SkinnedMeshRenderer");
            SkinnedMeshRenderer::InitInfo info{};
            info.device = ctx.device;
            info.raiiDevice = static_cast<vk::raii::Device*>(raiiDev.device);
            info.allocator = ctx.allocator;
            info.descriptorPool = static_cast<DescriptorManager::Pool*>(descPool.pool);
            info.renderPass = hdrRP.renderPass;
            info.extent = extent.extent;
            info.shaderPath = resourcePath.path + "/shaders";
            info.framesInFlight = frames.count;
            info.addCommonBindings = [](DescriptorManager::LayoutBuilder& builder) {
                DescriptorInfrastructure::addCommonDescriptorBindings(builder);
            };
            auto renderer = SkinnedMeshRenderer::create(info);
            return renderer ? renderer.release() : nullptr;
        })

        // SkySystem
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP) -> SkySystem* {
            SDL_Log("FruitDI: Creating SkySystem");
            auto sky = SkySystem::create(ctx, hdrRP.renderPass);
            return sky ? sky.release() : nullptr;
        })

        // AtmosphereLUTSystem
        .registerProvider([](const InitContext& ctx) -> AtmosphereLUTSystem* {
            SDL_Log("FruitDI: Creating AtmosphereLUTSystem");
            auto lut = AtmosphereLUTSystem::create(ctx);
            if (lut) {
                CommandScope cmdScope(ctx.device, ctx.commandPool, ctx.graphicsQueue);
                if (cmdScope.begin()) {
                    lut->computeTransmittanceLUT(cmdScope.get());
                    lut->computeMultiScatterLUT(cmdScope.get());
                    lut->computeIrradianceLUT(cmdScope.get());
                    glm::vec3 sunDir = glm::vec3(0.0f, 0.707f, 0.707f);
                    lut->computeSkyViewLUT(cmdScope.get(), sunDir, glm::vec3(0.0f), 0.0f);
                    lut->computeCloudMapLUT(cmdScope.get(), glm::vec3(0.0f), 0.0f);
                    cmdScope.end();
                }
                lut->exportLUTsAsPNG(ctx.resourcePath);
            }
            return lut ? lut.release() : nullptr;
        })

        // FroxelSystem
        .registerProvider([](const InitContext& ctx,
                            ShadowSystem* shadow,
                            GlobalBufferManager* globalBuffers) -> FroxelSystem* {
            SDL_Log("FruitDI: Creating FroxelSystem");
            if (!shadow || !globalBuffers) return nullptr;
            auto froxel = FroxelSystem::create(
                ctx,
                static_cast<VkImageView>(shadow->getShadowImageView()),
                static_cast<VkSampler>(shadow->getShadowSampler()),
                globalBuffers->lightBuffers.buffers);
            return froxel ? froxel.release() : nullptr;
        })

        // CloudShadowSystem
        .registerProvider([](const InitContext& ctx,
                            AtmosphereLUTSystem* atmosphereLUT) -> CloudShadowSystem* {
            SDL_Log("FruitDI: Creating CloudShadowSystem");
            if (!atmosphereLUT) return nullptr;
            auto cloudShadow = CloudShadowSystem::create(
                ctx,
                atmosphereLUT->getCloudMapLUTView(),
                atmosphereLUT->getLUTSampler());
            return cloudShadow ? cloudShadow.release() : nullptr;
        })

        // SnowMaskSystem
        .registerProvider([](const InitContext& ctx) -> SnowMaskSystem* {
            SDL_Log("FruitDI: Creating SnowMaskSystem");
            auto snowMask = SnowMaskSystem::create(ctx);
            return snowMask ? snowMask.release() : nullptr;
        })

        // VolumetricSnowSystem
        .registerProvider([](const InitContext& ctx) -> VolumetricSnowSystem* {
            SDL_Log("FruitDI: Creating VolumetricSnowSystem");
            auto volumetricSnow = VolumetricSnowSystem::create(ctx);
            return volumetricSnow ? volumetricSnow.release() : nullptr;
        })

        // WeatherSystem
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP) -> WeatherSystem* {
            SDL_Log("FruitDI: Creating WeatherSystem");
            auto weather = WeatherSystem::create(ctx, hdrRP.renderPass);
            return weather ? weather.release() : nullptr;
        })

        // LeafSystem
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP) -> LeafSystem* {
            SDL_Log("FruitDI: Creating LeafSystem");
            auto leaf = LeafSystem::create(ctx, hdrRP.renderPass);
            return leaf ? leaf.release() : nullptr;
        })

        // WindSystem
        .registerProvider([](const InitContext& ctx) -> WindSystem* {
            SDL_Log("FruitDI: Creating WindSystem");
            auto wind = WindSystem::create(ctx);
            return wind ? wind.release() : nullptr;
        })

        // DisplacementSystem
        .registerProvider([](const InitContext& ctx) -> DisplacementSystem* {
            SDL_Log("FruitDI: Creating DisplacementSystem");
            auto displacement = DisplacementSystem::create(ctx);
            return displacement ? displacement.release() : nullptr;
        })

        // GrassSystem
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP,
                            const di::ShadowRenderPass& shadowRP,
                            ShadowSystem* shadow) -> GrassSystem* {
            SDL_Log("FruitDI: Creating GrassSystem");
            uint32_t shadowMapSize = shadow ? shadow->getShadowMapSize() : 2048;
            auto grass = GrassSystem::create(ctx, hdrRP.renderPass, shadowRP.renderPass, shadowMapSize);
            return grass ? grass.release() : nullptr;
        })

        // CatmullClarkSystem
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP,
                            GlobalBufferManager* globalBuffers,
                            const di::ResourcePath& resourcePath) -> CatmullClarkSystem* {
            SDL_Log("FruitDI: Creating CatmullClarkSystem");
            auto catmullClark = CatmullClarkSystem::create(
                ctx, hdrRP.renderPass,
                globalBuffers->uniformBuffers.buffers,
                resourcePath.path);
            return catmullClark ? catmullClark.release() : nullptr;
        })

        // HiZSystem
        .registerProvider([](const InitContext& ctx,
                            const di::DepthFormat& depthFormat) -> HiZSystem* {
            SDL_Log("FruitDI: Creating HiZSystem");
            auto hiZ = HiZSystem::create(ctx, depthFormat.format);
            return hiZ ? hiZ.release() : nullptr;
        })

        // Water systems
        .registerProvider([](const InitContext& ctx) -> WaterDisplacement* {
            SDL_Log("FruitDI: Creating WaterDisplacement");
            auto displacement = WaterDisplacement::create(ctx);
            return displacement ? displacement.release() : nullptr;
        })
        .registerProvider([](const InitContext& ctx) -> FlowMapGenerator* {
            SDL_Log("FruitDI: Creating FlowMapGenerator");
            auto flowMap = FlowMapGenerator::create(ctx);
            return flowMap ? flowMap.release() : nullptr;
        })
        .registerProvider([](const InitContext& ctx) -> FoamBuffer* {
            SDL_Log("FruitDI: Creating FoamBuffer");
            auto foam = FoamBuffer::create(ctx);
            return foam ? foam.release() : nullptr;
        })
        .registerProvider([](const InitContext& ctx) -> SSRSystem* {
            SDL_Log("FruitDI: Creating SSRSystem");
            auto ssr = SSRSystem::create(ctx);
            return ssr ? ssr.release() : nullptr;
        })
        .registerProvider([](const InitContext& ctx) -> WaterTileCull* {
            SDL_Log("FruitDI: Creating WaterTileCull");
            auto tileCull = WaterTileCull::create(ctx);
            return tileCull ? tileCull.release() : nullptr;
        })
        .registerProvider([](const InitContext& ctx) -> WaterGBuffer* {
            SDL_Log("FruitDI: Creating WaterGBuffer");
            auto gBuffer = WaterGBuffer::create(ctx);
            return gBuffer ? gBuffer.release() : nullptr;
        })
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP) -> WaterSystem* {
            SDL_Log("FruitDI: Creating WaterSystem");
            auto water = WaterSystem::create(ctx, hdrRP.renderPass);
            return water ? water.release() : nullptr;
        })

        // SceneManager
        .registerProvider([](const InitContext& ctx,
                            TerrainSystem* terrain,
                            const di::ResourcePath& resourcePath,
                            const di::SceneOriginConfig& sceneOriginCfg,
                            const di::AssetRegistryPtr& assetRegistryPtr) -> SceneManager* {
            SDL_Log("FruitDI: Creating SceneManager");
            if (!terrain) return nullptr;

            SceneBuilder::InitInfo sceneInfo{};
            sceneInfo.allocator = ctx.allocator;
            sceneInfo.device = ctx.device;
            sceneInfo.commandPool = ctx.commandPool;
            sceneInfo.graphicsQueue = ctx.graphicsQueue;
            sceneInfo.physicalDevice = ctx.physicalDevice;
            sceneInfo.resourcePath = resourcePath.path;
            sceneInfo.assetRegistry = assetRegistryPtr.registry;
            sceneInfo.getTerrainHeight = [terrain](float x, float z) {
                return terrain->getHeightAt(x, z);
            };
            sceneInfo.sceneOrigin = sceneOriginCfg.origin;
            sceneInfo.deferRenderables = true;

            auto sceneManager = SceneManager::create(sceneInfo);
            return sceneManager ? sceneManager.release() : nullptr;
        })

        // Profiler
        .registerProvider([](const InitContext& ctx,
                            const di::FramesInFlight& frames) -> Profiler* {
            SDL_Log("FruitDI: Creating Profiler");
            return Profiler::create(ctx.device, ctx.physicalDevice, frames.count).release();
        })

        // DebugLineSystem
        .registerProvider([](const InitContext& ctx,
                            const di::HDRRenderPass& hdrRP) -> DebugLineSystem* {
            SDL_Log("FruitDI: Creating DebugLineSystem");
            auto debugLine = DebugLineSystem::create(ctx, hdrRP.renderPass);
            return debugLine ? debugLine.release() : nullptr;
        })

        // Simple infrastructure systems
        .registerProvider([]() -> TimeSystem* {
            return new TimeSystem();
        })
        .registerProvider([]() -> CelestialCalculator* {
            return new CelestialCalculator();
        })
        .registerProvider([]() -> UBOBuilder* {
            return new UBOBuilder();
        })
        .registerProvider([]() -> ResizeCoordinator* {
            return new ResizeCoordinator();
        })
        .registerProvider([]() -> ErosionDataLoader* {
            return new ErosionDataLoader();
        })
        .registerProvider([]() -> RoadNetworkLoader* {
            return new RoadNetworkLoader();
        })
        .registerProvider([]() -> RoadRiverVisualization* {
            return new RoadRiverVisualization();
        })
        .registerProvider([]() -> EnvironmentSettings* {
            return new EnvironmentSettings();
        });
}
