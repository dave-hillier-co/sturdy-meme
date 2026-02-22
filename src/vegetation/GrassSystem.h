#pragma once

#include "EnvironmentSettings.h"
#include "GrassConstants.h"
#include "GrassTypes.h"
#include "GrassBuffers.h"
#include "GrassComputePass.h"
#include "GrassShadowPass.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "DynamicUniformBuffer.h"
#include "SystemLifecycleHelper.h"
#include "UBOs.h"
#include "core/FrameBuffered.h"
#include "interfaces/IRecordable.h"
#include "interfaces/IShadowCaster.h"

// Forward declarations
class WindSystem;
class GrassTileManager;
class DisplacementSystem;
struct InitContext;

class GrassSystem : public IRecordableAnimated, public IShadowCasterAnimated {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        vk::RenderPass renderPass;
        DescriptorManager::Pool* descriptorPool;
        vk::Extent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;
        vk::RenderPass shadowRenderPass;
        uint32_t shadowMapSize;
    };

    /**
     * Factory: Create and initialize GrassSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GrassSystem> create(const InitInfo& info);

    explicit GrassSystem(ConstructToken);

    /**
     * Bundle of grass-related systems
     */
    struct Bundle {
        std::unique_ptr<WindSystem> wind;
        std::unique_ptr<GrassSystem> grass;
    };

    /**
     * Factory: Create WindSystem and GrassSystem together.
     * Returns nullopt on failure.
     */
    static std::optional<Bundle> createWithDependencies(
        const InitContext& ctx,
        vk::RenderPass hdrRenderPass,
        vk::RenderPass shadowRenderPass,
        uint32_t shadowMapSize
    );

    ~GrassSystem();

    // Non-copyable, non-movable
    GrassSystem(const GrassSystem&) = delete;
    GrassSystem& operator=(const GrassSystem&) = delete;
    GrassSystem(GrassSystem&&) = delete;
    GrassSystem& operator=(GrassSystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(vk::Extent2D newExtent) { extent_ = newExtent; }
    void setExtent(VkExtent2D newExtent) { extent_ = vk::Extent2D{newExtent.width, newExtent.height}; }  // Backward-compatible overload

    void updateDescriptorSets(vk::Device device, const std::vector<vk::Buffer>& uniformBuffers,
                              vk::ImageView shadowMapView, vk::Sampler shadowSampler,
                              const std::vector<vk::Buffer>& windBuffers,
                              const std::vector<vk::Buffer>& lightBuffers,
                              vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
                              const std::vector<vk::Buffer>& snowBuffers,
                              const std::vector<vk::Buffer>& cloudShadowBuffers,
                              vk::ImageView cloudShadowMapView, vk::Sampler cloudShadowMapSampler,
                              vk::ImageView tileArrayView = {},
                              vk::Sampler tileSampler = {},
                              const std::array<vk::Buffer, 3>& tileInfoBuffers = {},
                              const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO = nullptr,
                              vk::ImageView holeMaskView = {},
                              vk::Sampler holeMaskSampler = {});

    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                        float terrainSize, float terrainHeightScale, float time);
    void recordResetAndCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time);
    void recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time);
    void recordShadowDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex);

    // IRecordableAnimated interface implementation
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) override {
        recordDraw(vk::CommandBuffer(cmd), frameIndex, time);
    }

    // IShadowCasterAnimated interface implementation
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, int cascade) override {
        recordShadowDraw(vk::CommandBuffer(cmd), frameIndex, time, static_cast<uint32_t>(cascade));
    }

    // Double-buffer management: call at frame start to swap compute/render buffer sets
    void advanceBufferSet();

    // Displacement system setter (required before recordResetAndCompute)
    void setDisplacementSystem(DisplacementSystem* displacement) { displacementSystem_ = displacement; }

    // Displacement texture accessors (delegate to DisplacementSystem)
    vk::ImageView getDisplacementImageView() const;
    vk::Sampler getDisplacementSampler() const;

    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

    // Set snow mask texture for snow on grass blades
    void setSnowMask(vk::Device device, vk::ImageView snowMaskView, vk::Sampler snowMaskSampler);

    // Set screen-space shadow buffer for pre-computed shadows
    void setScreenShadowBuffer(vk::ImageView view, vk::Sampler sampler) {
        screenShadowView_ = view;
        screenShadowSampler_ = sampler;
    }

    // Tiled grass mode accessors
    bool isTiledModeEnabled() const { return tiledModeEnabled_; }
    void setTiledModeEnabled(bool enabled) { tiledModeEnabled_ = enabled; }

    // Get the tile manager (may be null if not in tiled mode)
    GrassTileManager* getTileManager() const { return tileManager_.get(); }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles);
    bool createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles);
    bool createDescriptorSets();
    bool createExtraPipelines(SystemLifecycleHelper::PipelineHandles& computeHandles,
                               SystemLifecycleHelper::PipelineHandles& graphicsHandles);

    // Accessors
    vk::Device getDevice() const { return device_; }
    VmaAllocator getAllocator() const { return allocator_; }
    vk::RenderPass getRenderPass() const { return renderPass_; }
    DescriptorManager::Pool* getDescriptorPool() const { return descriptorPool_; }
    vk::Extent2D getExtent() const { return extent_; }
    const std::string& getShaderPath() const { return shaderPath_; }
    uint32_t getFramesInFlight() const { return framesInFlight_; }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return lifecycle_.getComputePipeline(); }
    SystemLifecycleHelper::PipelineHandles& getGraphicsPipelineHandles() { return lifecycle_.getGraphicsPipeline(); }

    // Descriptor set access
    VkDescriptorSet getGraphicsDescriptorSet(uint32_t index) const { return graphicsDescriptorSets_[index]; }

    // Composed components
    GrassBuffers buffers_;
    GrassComputePass computePass_;
    GrassShadowPass shadowPass_;

    // Core lifecycle helper
    SystemLifecycleHelper lifecycle_;

    // Graphics descriptor sets (one per buffer set)
    std::vector<VkDescriptorSet> graphicsDescriptorSets_;

    // Stored init info
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::RenderPass renderPass_;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    vk::Extent2D extent_;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;

    vk::RenderPass shadowRenderPass_;
    uint32_t shadowMapSize_ = 0;

    // RAII device pointer
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Displacement system (owned externally, provides displacement texture)
    DisplacementSystem* displacementSystem_ = nullptr;

    // Terrain heightmap for grass placement (stored for compute descriptor updates)
    vk::ImageView terrainHeightMapView_;
    vk::Sampler terrainHeightMapSampler_;

    // Screen-space shadow buffer (optional, from ScreenSpaceShadowSystem)
    vk::ImageView screenShadowView_;
    vk::Sampler screenShadowSampler_;

    // Tile cache resources for high-res terrain sampling
    vk::ImageView tileArrayView_;
    vk::Sampler tileSampler_;
    TripleBuffered<vk::Buffer> tileInfoBuffers_;  // Triple-buffered for frames-in-flight sync

    // Hole mask for terrain cutouts (caves, wells)
    vk::ImageView holeMaskView_;
    vk::Sampler holeMaskSampler_;

    // Renderer uniform buffers for per-frame descriptor updates (stores reference from updateDescriptorSets)
    std::vector<vk::Buffer> rendererUniformBuffers_;

    // Dynamic renderer UBO - used with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO_ = nullptr;

    const EnvironmentSettings* environmentSettings = nullptr;

    // Tiled grass system (for world-scale rendering)
    bool tiledModeEnabled_ = true;
    std::unique_ptr<GrassTileManager> tileManager_;

    // Camera position for camera-centered dispatch
    glm::vec3 lastCameraPos_{0.0f};
};
