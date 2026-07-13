#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <optional>

#include "TreeImpostorAtlas.h"
#include "DescriptorManager.h"

class TreeSystem;
struct TreeInstanceData;

// Per-tree LOD state
struct TreeLODState {
    enum class Level {
        FullDetail,     // Full geometry rendering
        Impostor,       // Billboard impostor only
        Blending        // Cross-fade between detail and impostor
    };

    Level currentLevel = Level::FullDetail;
    Level targetLevel = Level::FullDetail;
    float blendFactor = 0.0f;       // 0 = full detail, 1 = full impostor
    float lastDistance = 0.0f;
    uint32_t archetypeIndex = 0;    // Index into impostor atlas
};

// GPU instance data for impostor rendering
// Layout must match ImpostorInstance in shaders/tree_impostor_instance.glsl
struct ImpostorInstanceGPU {
    glm::vec4 positionAndScale;     // xyz = world position, w = scale
    glm::vec4 rotationAndArchetype; // x = rotation, y = archetype (as float), z = blend factor, w = reserved
    glm::vec4 sizeAndOffset;        // x = hSize, y = vSize, z = baseOffset, w = reserved
};

// Push constants for impostor rendering
struct ImpostorPushConstants {
    glm::vec4 cameraPos;    // xyz=camera position, w=autumnHueShift
    glm::vec4 lodParams;    // x=unused, y=brightness, z=normalStrength, w=unused
    glm::vec4 atlasParams;  // x=enableFrameBlending, y=unused, z=unused, w=unused
};

// Push constants for shadow rendering (extends ImpostorPushConstants)
struct ImpostorShadowPushConstants {
    glm::vec4 cameraPos;
    glm::vec4 lodParams;
    int cascadeIndex;
    float _pad[3];
};

class TreeLODSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TreeLODSystem(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice;  // vulkan-hpp RAII device
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        vk::RenderPass hdrRenderPass;
        vk::RenderPass shadowRenderPass;  // For shadow casting
        vk::CommandPool commandPool;
        vk::Queue graphicsQueue;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string resourcePath;
        uint32_t maxFramesInFlight;
        uint32_t shadowMapSize = 2048;  // Shadow map dimensions
    };

    static std::unique_ptr<TreeLODSystem> create(const InitInfo& info);
    ~TreeLODSystem();

    // Non-copyable, non-movable
    TreeLODSystem(const TreeLODSystem&) = delete;
    TreeLODSystem& operator=(const TreeLODSystem&) = delete;
    TreeLODSystem(TreeLODSystem&&) = delete;
    TreeLODSystem& operator=(TreeLODSystem&&) = delete;

    // Screen parameters for screen-space error LOD
    struct ScreenParams {
        float screenHeight;
        float tanHalfFOV;  // tan(fov/2)

        ScreenParams() : screenHeight(1080.0f), tanHalfFOV(1.0f) {}
        ScreenParams(float height, float tanFov) : screenHeight(height), tanHalfFOV(tanFov) {}
    };

    // Update LOD states based on camera position
    void update(float deltaTime, const glm::vec3& cameraPos, const TreeSystem& treeSystem,
                const ScreenParams& screenParams = ScreenParams());

    // Render impostors (called after full geometry trees are rendered)
    void renderImpostors(vk::CommandBuffer cmd, uint32_t frameIndex,
                         vk::Buffer uniformBuffer, vk::ImageView shadowMap, vk::Sampler shadowSampler);

    // Render impostors using GPU-culled data (indirect drawing with Hi-Z occlusion)
    // Call this instead of renderImpostors when ImpostorCullSystem is available
    void renderImpostorsGPUCulled(vk::CommandBuffer cmd, uint32_t frameIndex,
                                  vk::Buffer uniformBuffer, vk::ImageView shadowMap, vk::Sampler shadowSampler,
                                  vk::Buffer gpuInstanceBuffer, vk::Buffer indirectDrawBuffer);

    // Render impostor shadows for a specific cascade
    void renderImpostorShadows(vk::CommandBuffer cmd, uint32_t frameIndex,
                               int cascadeIndex, vk::Buffer uniformBuffer);

    // Render impostor shadows using GPU-culled data (indirect drawing with Hi-Z occlusion)
    void renderImpostorShadowsGPUCulled(vk::CommandBuffer cmd, uint32_t frameIndex,
                                        int cascadeIndex, vk::Buffer uniformBuffer,
                                        vk::Buffer gpuInstanceBuffer, vk::Buffer indirectDrawBuffer);

    // Get LOD state for a specific tree
    const TreeLODState& getTreeLODState(uint32_t treeIndex) const;

    // Check if tree should be rendered as full geometry
    bool shouldRenderFullGeometry(uint32_t treeIndex) const;

    // Check if tree should render impostor
    bool shouldRenderImpostor(uint32_t treeIndex) const;

    // Get blend factor for cross-fade (0 = full detail visible, 1 = impostor visible)
    float getBlendFactor(uint32_t treeIndex) const;

    // Cascade-aware shadow LOD queries
    // These consider both per-tree LOD state AND cascade-specific settings
    bool shouldRenderBranchShadow(uint32_t treeIndex, uint32_t cascadeIndex) const;
    bool shouldRenderLeafShadow(uint32_t treeIndex, uint32_t cascadeIndex) const;

    // Access to impostor atlas
    TreeImpostorAtlas* getImpostorAtlas() { return impostorAtlas_.get(); }
    const TreeImpostorAtlas* getImpostorAtlas() const { return impostorAtlas_.get(); }

    // LOD settings access
    TreeLODSettings& getLODSettings() { return impostorAtlas_->getLODSettings(); }
    const TreeLODSettings& getLODSettings() const { return impostorAtlas_->getLODSettings(); }

    // Generate impostor for a tree archetype
    int32_t generateImpostor(const std::string& name, const struct TreeOptions& options,
                             const struct Mesh& branchMesh,
                             const std::vector<struct LeafInstanceGPU>& leafInstances,
                             vk::ImageView barkAlbedo, vk::ImageView barkNormal,
                             vk::ImageView leafAlbedo, vk::Sampler sampler);

    // Update tree count (call when trees are added/removed)
    void updateTreeCount(size_t count);

    // Update extent on resize
    void setExtent(VkExtent2D newExtent);

    // Enable/disable GPU culling mode
    // When enabled, update() skips building CPU impostor list (GPU handles it)
    void setGPUCullingEnabled(bool enabled) { gpuCullingEnabled_ = enabled; }
    bool isGPUCullingEnabled() const { return gpuCullingEnabled_; }

    // Initialize descriptor sets with static bindings (call once during init)
    void initializeDescriptorSets(const std::vector<vk::Buffer>& uniformBuffers,
                                  vk::ImageView shadowMap, vk::Sampler shadowSampler);

    // Initialize GPU-culled descriptor sets with the GPU instance buffer (call once when GPU culling is enabled)
    void initializeGPUCulledDescriptors(vk::Buffer gpuInstanceBuffer);

    vk::Device getDevice() const { return device_; }

    // Debug info
    struct DebugInfo {
        glm::vec3 cameraPos{0.0f};
        glm::vec3 nearestTreePos{0.0f};
        float nearestTreeDistance = 0.0f;
        float calculatedElevation = 0.0f;
    };
    const DebugInfo& getDebugInfo() const { return debugInfo_; }


private:
    bool initInternal(const InitInfo& info);
    bool createPipeline();
    bool createShadowPipeline();
    bool createDescriptorSetLayout();
    bool createShadowDescriptorSetLayout();
    bool allocateDescriptorSets();
    bool allocateShadowDescriptorSets();
    bool createInstanceBuffer(size_t maxInstances);
    bool createBillboardMesh();
    void updateInstanceBuffer(const std::vector<ImpostorInstanceGPU>& instances);

    // LOD calculation helpers
    void updateTreeLODState(TreeLODState& state, float distance, float treeScale,
                            const TreeLODSettings& settings, const ScreenParams& screenParams);
    void buildImpostorInstance(ImpostorInstanceGPU& instance, const TreeInstanceData& tree,
                               const TreeLODState& state, const TreeSystem& treeSystem);

    // Push constant helpers
    ImpostorPushConstants buildImpostorPushConstants() const;
    ImpostorShadowPushConstants buildShadowPushConstants(int cascadeIndex) const;

    // Render setup helpers
    void bindImpostorPipeline(vk::CommandBuffer& cmd, uint32_t frameIndex);
    void bindShadowPipeline(vk::CommandBuffer& cmd, uint32_t frameIndex);
    void bindBillboardBuffers(vk::CommandBuffer& cmd, vk::Buffer instanceBuf = VK_NULL_HANDLE);

    const vk::raii::Device* raiiDevice_ = nullptr;
    vk::Device device_ = VK_NULL_HANDLE;
    vk::PhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::RenderPass hdrRenderPass_ = VK_NULL_HANDLE;
    vk::RenderPass shadowRenderPass_ = VK_NULL_HANDLE;
    vk::CommandPool commandPool_ = VK_NULL_HANDLE;
    vk::Queue graphicsQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    VkExtent2D extent_{};
    uint32_t maxFramesInFlight_ = 0;
    uint32_t shadowMapSize_ = 2048;

    // Impostor atlas generator
    std::unique_ptr<TreeImpostorAtlas> impostorAtlas_;

    // Per-tree LOD states
    std::vector<TreeLODState> lodStates_;

    // Impostor rendering pipeline
    std::optional<vk::raii::Pipeline> impostorPipeline_;
    std::optional<vk::raii::PipelineLayout> impostorPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> impostorDescriptorSetLayout_;

    // Shadow rendering pipeline
    std::optional<vk::raii::Pipeline> shadowPipeline_;
    std::optional<vk::raii::PipelineLayout> shadowPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> shadowDescriptorSetLayout_;

    // Per-frame descriptor sets
    std::vector<vk::DescriptorSet> impostorDescriptorSets_;
    std::vector<vk::DescriptorSet> shadowDescriptorSets_;

    // Billboard quad mesh
    vk::Buffer billboardVertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation billboardVertexAllocation_ = VK_NULL_HANDLE;
    vk::Buffer billboardIndexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation billboardIndexAllocation_ = VK_NULL_HANDLE;
    uint32_t billboardIndexCount_ = 0;

    // Instance buffer for impostor rendering
    vk::Buffer instanceBuffer_ = VK_NULL_HANDLE;
    VmaAllocation instanceAllocation_ = VK_NULL_HANDLE;
    vk::DeviceSize instanceBufferSize_ = 0;
    size_t maxInstances_ = 0;

    // Current frame data
    std::vector<ImpostorInstanceGPU> visibleImpostors_;
    glm::vec3 lastCameraPos_{0.0f};

    // GPU culling mode - when true, skip building CPU impostor list
    bool gpuCullingEnabled_ = false;

    // Debug info
    DebugInfo debugInfo_;
};
