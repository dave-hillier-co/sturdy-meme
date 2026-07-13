#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <string>
#include <functional>
#include <memory>
#include <optional>

#include "Camera.h"
#include "InitContext.h"
#include "Light.h"
#include "ecs/Components.h"
#include "ecs/Components.h"
#include "SkinnedMesh.h"
#include "VulkanHelpers.h"

// Forward declarations for the GPU-driven indirect shadow path
class GPUSceneBuffer;
class ShadowCullPass;

// Number of cascades for CSM
static constexpr uint32_t NUM_SHADOW_CASCADES = 4;

// Push constants for shadow rendering
// alignas(16) required for SIMD operations on glm::mat4
struct alignas(16) ShadowPushConstants {
    glm::mat4 model;
    int cascadeIndex;  // Which cascade we're rendering to
    int padding[3];    // Padding to align
};

class ShadowSystem {
public:
    // Configuration for shadow system initialization
    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;  // For RAII resource creation
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        vk::DescriptorSetLayout mainDescriptorSetLayout;  // For pipeline compatibility
        vk::DescriptorSetLayout skinnedDescriptorSetLayout = VK_NULL_HANDLE;  // For skinned shadow pipeline (optional)
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    ShadowSystem(ConstructToken, const InitInfo& info);

    /**
     * Factory: Create and initialize shadow system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<ShadowSystem> create(const InitInfo& info);
    static std::unique_ptr<ShadowSystem> create(const InitContext& ctx,
                                                 vk::DescriptorSetLayout mainDescriptorSetLayout,
                                                 vk::DescriptorSetLayout skinnedDescriptorSetLayout = VK_NULL_HANDLE);

    ~ShadowSystem();

    // Non-copyable, non-movable (stored via unique_ptr)
    ShadowSystem(ShadowSystem&&) = delete;
    ShadowSystem& operator=(ShadowSystem&&) = delete;
    ShadowSystem(const ShadowSystem&) = delete;
    ShadowSystem& operator=(const ShadowSystem&) = delete;

    // Update cascade matrices based on light direction and camera
    void updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera);

    // Record shadow pass for all cascades
    // Callback signature: void(vk::CommandBuffer cmd, uint32_t cascade, const glm::mat4& lightMatrix)
    using DrawCallback = std::function<void(vk::CommandBuffer, uint32_t, const glm::mat4&)>;
    // Pre-cascade compute callback: runs BEFORE each cascade's render pass (for GPU culling)
    // Signature: void(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t cascade, const glm::mat4& lightMatrix)
    using ComputeCallback = std::function<void(vk::CommandBuffer, uint32_t, uint32_t, const glm::mat4&)>;
    // Optional GPU-driven indirect scene-object shadow path. When enabled, the shared scene
    // objects (those mirrored into GPUSceneBuffer) are culled per-cascade on the GPU and drawn
    // with indirect commands instead of the instanced/per-object path. Trees, grass, terrain
    // and skinned characters keep their own callbacks either way. The CPU-supplied
    // sceneObjects vector is still drawn when this is disabled.
    // Plain aggregate (no in-class initializers) so it can be a `= {}` default argument:
    // a value-initialized instance is all-zero, i.e. disabled.
    struct IndirectShadowParams {
        bool enabled;
        ShadowCullPass* cullPass;
        GPUSceneBuffer* sceneBuffer;
        bool canMultiDrawIndirect;
    };

    void recordShadowPass(vk::CommandBuffer cmd, uint32_t frameIndex,
                          vk::DescriptorSet descriptorSet,
                          const std::vector<ecs::RenderData>& sceneObjects,
                          const DrawCallback& terrainDrawCallback,
                          const DrawCallback& grassDrawCallback,
                          const DrawCallback& treeDrawCallback = nullptr,
                          const DrawCallback& skinnedDrawCallback = nullptr,
                          const ComputeCallback& preCascadeComputeCallback = nullptr,
                          const IndirectShadowParams& indirect = {});

    /**
     * Record empty depth-clear render passes for each cascade if the shadow map
     * has never been rendered. The cascade shadow map is statically sampled by the
     * main scene shaders, so it must hold valid contents (depth = 1.0, no shadow)
     * even on frames where the shadow pass is skipped (e.g. sun below horizon).
     */
    void recordInitialClearIfNeeded(vk::CommandBuffer cmd);

    /**
     * Initialize GPU-driven indirect shadow resources (pipeline + per-frame instance
     * descriptor sets bound to the GPUSceneBuffer instance buffer). Requires the
     * GPUSceneBuffer to already exist. Safe to skip; the CPU/instanced path remains.
     */
    bool initIndirectShadowPath(GPUSceneBuffer& sceneBuffer);
    bool hasIndirectShadowPath() const { return indirectShadowReady_; }

    // Record skinned mesh shadow for a single cascade (called after bindSkinnedShadowPipeline)
    void recordSkinnedMeshShadow(vk::CommandBuffer cmd, uint32_t cascade,
                                  const glm::mat4& modelMatrix,
                                  const SkinnedMesh& mesh);

    /**
     * Bind the skinned shadow pipeline with dynamic offset for bone matrices.
     * @param cmd Command buffer to record to
     * @param descriptorSet Descriptor set with dynamic UBO binding for bone matrices
     * @param boneMatrixOffset Dynamic offset to select character's bone matrices slot
     */
    void bindSkinnedShadowPipeline(vk::CommandBuffer cmd, vk::DescriptorSet descriptorSet,
                                    uint32_t boneMatrixOffset = 0);

    // Shadow map accessors (vulkan-hpp)
    vk::ImageView getShadowImageView() const { return vk::ImageView(csmResources.getArrayView()); }
    vk::Sampler getShadowSampler() const { return vk::Sampler(csmResources.getSampler()); }
    uint32_t getShadowMapSize() const { return SHADOW_MAP_SIZE; }

    // Cascade accessors (vulkan-hpp)
    uint32_t getCascadeCount() const { return NUM_SHADOW_CASCADES; }
    vk::ImageView getCascadeView(uint32_t cascade) const {
        return cascade < NUM_SHADOW_CASCADES ? vk::ImageView(csmResources.getLayerView(cascade)) : vk::ImageView{};
    }
    vk::RenderPass getShadowRenderPass() const { return vk::RenderPass(shadowRenderPass); }
    vk::Framebuffer getCascadeFramebuffer(uint32_t cascade) const {
        return cascade < cascadeFramebuffers.size() ? vk::Framebuffer(cascadeFramebuffers[cascade]) : vk::Framebuffer{};
    }

    // Additional shadow pipeline accessors (not in interface)
    vk::Pipeline getShadowPipeline() const { return shadowPipeline; }
    vk::PipelineLayout getShadowPipelineLayout() const { return shadowPipelineLayout; }
    vk::Pipeline getSkinnedShadowPipeline() const { return skinnedShadowPipeline; }
    vk::PipelineLayout getSkinnedShadowPipelineLayout() const { return skinnedShadowPipelineLayout; }

    // Cascade data accessors
    const std::array<glm::mat4, NUM_SHADOW_CASCADES>& getCascadeMatrices() const { return cascadeMatrices; }
    const std::vector<float>& getCascadeSplitDepths() const { return cascadeSplitDepths; }

    // Dynamic shadow resource accessors (for binding in main shader)
    // Bounds-checked accessors to prevent O3 UB from OOB access
    vk::ImageView getPointShadowArrayView(uint32_t frameIndex) const {
        return frameIndex < pointShadowResources.size() ? pointShadowResources[frameIndex].getArrayView() : VK_NULL_HANDLE;
    }
    vk::Sampler getPointShadowSampler() const { return pointShadowResources.empty() ? VK_NULL_HANDLE : pointShadowResources[0].getSampler(); }
    vk::ImageView getSpotShadowArrayView(uint32_t frameIndex) const {
        return frameIndex < spotShadowResources.size() ? spotShadowResources[frameIndex].getArrayView() : VK_NULL_HANDLE;
    }
    vk::Sampler getSpotShadowSampler() const { return spotShadowResources.empty() ? VK_NULL_HANDLE : spotShadowResources[0].getSampler(); }

    // Dynamic shadow rendering (placeholder for future implementation)
    void renderDynamicShadows(vk::CommandBuffer cmd, uint32_t frameIndex,
                              vk::DescriptorSet descriptorSet,
                              const std::vector<ecs::RenderData>& sceneObjects,
                              const DrawCallback& terrainDrawCallback,
                              const DrawCallback& grassDrawCallback,
                              const DrawCallback& skinnedDrawCallback,
                              const std::vector<Light>& visibleLights);


private:
    // CSM creation methods
    bool createShadowResources();
    bool createShadowRenderPass();
    bool createShadowPipeline();
    bool createSkinnedShadowPipeline();

    // Dynamic shadow creation methods
    bool createDynamicShadowResources();
    bool createDynamicShadowPipeline();
    void destroyDynamicShadowResources();

    // Pipeline helper
    bool createShadowPipelineCommon(
        const std::string& vertShader,
        const std::string& fragShader,
        vk::DescriptorSetLayout descriptorSetLayout,
        const VkVertexInputBindingDescription& binding,
        const std::vector<VkVertexInputAttributeDescription>& attributes,
        vk::PipelineLayout& outLayout,
        vk::Pipeline& outPipeline);

    // Draw helper
    void drawShadowScene(
        vk::CommandBuffer cmd,
        vk::PipelineLayout layout,
        uint32_t cascadeOrFaceIndex,
        const glm::mat4& lightMatrix,
        const std::vector<ecs::RenderData>& sceneObjects,
        const DrawCallback& terrainCallback,
        const DrawCallback& grassCallback,
        const DrawCallback& treeCallback,
        const DrawCallback& skinnedCallback);

    // Cascade calculation methods
    void calculateCascadeSplits(float nearClip, float farClip, float lambda, std::vector<float>& splits);
    glm::mat4 calculateCascadeMatrix(const glm::vec3& lightDir, const Camera& camera, const glm::mat4& invView, float nearSplit, float farSplit);

    const InitInfo initInfo_;

    // CSM shadow map resources
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    DepthArrayResources csmResources;
    std::optional<vk::raii::RenderPass> shadowRenderPass_;
    vk::RenderPass shadowRenderPass = VK_NULL_HANDLE;  // Raw handle for compatibility
    std::vector<vk::Framebuffer> cascadeFramebuffers;
    vk::Pipeline shadowPipeline = VK_NULL_HANDLE;
    vk::PipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

    // CSM cascade data
    std::vector<float> cascadeSplitDepths;
    std::array<glm::mat4, NUM_SHADOW_CASCADES> cascadeMatrices;

    // Change-detection cache: the cascade matrices only depend on the sun direction
    // and the camera view/projection, so skip the recompute when none have changed.
    bool cascadesValid_ = false;
    glm::vec3 lastLightDir_{0.0f};
    glm::mat4 lastView_{1.0f};
    glm::mat4 lastProj_{1.0f};

    // Dynamic light shadow maps
    static constexpr uint32_t DYNAMIC_SHADOW_MAP_SIZE = 1024;
    static constexpr uint32_t MAX_SHADOW_CASTING_LIGHTS = 8;

    // Point light shadows (cube maps) - per frame
    std::vector<DepthArrayResources> pointShadowResources;
    std::vector<std::vector<vk::Framebuffer>> pointShadowFramebuffers;  // [frame][face]

    // Spot light shadows (2D depth textures) - per frame
    std::vector<DepthArrayResources> spotShadowResources;
    std::vector<std::vector<vk::Framebuffer>> spotShadowFramebuffers;   // [frame][light]

    vk::Pipeline dynamicShadowPipeline = VK_NULL_HANDLE;
    vk::PipelineLayout dynamicShadowPipelineLayout = VK_NULL_HANDLE;

    // Skinned mesh shadow pipeline (for GPU-skinned characters)
    vk::Pipeline skinnedShadowPipeline = VK_NULL_HANDLE;
    vk::PipelineLayout skinnedShadowPipelineLayout = VK_NULL_HANDLE;

    // Instanced shadow rendering (batches scene objects by mesh)
    static constexpr uint32_t MAX_SHADOW_INSTANCES = 512;  // Max instances per frame
    vk::Pipeline instancedShadowPipeline = VK_NULL_HANDLE;
    vk::PipelineLayout instancedShadowPipelineLayout = VK_NULL_HANDLE;
    vk::DescriptorSetLayout instancedShadowDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<vk::DescriptorSet> instancedShadowDescriptorSets;  // Per frame
    std::vector<vk::Buffer> instanceBuffers;      // Per frame
    std::vector<VmaAllocation> instanceAllocations;  // Per frame
    std::vector<void*> instanceMappedPtrs;      // Persistently mapped

    // Push constants for instanced shadow rendering
    struct InstancedShadowPushConstants {
        uint32_t cascadeIndex;
        uint32_t instanceOffset;
    };

    bool createInstancedShadowPipeline();
    bool createInstancedShadowResources();
    void destroyInstancedShadowResources();

    // Draw scene objects using instanced rendering (batches by mesh)
    void drawShadowSceneInstanced(
        vk::CommandBuffer cmd,
        uint32_t frameIndex,
        uint32_t cascadeIndex,
        const std::vector<ecs::RenderData>& sceneObjects);

    // GPU-driven indirect shadow rendering (reuses GPUSceneBuffer instance buffer + the
    // per-cascade ShadowCullPass output). set 0 = main UBO (cascade matrices), set 1 =
    // scene instance SSBO (reuses instancedShadowDescriptorSetLayout).
    struct IndirectShadowPushConstants {
        uint32_t cascadeIndex;
    };
    vk::Pipeline indirectShadowPipeline = VK_NULL_HANDLE;
    vk::PipelineLayout indirectShadowPipelineLayout = VK_NULL_HANDLE;
    vk::DescriptorPool indirectShadowPool = VK_NULL_HANDLE;
    std::vector<vk::DescriptorSet> indirectInstanceDescriptorSets;  // per frame -> instance SSBO
    bool indirectShadowReady_ = false;
    bool csmInitialized_ = false;

    // Draw scene objects for one cascade via per-cascade indirect commands.
    void recordShadowSceneIndirect(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t cascade,
                                   vk::DescriptorSet uboDescriptorSet,
                                   GPUSceneBuffer& sceneBuffer,
                                   vk::Buffer indirectBuffer, bool canMultiDrawIndirect);

    bool initialized_ = false;
};
