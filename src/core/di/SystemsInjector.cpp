// SystemsInjector.cpp - Fruit DI-based RendererSystems implementation
//
// This implementation uses Fruit DI for systems with simple constructors
// (TimeSystem, CelestialCalculator, etc.) while maintaining factory patterns
// for complex Vulkan-dependent systems.

#include "SystemsInjector.h"
#include "SystemComponents.h"
#include "CoreResources.h"

// Include DI-managed system headers
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "ResizeCoordinator.h"
#include "UBOBuilder.h"
#include "ErosionDataLoader.h"
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "EnvironmentSettings.h"

// Include factory-created system headers
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
#include "Profiler.h"
#include "DebugLineSystem.h"
#include "ShadowSystem.h"
#include "SceneManager.h"
#include "GlobalBufferManager.h"
#include "SkinnedMeshRenderer.h"
#include "VulkanContext.h"
#include "PerformanceToggles.h"

// Include control subsystem headers
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

#include <SDL3/SDL_log.h>

// ============================================================================
// Constructor - Create Fruit injector for DI-managed systems
// ============================================================================

RendererSystems::RendererSystems() {
    // Create the Fruit injector with the infrastructure component
    // This creates and owns: TimeSystem, CelestialCalculator, ResizeCoordinator,
    // UBOBuilder, ErosionDataLoader, RoadNetworkLoader, RoadRiverVisualization,
    // EnvironmentSettings
    injector_ = std::make_unique<fruit::Injector<di::InfrastructureComponent>>(
        di::getInfrastructureComponent
    );

    SDL_Log("RendererSystems: Fruit DI injector created for infrastructure systems");
}

RendererSystems::~RendererSystems() {
    // Fruit injector destructor handles DI-managed systems
    // unique_ptrs handle factory-created systems
}

// ============================================================================
// DI-managed system accessors - delegate to Fruit injector
// ============================================================================

TimeSystem& RendererSystems::time() {
    return injector_->get<TimeSystem&>();
}
const TimeSystem& RendererSystems::time() const {
    return injector_->get<TimeSystem&>();
}

CelestialCalculator& RendererSystems::celestial() {
    return injector_->get<CelestialCalculator&>();
}
const CelestialCalculator& RendererSystems::celestial() const {
    return injector_->get<CelestialCalculator&>();
}

ResizeCoordinator& RendererSystems::resizeCoordinator() {
    return injector_->get<ResizeCoordinator&>();
}
const ResizeCoordinator& RendererSystems::resizeCoordinator() const {
    return injector_->get<ResizeCoordinator&>();
}

UBOBuilder& RendererSystems::uboBuilder() {
    return injector_->get<UBOBuilder&>();
}
const UBOBuilder& RendererSystems::uboBuilder() const {
    return injector_->get<UBOBuilder&>();
}

ErosionDataLoader& RendererSystems::erosionData() {
    return injector_->get<ErosionDataLoader&>();
}
const ErosionDataLoader& RendererSystems::erosionData() const {
    return injector_->get<ErosionDataLoader&>();
}

RoadNetworkLoader& RendererSystems::roadData() {
    return injector_->get<RoadNetworkLoader&>();
}
const RoadNetworkLoader& RendererSystems::roadData() const {
    return injector_->get<RoadNetworkLoader&>();
}

RoadRiverVisualization& RendererSystems::roadRiverVis() {
    return injector_->get<RoadRiverVisualization&>();
}
const RoadRiverVisualization& RendererSystems::roadRiverVis() const {
    return injector_->get<RoadRiverVisualization&>();
}

EnvironmentSettings& RendererSystems::environmentSettings() {
    return injector_->get<EnvironmentSettings&>();
}
const EnvironmentSettings& RendererSystems::environmentSettings() const {
    return injector_->get<EnvironmentSettings&>();
}

// ============================================================================
// Destroy - cleanup in reverse dependency order
// ============================================================================

void RendererSystems::destroy(VkDevice device, VmaAllocator allocator) {
    SDL_Log("RendererSystems::destroy starting");

    // Destroy factory-created systems in reverse dependency order
    debugLineSystem_.reset();

    // Water
    waterGBuffer_.reset();
    waterTileCull_.reset();
    ssrSystem_.reset();
    foamBuffer_.reset();
    flowMapGenerator_.reset();
    waterDisplacement_.reset();
    waterSystem_.reset();

    profiler_.reset();
    hiZSystem_.reset();

    // Geometry/Vegetation
    deferredTerrainObjects_.reset();
    detritusSystem_.reset();
    catmullClarkSystem_.reset();
    rocksSystem_.reset();
    treeLODSystem_.reset();
    impostorCullSystem_.reset();
    treeRenderer_.reset();
    treeSystem_.reset();

    // Atmosphere
    cloudShadowSystem_.reset();
    atmosphereLUTSystem_.reset();
    froxelSystem_.reset();

    // Weather/Snow
    leafSystem_.reset();
    weatherSystem_.reset();
    volumetricSnowSystem_.reset();
    snowMaskSystem_.reset();

    // Grass/Wind
    windSystem_.reset();
    grassSystem_.reset();

    sceneManager_.reset();
    globalBufferManager_.reset();

    // Tier 1
    skySystem_.reset();
    terrainSystem_.reset();
    shadowSystem_.reset();
    skinnedMeshRenderer_.reset();
    bilateralGridSystem_.reset();
    bloomSystem_.reset();
    postProcessSystem_.reset();

    // Fruit injector handles DI-managed systems automatically when destroyed
    // but we can reset it explicitly here for clarity
    injector_.reset();

    SDL_Log("RendererSystems::destroy complete");
}

// ============================================================================
// CoreResources
// ============================================================================

CoreResources RendererSystems::getCoreResources(uint32_t framesInFlight) const {
    return CoreResources::collect(*postProcessSystem_, *shadowSystem_,
                                  *terrainSystem_, framesInFlight);
}

// ============================================================================
// Factory-created system setters
// ============================================================================

void RendererSystems::setPostProcess(std::unique_ptr<PostProcessSystem> system) {
    postProcessSystem_ = std::move(system);
}

void RendererSystems::setBloom(std::unique_ptr<BloomSystem> system) {
    bloomSystem_ = std::move(system);
}

void RendererSystems::setBilateralGrid(std::unique_ptr<BilateralGridSystem> system) {
    bilateralGridSystem_ = std::move(system);
}

void RendererSystems::setShadow(std::unique_ptr<ShadowSystem> system) {
    shadowSystem_ = std::move(system);
}

void RendererSystems::setTerrain(std::unique_ptr<TerrainSystem> system) {
    terrainSystem_ = std::move(system);
}

void RendererSystems::setSky(std::unique_ptr<SkySystem> system) {
    skySystem_ = std::move(system);
}

void RendererSystems::setAtmosphereLUT(std::unique_ptr<AtmosphereLUTSystem> system) {
    atmosphereLUTSystem_ = std::move(system);
}

void RendererSystems::setFroxel(std::unique_ptr<FroxelSystem> system) {
    froxelSystem_ = std::move(system);
}

void RendererSystems::setCloudShadow(std::unique_ptr<CloudShadowSystem> system) {
    cloudShadowSystem_ = std::move(system);
}

void RendererSystems::setGrass(std::unique_ptr<GrassSystem> system) {
    grassSystem_ = std::move(system);
}

void RendererSystems::setWind(std::unique_ptr<WindSystem> system) {
    windSystem_ = std::move(system);
}

void RendererSystems::setDisplacement(std::unique_ptr<DisplacementSystem> system) {
    displacementSystem_ = std::move(system);
}

void RendererSystems::setWeather(std::unique_ptr<WeatherSystem> system) {
    weatherSystem_ = std::move(system);
}

void RendererSystems::setLeaf(std::unique_ptr<LeafSystem> system) {
    leafSystem_ = std::move(system);
}

void RendererSystems::setSnowMask(std::unique_ptr<SnowMaskSystem> system) {
    snowMaskSystem_ = std::move(system);
}

void RendererSystems::setVolumetricSnow(std::unique_ptr<VolumetricSnowSystem> system) {
    volumetricSnowSystem_ = std::move(system);
}

void RendererSystems::setWater(std::unique_ptr<WaterSystem> system) {
    waterSystem_ = std::move(system);
}

void RendererSystems::setWaterDisplacement(std::unique_ptr<WaterDisplacement> system) {
    waterDisplacement_ = std::move(system);
}

void RendererSystems::setFlowMap(std::unique_ptr<FlowMapGenerator> system) {
    flowMapGenerator_ = std::move(system);
}

void RendererSystems::setFoam(std::unique_ptr<FoamBuffer> system) {
    foamBuffer_ = std::move(system);
}

void RendererSystems::setSSR(std::unique_ptr<SSRSystem> system) {
    ssrSystem_ = std::move(system);
}

void RendererSystems::setWaterTileCull(std::unique_ptr<WaterTileCull> system) {
    waterTileCull_ = std::move(system);
}

void RendererSystems::setWaterGBuffer(std::unique_ptr<WaterGBuffer> system) {
    waterGBuffer_ = std::move(system);
}

void RendererSystems::setCatmullClark(std::unique_ptr<CatmullClarkSystem> system) {
    catmullClarkSystem_ = std::move(system);
}

void RendererSystems::setRocks(std::unique_ptr<ScatterSystem> system) {
    if (rocksSystem_) {
        sceneCollection_.unregisterMaterial(&rocksSystem_->getMaterial());
    }
    rocksSystem_ = std::move(system);
    if (rocksSystem_) {
        sceneCollection_.registerMaterial(&rocksSystem_->getMaterial());
    }
}

void RendererSystems::setTree(std::unique_ptr<TreeSystem> system) {
    treeSystem_ = std::move(system);
}

void RendererSystems::setTreeRenderer(std::unique_ptr<TreeRenderer> renderer) {
    treeRenderer_ = std::move(renderer);
}

void RendererSystems::setTreeLOD(std::unique_ptr<TreeLODSystem> system) {
    treeLODSystem_ = std::move(system);
}

void RendererSystems::setImpostorCull(std::unique_ptr<ImpostorCullSystem> system) {
    impostorCullSystem_ = std::move(system);
}

void RendererSystems::setDetritus(std::unique_ptr<ScatterSystem> system) {
    if (detritusSystem_) {
        sceneCollection_.unregisterMaterial(&detritusSystem_->getMaterial());
    }
    detritusSystem_ = std::move(system);
    if (detritusSystem_) {
        sceneCollection_.registerMaterial(&detritusSystem_->getMaterial());
    }
}

void RendererSystems::setDeferredTerrainObjects(std::unique_ptr<DeferredTerrainObjects> deferred) {
    deferredTerrainObjects_ = std::move(deferred);
}

void RendererSystems::setHiZ(std::unique_ptr<HiZSystem> system) {
    hiZSystem_ = std::move(system);
}

void RendererSystems::setScene(std::unique_ptr<SceneManager> system) {
    sceneManager_ = std::move(system);
}

void RendererSystems::setGlobalBuffers(std::unique_ptr<GlobalBufferManager> buffers) {
    globalBufferManager_ = std::move(buffers);
}

void RendererSystems::setSkinnedMesh(std::unique_ptr<SkinnedMeshRenderer> system) {
    skinnedMeshRenderer_ = std::move(system);
}

void RendererSystems::setDebugLineSystem(std::unique_ptr<DebugLineSystem> system) {
    debugLineSystem_ = std::move(system);
}

void RendererSystems::setProfiler(std::unique_ptr<Profiler> profiler) {
    profiler_ = std::move(profiler);
}

// ============================================================================
// System group accessors
// ============================================================================

AtmosphereSystemGroup RendererSystems::atmosphere() {
    return AtmosphereSystemGroup{
        skySystem_.get(),
        froxelSystem_.get(),
        atmosphereLUTSystem_.get(),
        cloudShadowSystem_.get()
    };
}

VegetationSystemGroup RendererSystems::vegetation() {
    return VegetationSystemGroup{
        grassSystem_.get(),
        windSystem_.get(),
        displacementSystem_.get(),
        treeSystem_.get(),
        treeRenderer_.get(),
        treeLODSystem_.get(),
        impostorCullSystem_.get(),
        rocksSystem_.get(),
        detritusSystem_.get()
    };
}

WaterSystemGroup RendererSystems::waterGroup() {
    return WaterSystemGroup{
        waterSystem_.get(),
        waterDisplacement_.get(),
        flowMapGenerator_.get(),
        foamBuffer_.get(),
        ssrSystem_.get(),
        waterTileCull_.get(),
        waterGBuffer_.get()
    };
}

SnowSystemGroup RendererSystems::snowGroup() {
    return SnowSystemGroup{
        snowMaskSystem_.get(),
        volumetricSnowSystem_.get(),
        weatherSystem_.get(),
        leafSystem_.get()
    };
}

GeometrySystemGroup RendererSystems::geometry() {
    return GeometrySystemGroup{
        catmullClarkSystem_.get()
    };
}

#ifdef JPH_DEBUG_RENDERER
void RendererSystems::createPhysicsDebugRenderer(const InitContext& /*ctx*/, VkRenderPass /*hdrRenderPass*/) {
    physicsDebugRenderer_ = std::make_unique<PhysicsDebugRenderer>();
    physicsDebugRenderer_->init();
}
#endif

// ============================================================================
// Control Subsystems
// ============================================================================

void RendererSystems::initControlSubsystems(VulkanContext& vulkanContext, PerformanceToggles& perfToggles) {
    environmentControl_ = std::make_unique<EnvironmentControlSubsystem>(
        *froxelSystem_, *atmosphereLUTSystem_, *leafSystem_, *cloudShadowSystem_,
        *postProcessSystem_, environmentSettings());
    waterControl_ = std::make_unique<WaterControlSubsystem>(*waterSystem_, *waterTileCull_);
    treeControl_ = std::make_unique<TreeControlSubsystem>(treeSystem_.get(), *this);
    grassControl_ = std::make_unique<GrassControlAdapter>(*grassSystem_);
    debugControl_ = std::make_unique<DebugControlSubsystem>(*debugLineSystem_, *hiZSystem_, *this);
    performanceControl_ = std::make_unique<PerformanceControlSubsystem>(perfToggles, nullptr);
    sceneControl_ = std::make_unique<SceneControlSubsystem>(*sceneManager_, vulkanContext);
    playerControl_ = std::make_unique<PlayerControlSubsystem>(*sceneManager_);

    controlsInitialized_ = true;
    SDL_Log("Control subsystems initialized");
}

void RendererSystems::setPerformanceSyncCallback(std::function<void()> callback) {
    if (performanceControl_) {
        performanceControl_->setSyncCallback(callback);
    }
}

// Control subsystem accessors
ILocationControl& RendererSystems::locationControl() { return celestial(); }
const ILocationControl& RendererSystems::locationControl() const { return celestial(); }

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

IWaterControl& RendererSystems::waterControl() { return *waterControl_; }
const IWaterControl& RendererSystems::waterControl() const { return *waterControl_; }

ITreeControl& RendererSystems::treeControl() { return *treeControl_; }
const ITreeControl& RendererSystems::treeControl() const { return *treeControl_; }

IGrassControl& RendererSystems::grassControl() { return *grassControl_; }
const IGrassControl& RendererSystems::grassControl() const { return *grassControl_; }

IDebugControl& RendererSystems::debugControl() { return *debugControl_; }
const IDebugControl& RendererSystems::debugControl() const { return *debugControl_; }
DebugControlSubsystem& RendererSystems::debugControlSubsystem() { return *debugControl_; }
const DebugControlSubsystem& RendererSystems::debugControlSubsystem() const { return *debugControl_; }

IProfilerControl& RendererSystems::profilerControl() { return *profiler_; }
const IProfilerControl& RendererSystems::profilerControl() const { return *profiler_; }

IPerformanceControl& RendererSystems::performanceControl() { return *performanceControl_; }
const IPerformanceControl& RendererSystems::performanceControl() const { return *performanceControl_; }

ISceneControl& RendererSystems::sceneControl() { return *sceneControl_; }
const ISceneControl& RendererSystems::sceneControl() const { return *sceneControl_; }

IPlayerControl& RendererSystems::playerControl() { return *playerControl_; }
const IPlayerControl& RendererSystems::playerControl() const { return *playerControl_; }
