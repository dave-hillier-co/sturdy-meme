#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <memory>

#include "Camera.h"
#include "DescriptorManager.h"
#include "VulkanContext.h"
#include "UBOs.h"
#include "FrameData.h"
#include "RenderContext.h"
#include "RenderPipeline.h"
#include "VulkanRAII.h"
#include "RendererSystems.h"

// Forward declarations for types used in public API
class PostProcessSystem;
class TimeSystem;
class TerrainSystem;
class CatmullClarkSystem;
class WindSystem;
class WaterSystem;
class WaterTileCull;
class SceneManager;
class TreeEditSystem;
class DebugLineSystem;
class Profiler;
class HiZSystem;
class AtmosphereLUTSystem;
struct AtmosphereParams;
struct EnvironmentSettings;
struct GeographicLocation;
class CelestialCalculator;
struct WaterPlacementData;
class ErosionDataLoader;
class SceneBuilder;
class Mesh;
class PhysicsWorld;

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);


class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool init(SDL_Window* window, const std::string& resourcePath);
    void shutdown();

    // Returns true if frame was rendered, false if skipped (caller must handle GUI frame cancellation)
    bool render(const Camera& camera);
    void waitIdle();

    // Wait for the previous frame's GPU work to complete.
    // MUST be called before destroying/updating any mesh buffers that the previous frame used.
    // This prevents race conditions where GPU is reading buffers we're about to destroy.
    void waitForPreviousFrame();

    uint32_t getWidth() const { return vulkanContext.getWidth(); }
    uint32_t getHeight() const { return vulkanContext.getHeight(); }

    // Handle window resize (recreate swapchain and dependent resources)
    bool handleResize();

    // Notify renderer that window was resized (will trigger resize on next render)
    void notifyWindowResized() { framebufferResized = true; }

    // Notify renderer that window was minimized/hidden (e.g., screen lock on macOS)
    void notifyWindowSuspended() { windowSuspended = true; }

    // Notify renderer that window was restored (e.g., screen unlock on macOS)
    void notifyWindowRestored() {
        windowSuspended = false;
        framebufferResized = true;  // Force swapchain recreation after restore
    }

    bool isWindowSuspended() const { return windowSuspended; }

    // Vulkan handle getters for GUI integration
    VkInstance getInstance() const { return vulkanContext.getInstance(); }
    VkPhysicalDevice getPhysicalDevice() const { return vulkanContext.getPhysicalDevice(); }
    VkDevice getDevice() const { return vulkanContext.getDevice(); }
    VkQueue getGraphicsQueue() const { return vulkanContext.getGraphicsQueue(); }
    uint32_t getGraphicsQueueFamily() const { return vulkanContext.getGraphicsQueueFamily(); }
    VkRenderPass getSwapchainRenderPass() const { return renderPass.get(); }
    uint32_t getSwapchainImageCount() const { return vulkanContext.getSwapchainImageCount(); }

    // Access to VulkanContext
    VulkanContext& getVulkanContext() { return vulkanContext; }
    const VulkanContext& getVulkanContext() const { return vulkanContext; }

    // GUI rendering callback (called during swapchain render pass)
    using GuiRenderCallback = std::function<void(VkCommandBuffer)>;
    void setGuiRenderCallback(GuiRenderCallback callback) { guiRenderCallback = callback; }

    void setTimeScale(float scale) { systems_->time().setTimeScale(scale); }
    float getTimeScale() const { return systems_->time().getTimeScale(); }
    void setTimeOfDay(float time) { systems_->time().setTimeOfDay(time); }
    void resumeAutoTime() { systems_->time().resumeAutoTime(); }
    float getTimeOfDay() const { return systems_->time().getTimeOfDay(); }
    TimeSystem& getTimeSystem() { return systems_->time(); }
    const TimeSystem& getTimeSystem() const { return systems_->time(); }

    void toggleCascadeDebug() { showCascadeDebug = !showCascadeDebug; }
    bool isShowingCascadeDebug() const { return showCascadeDebug; }

    void toggleSnowDepthDebug() { showSnowDepthDebug = !showSnowDepthDebug; }
    bool isShowingSnowDepthDebug() const { return showSnowDepthDebug; }

    // Cloud style toggle (procedural vs paraboloid LUT hybrid)
    void toggleCloudStyle() { useParaboloidClouds = !useParaboloidClouds; }
    bool isUsingParaboloidClouds() const { return useParaboloidClouds; }

    // Cloud coverage and density (synced to sky shader, cloud shadows, and cloud map LUT)
    void setCloudCoverage(float coverage) {
        cloudCoverage = glm::clamp(coverage, 0.0f, 1.0f);
        systems_->cloudShadow().setCloudCoverage(cloudCoverage);
        systems_->atmosphereLUT().setCloudCoverage(cloudCoverage);
    }
    float getCloudCoverage() const { return cloudCoverage; }

    void setCloudDensity(float density) {
        cloudDensity = glm::clamp(density, 0.0f, 1.0f);
        systems_->cloudShadow().setCloudDensity(cloudDensity);
        systems_->atmosphereLUT().setCloudDensity(cloudDensity);
    }
    float getCloudDensity() const { return cloudDensity; }

    // Cloud shadow control
    void setCloudShadowEnabled(bool enabled) { systems_->cloudShadow().setEnabled(enabled); }
    bool isCloudShadowEnabled() const { return systems_->cloudShadow().isEnabled(); }

    // HDR/Post-processing control
    void setHDREnabled(bool enabled) { hdrEnabled = enabled; }
    bool isHDREnabled() const { return hdrEnabled; }
    void setCloudShadowIntensity(float intensity) { systems_->cloudShadow().setShadowIntensity(intensity); }
    float getCloudShadowIntensity() const { return systems_->cloudShadow().getShadowIntensity(); }

    // God ray quality control
    void setGodRaysEnabled(bool enabled) { systems_->postProcess().setGodRaysEnabled(enabled); }
    bool isGodRaysEnabled() const { return systems_->postProcess().isGodRaysEnabled(); }
    void setGodRayQuality(PostProcessSystem::GodRayQuality quality);
    PostProcessSystem::GodRayQuality getGodRayQuality() const;

    // Froxel volumetric fog quality control
    void setFroxelFilterQuality(bool highQuality) { systems_->postProcess().setFroxelFilterQuality(highQuality); }
    bool isFroxelFilterHighQuality() const { return systems_->postProcess().isFroxelFilterHighQuality(); }

    // Terrain control
    void setTerrainEnabled(bool enabled) { terrainEnabled = enabled; }
    bool isTerrainEnabled() const { return terrainEnabled; }
    void toggleTerrainWireframe() { systems_->terrain().setWireframeMode(!systems_->terrain().isWireframeMode()); }
    bool isTerrainWireframeMode() const { return systems_->terrain().isWireframeMode(); }
    float getTerrainHeightAt(float x, float z) const { return systems_->terrain().getHeightAt(x, z); }
    uint32_t getTerrainNodeCount() const { return systems_->terrain().getNodeCount(); }

    // Terrain data access for physics integration
    const TerrainSystem& getTerrainSystem() const { return systems_->terrain(); }
    TerrainSystem& getTerrainSystem() { return systems_->terrain(); }

    // Catmull-Clark subdivision control
    void toggleCatmullClarkWireframe() { systems_->catmullClark().setWireframeMode(!systems_->catmullClark().isWireframeMode()); }
    bool isCatmullClarkWireframeMode() const { return systems_->catmullClark().isWireframeMode(); }
    CatmullClarkSystem& getCatmullClarkSystem() { return systems_->catmullClark(); }

    // Weather control
    void setWeatherIntensity(float intensity);
    void setWeatherType(uint32_t type);
    uint32_t getWeatherType() const { return systems_->weather().getWeatherType(); }
    float getIntensity() const { return systems_->weather().getIntensity(); }

    // Fog control - Froxel volumetric fog
    void setFogDensity(float density) { systems_->froxel().setFogDensity(density); }
    float getFogDensity() const { return systems_->froxel().getFogDensity(); }
    void setFogEnabled(bool enabled) { systems_->froxel().setEnabled(enabled); systems_->postProcess().setFroxelEnabled(enabled); }
    bool isFogEnabled() const { return systems_->froxel().isEnabled(); }

    // Froxel fog extended parameters
    void setFogBaseHeight(float h) { systems_->froxel().setFogBaseHeight(h); }
    float getFogBaseHeight() const { return systems_->froxel().getFogBaseHeight(); }
    void setFogScaleHeight(float h) { systems_->froxel().setFogScaleHeight(h); }
    float getFogScaleHeight() const { return systems_->froxel().getFogScaleHeight(); }
    void setFogAbsorption(float a) { systems_->froxel().setFogAbsorption(a); }
    float getFogAbsorption() const { return systems_->froxel().getFogAbsorption(); }
    void setVolumetricFarPlane(float f);
    float getVolumetricFarPlane() const { return systems_->froxel().getVolumetricFarPlane(); }
    void setTemporalBlend(float b) { systems_->froxel().setTemporalBlend(b); }
    float getTemporalBlend() const { return systems_->froxel().getTemporalBlend(); }

    // Height fog layer parameters
    void setLayerHeight(float h) { systems_->froxel().setLayerHeight(h); }
    float getLayerHeight() const { return systems_->froxel().getLayerHeight(); }
    void setLayerThickness(float t) { systems_->froxel().setLayerThickness(t); }
    float getLayerThickness() const { return systems_->froxel().getLayerThickness(); }
    void setLayerDensity(float d) { systems_->froxel().setLayerDensity(d); }
    float getLayerDensity() const { return systems_->froxel().getLayerDensity(); }

    // Atmospheric scattering parameters
    void setAtmosphereParams(const AtmosphereParams& params);
    const AtmosphereParams& getAtmosphereParams() const;
    AtmosphereLUTSystem& getAtmosphereLUTSystem() { return systems_->atmosphereLUT(); }

    // Leaf control
    void setLeafIntensity(float intensity) { systems_->leaf().setIntensity(intensity); }
    float getLeafIntensity() const { return systems_->leaf().getIntensity(); }
    void spawnConfetti(const glm::vec3& position, float velocity = 8.0f, float count = 100.0f, float coneAngle = 0.5f) {
        systems_->leaf().spawnConfetti(position, velocity, count, coneAngle);
    }

    // Snow control
    void setSnowAmount(float amount) { systems_->environmentSettings().snowAmount = glm::clamp(amount, 0.0f, 1.0f); }
    float getSnowAmount() const { return systems_->environmentSettings().snowAmount; }
    void setSnowColor(const glm::vec3& color) { systems_->environmentSettings().snowColor = color; }
    const glm::vec3& getSnowColor() const { return systems_->environmentSettings().snowColor; }
    void addSnowInteraction(const glm::vec3& position, float radius, float strength) {
        systems_->snowMask().addInteraction(position, radius, strength);
    }
    EnvironmentSettings& getEnvironmentSettings() { return systems_->environmentSettings(); }

    // Scene access
    SceneManager& getSceneManager() { return systems_->scene(); }
    const SceneManager& getSceneManager() const { return systems_->scene(); }

    // Rock system access for physics integration
    const RockSystem& getRockSystem() const { return systems_->rock(); }

    // Player position for grass interaction (xyz = position, w = capsule radius)
    void setPlayerPosition(const glm::vec3& position, float radius);
    void setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius);

    // Access to systems for simulation
    WindSystem& getWindSystem() { return systems_->wind(); }
    const WindSystem& getWindSystem() const { return systems_->wind(); }
    WaterSystem& getWaterSystem() { return systems_->water(); }
    const WaterSystem& getWaterSystem() const { return systems_->water(); }
    WaterTileCull& getWaterTileCull() { return systems_->waterTileCull(); }
    const WaterTileCull& getWaterTileCull() const { return systems_->waterTileCull(); }
    const WaterPlacementData& getWaterPlacementData() const;
    SceneBuilder& getSceneBuilder();
    Mesh& getFlagClothMesh();
    Mesh& getFlagPoleMesh();
    void uploadFlagClothMesh();

    // Animated character update
    // movementSpeed: horizontal speed for animation state selection
    // isGrounded: whether on the ground
    // isJumping: whether just started jumping
    void updateAnimatedCharacter(float deltaTime, float movementSpeed = 0.0f, bool isGrounded = true, bool isJumping = false);

    // Start a jump with trajectory prediction for animation sync
    void startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics);

    // Celestial/astronomical settings
    void setLocation(const GeographicLocation& location);
    const GeographicLocation& getLocation() const;
    void setDate(int year, int month, int day) { systems_->time().setDate(year, month, day); }
    int getCurrentYear() const { return systems_->time().getCurrentYear(); }
    int getCurrentMonth() const { return systems_->time().getCurrentMonth(); }
    int getCurrentDay() const { return systems_->time().getCurrentDay(); }
    const CelestialCalculator& getCelestialCalculator() const { return systems_->celestial(); }

    // Moon phase override controls
    void setMoonPhaseOverride(bool enabled) { systems_->time().setMoonPhaseOverride(enabled); }
    bool isMoonPhaseOverrideEnabled() const { return systems_->time().isMoonPhaseOverrideEnabled(); }
    void setMoonPhase(float phase) { systems_->time().setMoonPhase(phase); }
    float getMoonPhase() const { return systems_->time().getMoonPhase(); }
    float getCurrentMoonPhase() const { return systems_->time().getCurrentMoonPhase(); }  // Actual phase (auto or manual)

    // Moon brightness controls
    void setMoonBrightness(float brightness) { systems_->time().setMoonBrightness(brightness); }
    float getMoonBrightness() const { return systems_->time().getMoonBrightness(); }
    void setMoonDiscIntensity(float intensity) { systems_->time().setMoonDiscIntensity(intensity); }
    float getMoonDiscIntensity() const { return systems_->time().getMoonDiscIntensity(); }
    void setMoonEarthshine(float earthshine) { systems_->time().setMoonEarthshine(earthshine); }
    float getMoonEarthshine() const { return systems_->time().getMoonEarthshine(); }

    // Eclipse controls
    void setEclipseEnabled(bool enabled) { systems_->time().setEclipseEnabled(enabled); }
    bool isEclipseEnabled() const { return systems_->time().isEclipseEnabled(); }
    void setEclipseAmount(float amount) { systems_->time().setEclipseAmount(amount); }
    float getEclipseAmount() const { return systems_->time().getEclipseAmount(); }

    // Hi-Z occlusion culling control
    void setHiZCullingEnabled(bool enabled) { systems_->hiZ().setHiZEnabled(enabled); }
    bool isHiZCullingEnabled() const { return systems_->hiZ().isHiZEnabled(); }
    HiZSystem::CullingStats getHiZCullingStats() const;
    uint32_t getVisibleObjectCount() const { return systems_->hiZ().getVisibleCount(currentFrame); }

    // Profiling access
    Profiler& getProfiler() { return systems_->profiler(); }
    const Profiler& getProfiler() const { return systems_->profiler(); }
    void setProfilingEnabled(bool enabled) { systems_->profiler().setEnabled(enabled); }
    bool isProfilingEnabled() const { return systems_->profiler().isEnabled(); }

    // Tree edit system access
    TreeEditSystem& getTreeEditSystem() { return systems_->treeEdit(); }
    const TreeEditSystem& getTreeEditSystem() const { return systems_->treeEdit(); }
    bool isTreeEditMode() const { return systems_->treeEdit().isEnabled(); }
    void setTreeEditMode(bool enabled) { systems_->treeEdit().setEnabled(enabled); }
    void toggleTreeEditMode() { systems_->treeEdit().toggle(); }

    // Resource access for billboard capture
    VkCommandPool getCommandPool() const { return commandPool.get(); }
    DescriptorManager::Pool* getDescriptorPool() { return &*descriptorManagerPool; }
    std::string getShaderPath() const { return resourcePath + "/shaders"; }

    // Physics debug visualization
    DebugLineSystem& getDebugLineSystem() { return systems_->debugLine(); }
    const DebugLineSystem& getDebugLineSystem() const { return systems_->debugLine(); }
    void setPhysicsDebugEnabled(bool enabled) { physicsDebugEnabled = enabled; }
    bool isPhysicsDebugEnabled() const { return physicsDebugEnabled; }
#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* getPhysicsDebugRenderer() { return systems_->physicsDebugRenderer(); }
    const PhysicsDebugRenderer* getPhysicsDebugRenderer() const { return systems_->physicsDebugRenderer(); }

    // Update physics debug visualization (call before render)
    void updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos);
#endif

private:
    // High-level initialization phases
    bool initCoreVulkanResources();       // render pass, depth, framebuffers, command pool
    bool initDescriptorInfrastructure();  // layouts, pools, sets
    bool initSubsystems(const InitContext& initCtx);  // terrain, grass, weather, snow, water, etc.
    void initResizeCoordinator();         // resize registration

    bool createRenderPass();
    void destroyRenderResources();
    void destroyDepthImageAndView();  // Helper for resize (keeps sampler)
    void destroyFramebuffers();       // Helper for resize
    bool recreateDepthResources(VkExtent2D newExtent);  // Helper for resize
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorSetLayout();
    void addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder);
    bool createGraphicsPipeline();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createDepthResources();


    void updateUniformBuffer(uint32_t currentImage, const Camera& camera);

    // Render pass recording helpers (pure - only record commands, no state mutation)
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex);

    // Setup render pipeline stages with lambdas (called once during init)
    void setupRenderPipeline();

    // Pure calculation helpers (no state mutation)
    glm::vec2 calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const;

    // Build per-frame shared state from camera and timing
    FrameData buildFrameData(const Camera& camera, float deltaTime, float time) const;

    // Build render resources snapshot for pipeline stages
    RenderResources buildRenderResources(uint32_t swapchainImageIndex) const;

    SDL_Window* window = nullptr;
    std::string resourcePath;

    VulkanContext vulkanContext;

    // All rendering subsystems - managed with automatic lifecycle
    std::unique_ptr<RendererSystems> systems_;

    ManagedRenderPass renderPass;
    ManagedDescriptorSetLayout descriptorSetLayout;
    ManagedPipelineLayout pipelineLayout;
    ManagedPipeline graphicsPipeline;

    bool physicsDebugEnabled = false;
    glm::mat4 lastViewProj{1.0f};  // Cached view-projection for debug rendering
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    // Render pipeline (stages abstraction - for future refactoring)
    RenderPipeline renderPipeline;

    std::vector<ManagedFramebuffer> framebuffers;
    ManagedCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    ManagedImage depthImage;
    ManagedImageView depthImageView;
    ManagedSampler depthSampler;  // For Hi-Z pyramid generation
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    std::optional<DescriptorManager::Pool> descriptorManagerPool;

    std::vector<ManagedSemaphore> imageAvailableSemaphores;
    std::vector<ManagedSemaphore> renderFinishedSemaphores;
    std::vector<ManagedFence> inFlightFences;

    // Rock descriptor sets (RockSystem has its own textures, not in MaterialRegistry)
    std::vector<VkDescriptorSet> rockDescriptorSets;

    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    float lastSunIntensity = 1.0f;

    bool showCascadeDebug = false;         // true = show cascade colors overlay
    bool showSnowDepthDebug = false;       // true = show snow depth heat map overlay
    bool useParaboloidClouds = true;       // true = paraboloid LUT hybrid, false = procedural
    bool hdrEnabled = true;                // true = HDR tonemapping/bloom, false = bypass
    bool terrainEnabled = true;            // true = render terrain, false = skip terrain rendering

    // Cloud parameters (synced to UBO, cloud shadows, and cloud map LUT)
    float cloudCoverage = 0.5f;            // 0-1 cloud coverage amount
    float cloudDensity = 0.3f;             // Base density multiplier
    bool framebufferResized = false;       // true = window resized, need to recreate swapchain
    bool windowSuspended = false;          // true = window minimized/hidden (macOS screen lock)

    // Player position for grass displacement
    glm::vec3 playerPosition = glm::vec3(0.0f);
    glm::vec3 playerVelocity = glm::vec3(0.0f);
    float playerCapsuleRadius = 0.3f;      // Default capsule radius

    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    void updateLightBuffer(uint32_t currentImage, const Camera& camera);

    // Skinned mesh rendering
    bool initSkinnedMeshRenderer();
    bool createSkinnedMeshRendererDescriptorSets();

    // Hi-Z occlusion culling
    void updateHiZObjectData();
};
