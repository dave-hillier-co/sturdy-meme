#pragma once

#include <memory>
#include <functional>
#include <vulkan/vulkan.h>
#include <fruit/fruit.h>
#include <glm/glm.hpp>

#include "InitContext.h"
#include "CoreResources.h"
#include "AtmosphereSystemGroup.h"
#include "VegetationSystemGroup.h"
#include "WaterSystemGroup.h"
#include "SnowSystemGroup.h"
#include "GeometrySystemGroup.h"
#include "scene/SceneCollection.h"

// Forward declarations for control subsystems
class EnvironmentControlSubsystem;
class WaterControlSubsystem;
class TreeControlSubsystem;
class GrassControlAdapter;
class DebugControlSubsystem;
class PerformanceControlSubsystem;
class SceneControlSubsystem;
class PlayerControlSubsystem;

// Forward declarations for interfaces
class ILocationControl;
class IWeatherState;
class IEnvironmentControl;
class IPostProcessState;
class ICloudShadowControl;
class ITerrainControl;
class IWaterControl;
class ITreeControl;
class IGrassControl;
class IDebugControl;
class IProfilerControl;
class IPerformanceControl;
class ISceneControl;
class IPlayerControl;

class VulkanContext;
struct PerformanceToggles;

// Forward declarations for all subsystems
class SkySystem;
class GrassSystem;
class WindSystem;
class DisplacementSystem;
class WeatherSystem;
class LeafSystem;
class PostProcessSystem;
class BloomSystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class TerrainSystem;
class CatmullClarkSystem;
class SnowMaskSystem;
class VolumetricSnowSystem;
class ScatterSystem;
class TreeSystem;
class TreeRenderer;
class TreeLODSystem;
class ImpostorCullSystem;
class CloudShadowSystem;
class HiZSystem;
class WaterSystem;
class WaterDisplacement;
class FlowMapGenerator;
class FoamBuffer;
class SSRSystem;
class WaterTileCull;
class WaterGBuffer;
class ErosionDataLoader;
class RoadNetworkLoader;
class RoadRiverVisualization;
class UBOBuilder;
class Profiler;
class DebugLineSystem;
class ResizeCoordinator;
class ShadowSystem;
class SceneManager;
class GlobalBufferManager;
class SkinnedMeshRenderer;
class TimeSystem;
class CelestialCalculator;
class BilateralGridSystem;
class DeferredTerrainObjects;
struct EnvironmentSettings;
struct TerrainConfig;

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

namespace di {

// Injectable wrapper types for Vulkan handles
struct SwapchainRenderPass { VkRenderPass renderPass; };
struct HDRRenderPass { VkRenderPass renderPass; };
struct ShadowRenderPass { VkRenderPass renderPass; };
struct SwapchainFormat { VkFormat format; };
struct DepthFormat { VkFormat format; };
struct DepthSampler { VkSampler sampler; };
struct MainDescriptorSetLayout { VkDescriptorSetLayout layout; };
struct SkinnedDescriptorSetLayout { VkDescriptorSetLayout layout; };
struct ResourcePath { std::string path; };
struct FramesInFlight { uint32_t count; };
struct SceneOriginConfig { glm::vec2 origin; };
struct AssetRegistryPtr { AssetRegistry* registry; };
struct DescriptorPoolPtr { void* pool; };  // DescriptorManager::Pool*
struct SwapchainExtent { VkExtent2D extent; };
struct RaiiDevicePtr { void* device; };  // vk::raii::Device*

// Bundle holder for PostProcess systems (created together)
// Fruit owns this struct, which in turn owns the systems
struct PostProcessBundleHolder {
    PostProcessSystem* postProcess = nullptr;
    BloomSystem* bloom = nullptr;
    BilateralGridSystem* bilateralGrid = nullptr;

    // Actual ownership
    std::unique_ptr<PostProcessSystem> postProcessOwned;
    std::unique_ptr<BloomSystem> bloomOwned;
    std::unique_ptr<BilateralGridSystem> bilateralGridOwned;
};

} // namespace di

/**
 * RendererSystems - Owns all rendering subsystems via Google Fruit DI
 *
 * Design:
 * - All systems are created and owned by Fruit injector
 * - No setters for DI-managed systems
 * - Systems are accessed via raw pointers (injector owns the memory)
 * - Late-bound systems (trees, detritus) use separate ownership
 */
class RendererSystems {
public:
    // Non-copyable, non-movable
    RendererSystems(const RendererSystems&) = delete;
    RendererSystems& operator=(const RendererSystems&) = delete;
    RendererSystems(RendererSystems&&) = delete;
    RendererSystems& operator=(RendererSystems&&) = delete;

    ~RendererSystems();

    /**
     * Fruit-injectable constructor.
     * The injector provides all systems automatically.
     */
    INJECT(RendererSystems(
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
    ));

    /**
     * Get tier-1 core resources for dependent system initialization
     */
    CoreResources getCoreResources(uint32_t framesInFlight) const;

    // ========================================================================
    // System accessors (Fruit owns all these systems)
    // ========================================================================

    // Tier 1 - Core rendering
    PostProcessSystem& postProcess() { return *postProcessSystem_; }
    const PostProcessSystem& postProcess() const { return *postProcessSystem_; }
    BloomSystem& bloom() { return *bloomSystem_; }
    const BloomSystem& bloom() const { return *bloomSystem_; }
    BilateralGridSystem& bilateralGrid() { return *bilateralGridSystem_; }
    const BilateralGridSystem& bilateralGrid() const { return *bilateralGridSystem_; }
    ShadowSystem& shadow() { return *shadowSystem_; }
    const ShadowSystem& shadow() const { return *shadowSystem_; }
    TerrainSystem& terrain() { return *terrainSystem_; }
    const TerrainSystem& terrain() const { return *terrainSystem_; }

    // Infrastructure
    GlobalBufferManager& globalBuffers() { return *globalBufferManager_; }
    const GlobalBufferManager& globalBuffers() const { return *globalBufferManager_; }
    SkinnedMeshRenderer& skinnedMesh() { return *skinnedMeshRenderer_; }
    const SkinnedMeshRenderer& skinnedMesh() const { return *skinnedMeshRenderer_; }

    // Sky and atmosphere
    SkySystem& sky() { return *skySystem_; }
    const SkySystem& sky() const { return *skySystem_; }
    AtmosphereLUTSystem& atmosphereLUT() { return *atmosphereLUTSystem_; }
    const AtmosphereLUTSystem& atmosphereLUT() const { return *atmosphereLUTSystem_; }
    FroxelSystem& froxel() { return *froxelSystem_; }
    const FroxelSystem& froxel() const { return *froxelSystem_; }
    CloudShadowSystem& cloudShadow() { return *cloudShadowSystem_; }
    const CloudShadowSystem& cloudShadow() const { return *cloudShadowSystem_; }

    // Environment (grass, wind, weather)
    GrassSystem& grass() { return *grassSystem_; }
    const GrassSystem& grass() const { return *grassSystem_; }
    WindSystem& wind() { return *windSystem_; }
    const WindSystem& wind() const { return *windSystem_; }
    DisplacementSystem& displacement() { return *displacementSystem_; }
    const DisplacementSystem& displacement() const { return *displacementSystem_; }
    WeatherSystem& weather() { return *weatherSystem_; }
    const WeatherSystem& weather() const { return *weatherSystem_; }
    LeafSystem& leaf() { return *leafSystem_; }
    const LeafSystem& leaf() const { return *leafSystem_; }

    // Snow
    SnowMaskSystem& snowMask() { return *snowMaskSystem_; }
    const SnowMaskSystem& snowMask() const { return *snowMaskSystem_; }
    VolumetricSnowSystem& volumetricSnow() { return *volumetricSnowSystem_; }
    const VolumetricSnowSystem& volumetricSnow() const { return *volumetricSnowSystem_; }

    // Water
    WaterSystem& water() { return *waterSystem_; }
    const WaterSystem& water() const { return *waterSystem_; }
    WaterDisplacement& waterDisplacement() { return *waterDisplacement_; }
    const WaterDisplacement& waterDisplacement() const { return *waterDisplacement_; }
    FlowMapGenerator& flowMap() { return *flowMapGenerator_; }
    const FlowMapGenerator& flowMap() const { return *flowMapGenerator_; }
    FoamBuffer& foam() { return *foamBuffer_; }
    const FoamBuffer& foam() const { return *foamBuffer_; }
    SSRSystem& ssr() { return *ssrSystem_; }
    const SSRSystem& ssr() const { return *ssrSystem_; }
    WaterTileCull& waterTileCull() { return *waterTileCull_; }
    const WaterTileCull& waterTileCull() const { return *waterTileCull_; }
    bool hasWaterTileCull() const { return waterTileCull_ != nullptr; }
    WaterGBuffer& waterGBuffer() { return *waterGBuffer_; }
    const WaterGBuffer& waterGBuffer() const { return *waterGBuffer_; }

    // Geometry processing
    CatmullClarkSystem& catmullClark() { return *catmullClarkSystem_; }
    const CatmullClarkSystem& catmullClark() const { return *catmullClarkSystem_; }
    HiZSystem& hiZ() { return *hiZSystem_; }
    const HiZSystem& hiZ() const { return *hiZSystem_; }

    // Scene and resources
    SceneManager& scene() { return *sceneManager_; }
    const SceneManager& scene() const { return *sceneManager_; }
    ErosionDataLoader& erosionData() { return *erosionDataLoader_; }
    const ErosionDataLoader& erosionData() const { return *erosionDataLoader_; }
    RoadNetworkLoader& roadData() { return *roadNetworkLoader_; }
    const RoadNetworkLoader& roadData() const { return *roadNetworkLoader_; }
    RoadRiverVisualization& roadRiverVis() { return *roadRiverVisualization_; }
    const RoadRiverVisualization& roadRiverVis() const { return *roadRiverVisualization_; }

    // Tools and debug
    DebugLineSystem& debugLine() { return *debugLineSystem_; }
    const DebugLineSystem& debugLine() const { return *debugLineSystem_; }
    Profiler& profiler() { return *profiler_; }
    const Profiler& profiler() const { return *profiler_; }

    // Coordination
    ResizeCoordinator& resizeCoordinator() { return *resizeCoordinator_; }
    const ResizeCoordinator& resizeCoordinator() const { return *resizeCoordinator_; }
    UBOBuilder& uboBuilder() { return *uboBuilder_; }
    const UBOBuilder& uboBuilder() const { return *uboBuilder_; }

    // Time and celestial
    TimeSystem& time() { return *timeSystem_; }
    const TimeSystem& time() const { return *timeSystem_; }
    CelestialCalculator& celestial() { return *celestialCalculator_; }
    const CelestialCalculator& celestial() const { return *celestialCalculator_; }

    // Environment settings
    EnvironmentSettings& environmentSettings() { return *environmentSettings_; }
    const EnvironmentSettings& environmentSettings() const { return *environmentSettings_; }

    // ========================================================================
    // Late-bound systems (not created by Fruit - need terrain data first)
    // ========================================================================

    ScatterSystem& rocks() { return *rocksSystem_; }
    const ScatterSystem& rocks() const { return *rocksSystem_; }
    void setRocks(std::unique_ptr<ScatterSystem> system);

    TreeSystem* tree() { return treeSystem_; }
    const TreeSystem* tree() const { return treeSystem_; }
    void setTree(std::unique_ptr<TreeSystem> system);

    TreeRenderer* treeRenderer() { return treeRenderer_; }
    const TreeRenderer* treeRenderer() const { return treeRenderer_; }
    void setTreeRenderer(std::unique_ptr<TreeRenderer> renderer);

    TreeLODSystem* treeLOD() { return treeLODSystem_; }
    const TreeLODSystem* treeLOD() const { return treeLODSystem_; }
    void setTreeLOD(std::unique_ptr<TreeLODSystem> system);

    ImpostorCullSystem* impostorCull() { return impostorCullSystem_; }
    const ImpostorCullSystem* impostorCull() const { return impostorCullSystem_; }
    void setImpostorCull(std::unique_ptr<ImpostorCullSystem> system);

    ScatterSystem* detritus() { return detritusSystem_; }
    const ScatterSystem* detritus() const { return detritusSystem_; }
    void setDetritus(std::unique_ptr<ScatterSystem> system);

    DeferredTerrainObjects* deferredTerrainObjects() { return deferredTerrainObjects_; }
    const DeferredTerrainObjects* deferredTerrainObjects() const { return deferredTerrainObjects_; }
    void setDeferredTerrainObjects(std::unique_ptr<DeferredTerrainObjects> deferred);

    // Scene collection for unified material iteration
    SceneCollection& sceneCollection() { return sceneCollection_; }
    const SceneCollection& sceneCollection() const { return sceneCollection_; }

    // ========================================================================
    // System group accessors
    // ========================================================================

    AtmosphereSystemGroup atmosphere() {
        return AtmosphereSystemGroup{
            skySystem_, froxelSystem_, atmosphereLUTSystem_, cloudShadowSystem_
        };
    }

    VegetationSystemGroup vegetation() {
        return VegetationSystemGroup{
            grassSystem_, windSystem_, displacementSystem_,
            treeSystem_, treeRenderer_, treeLODSystem_, impostorCullSystem_,
            rocksSystem_, detritusSystem_
        };
    }

    WaterSystemGroup waterGroup() {
        return WaterSystemGroup{
            waterSystem_, waterDisplacement_, flowMapGenerator_,
            foamBuffer_, ssrSystem_, waterTileCull_, waterGBuffer_
        };
    }

    SnowSystemGroup snowGroup() {
        return SnowSystemGroup{
            snowMaskSystem_, volumetricSnowSystem_, weatherSystem_, leafSystem_
        };
    }

    GeometrySystemGroup geometry() {
        return GeometrySystemGroup{catmullClarkSystem_};
    }

#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* physicsDebugRenderer() { return physicsDebugRenderer_; }
    const PhysicsDebugRenderer* physicsDebugRenderer() const { return physicsDebugRenderer_; }
    void createPhysicsDebugRenderer(const InitContext& ctx, VkRenderPass hdrRenderPass);
#endif

    // ========================================================================
    // Control subsystem accessors
    // ========================================================================

    void initControlSubsystems(VulkanContext& vulkanContext, PerformanceToggles& perfToggles);

    ILocationControl& locationControl();
    const ILocationControl& locationControl() const;
    IWeatherState& weatherState();
    const IWeatherState& weatherState() const;
    IEnvironmentControl& environmentControl();
    const IEnvironmentControl& environmentControl() const;
    IPostProcessState& postProcessState();
    const IPostProcessState& postProcessState() const;
    ICloudShadowControl& cloudShadowControl();
    const ICloudShadowControl& cloudShadowControl() const;
    ITerrainControl& terrainControl();
    const ITerrainControl& terrainControl() const;
    IWaterControl& waterControl();
    const IWaterControl& waterControl() const;
    ITreeControl& treeControl();
    const ITreeControl& treeControl() const;
    IGrassControl& grassControl();
    const IGrassControl& grassControl() const;
    IDebugControl& debugControl();
    const IDebugControl& debugControl() const;
    DebugControlSubsystem& debugControlSubsystem();
    const DebugControlSubsystem& debugControlSubsystem() const;
    IProfilerControl& profilerControl();
    const IProfilerControl& profilerControl() const;
    IPerformanceControl& performanceControl();
    const IPerformanceControl& performanceControl() const;
    ISceneControl& sceneControl();
    const ISceneControl& sceneControl() const;
    IPlayerControl& playerControl();
    const IPlayerControl& playerControl() const;

    void setPerformanceSyncCallback(std::function<void()> callback);

private:
    // Fruit-injected systems (Fruit owns memory)
    PostProcessSystem* postProcessSystem_;
    BloomSystem* bloomSystem_;
    BilateralGridSystem* bilateralGridSystem_;
    ShadowSystem* shadowSystem_;
    TerrainSystem* terrainSystem_;
    GlobalBufferManager* globalBufferManager_;
    SkinnedMeshRenderer* skinnedMeshRenderer_;
    SkySystem* skySystem_;
    AtmosphereLUTSystem* atmosphereLUTSystem_;
    FroxelSystem* froxelSystem_;
    CloudShadowSystem* cloudShadowSystem_;
    SnowMaskSystem* snowMaskSystem_;
    VolumetricSnowSystem* volumetricSnowSystem_;
    WeatherSystem* weatherSystem_;
    LeafSystem* leafSystem_;
    WindSystem* windSystem_;
    DisplacementSystem* displacementSystem_;
    GrassSystem* grassSystem_;
    CatmullClarkSystem* catmullClarkSystem_;
    HiZSystem* hiZSystem_;
    WaterDisplacement* waterDisplacement_;
    FlowMapGenerator* flowMapGenerator_;
    FoamBuffer* foamBuffer_;
    SSRSystem* ssrSystem_;
    WaterTileCull* waterTileCull_;
    WaterGBuffer* waterGBuffer_;
    WaterSystem* waterSystem_;
    SceneManager* sceneManager_;
    Profiler* profiler_;
    DebugLineSystem* debugLineSystem_;
    TimeSystem* timeSystem_;
    CelestialCalculator* celestialCalculator_;
    UBOBuilder* uboBuilder_;
    ResizeCoordinator* resizeCoordinator_;
    ErosionDataLoader* erosionDataLoader_;
    RoadNetworkLoader* roadNetworkLoader_;
    RoadRiverVisualization* roadRiverVisualization_;
    EnvironmentSettings* environmentSettings_;

    // Late-bound systems (owned here, not by Fruit)
    std::unique_ptr<ScatterSystem> rocksSystemOwned_;
    ScatterSystem* rocksSystem_ = nullptr;
    std::unique_ptr<TreeSystem> treeSystemOwned_;
    TreeSystem* treeSystem_ = nullptr;
    std::unique_ptr<TreeRenderer> treeRendererOwned_;
    TreeRenderer* treeRenderer_ = nullptr;
    std::unique_ptr<TreeLODSystem> treeLODSystemOwned_;
    TreeLODSystem* treeLODSystem_ = nullptr;
    std::unique_ptr<ImpostorCullSystem> impostorCullSystemOwned_;
    ImpostorCullSystem* impostorCullSystem_ = nullptr;
    std::unique_ptr<ScatterSystem> detritusSystemOwned_;
    ScatterSystem* detritusSystem_ = nullptr;
    std::unique_ptr<DeferredTerrainObjects> deferredTerrainObjectsOwned_;
    DeferredTerrainObjects* deferredTerrainObjects_ = nullptr;

    SceneCollection sceneCollection_;

#ifdef JPH_DEBUG_RENDERER
    std::unique_ptr<PhysicsDebugRenderer> physicsDebugRendererOwned_;
    PhysicsDebugRenderer* physicsDebugRenderer_ = nullptr;
#endif

    // Control subsystems
    std::unique_ptr<EnvironmentControlSubsystem> environmentControl_;
    std::unique_ptr<WaterControlSubsystem> waterControlSubsystem_;
    std::unique_ptr<TreeControlSubsystem> treeControlSubsystem_;
    std::unique_ptr<GrassControlAdapter> grassControlAdapter_;
    std::unique_ptr<DebugControlSubsystem> debugControlSubsystem_;
    std::unique_ptr<PerformanceControlSubsystem> performanceControlSubsystem_;
    std::unique_ptr<SceneControlSubsystem> sceneControlSubsystem_;
    std::unique_ptr<PlayerControlSubsystem> playerControlSubsystem_;

    bool controlsInitialized_ = false;
};

// ============================================================================
// Fruit Component Declaration
// ============================================================================

class AssetRegistry;  // Forward declaration

namespace DescriptorManager { class Pool; }  // Forward declaration

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
);
