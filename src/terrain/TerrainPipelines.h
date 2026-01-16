#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <string>
#include <memory>
#include <optional>

struct SubgroupCapabilities;
class TerrainMeshlet;

// Manages all Vulkan pipelines for terrain rendering
// Extracted from TerrainSystem to reduce complexity
class TerrainPipelines {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TerrainPipelines(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VkRenderPass renderPass;
        VkRenderPass shadowRenderPass;
        VkDescriptorSetLayout computeDescriptorSetLayout;
        VkDescriptorSetLayout renderDescriptorSetLayout;
        std::string shaderPath;
        bool useMeshlets;
        uint32_t meshletIndexCount;  // From TerrainMeshlet::getIndexCount()
        const SubgroupCapabilities* subgroupCaps;  // For optimized compute paths
    };

    /**
     * Factory: Create and initialize TerrainPipelines.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TerrainPipelines> create(const InitInfo& info);


    ~TerrainPipelines() = default;

    // Non-copyable, non-movable
    TerrainPipelines(const TerrainPipelines&) = delete;
    TerrainPipelines& operator=(const TerrainPipelines&) = delete;
    TerrainPipelines(TerrainPipelines&&) = delete;
    TerrainPipelines& operator=(TerrainPipelines&&) = delete;

    // Compute pipeline accessors
    VkPipelineLayout getDispatcherPipelineLayout() const { return dispatcherPipelineLayout_ ? **dispatcherPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getDispatcherPipeline() const { return dispatcherPipeline_ ? **dispatcherPipeline_ : VK_NULL_HANDLE; }

    VkPipelineLayout getSubdivisionPipelineLayout() const { return subdivisionPipelineLayout_ ? **subdivisionPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getSubdivisionPipeline() const { return subdivisionPipeline_ ? **subdivisionPipeline_ : VK_NULL_HANDLE; }

    VkPipelineLayout getSumReductionPipelineLayout() const { return sumReductionPipelineLayout_ ? **sumReductionPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getSumReductionPrepassPipeline() const { return sumReductionPrepassPipeline_ ? **sumReductionPrepassPipeline_ : VK_NULL_HANDLE; }
    VkPipeline getSumReductionPrepassSubgroupPipeline() const { return sumReductionPrepassSubgroupPipeline_ ? **sumReductionPrepassSubgroupPipeline_ : VK_NULL_HANDLE; }
    VkPipeline getSumReductionPipeline() const { return sumReductionPipeline_ ? **sumReductionPipeline_ : VK_NULL_HANDLE; }

    VkPipelineLayout getSumReductionBatchedPipelineLayout() const { return sumReductionBatchedPipelineLayout_ ? **sumReductionBatchedPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getSumReductionBatchedPipeline() const { return sumReductionBatchedPipeline_ ? **sumReductionBatchedPipeline_ : VK_NULL_HANDLE; }

    VkPipelineLayout getFrustumCullPipelineLayout() const { return frustumCullPipelineLayout_ ? **frustumCullPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getFrustumCullPipeline() const { return frustumCullPipeline_ ? **frustumCullPipeline_ : VK_NULL_HANDLE; }

    VkPipelineLayout getPrepareDispatchPipelineLayout() const { return prepareDispatchPipelineLayout_ ? **prepareDispatchPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getPrepareDispatchPipeline() const { return prepareDispatchPipeline_ ? **prepareDispatchPipeline_ : VK_NULL_HANDLE; }

    // Render pipeline accessors
    VkPipelineLayout getRenderPipelineLayout() const { return renderPipelineLayout_ ? **renderPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getRenderPipeline() const { return renderPipeline_ ? **renderPipeline_ : VK_NULL_HANDLE; }
    VkPipeline getWireframePipeline() const { return wireframePipeline_ ? **wireframePipeline_ : VK_NULL_HANDLE; }
    VkPipeline getMeshletRenderPipeline() const { return meshletRenderPipeline_ ? **meshletRenderPipeline_ : VK_NULL_HANDLE; }
    VkPipeline getMeshletWireframePipeline() const { return meshletWireframePipeline_ ? **meshletWireframePipeline_ : VK_NULL_HANDLE; }

    // Shadow pipeline accessors
    VkPipelineLayout getShadowPipelineLayout() const { return shadowPipelineLayout_ ? **shadowPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getShadowPipeline() const { return shadowPipeline_ ? **shadowPipeline_ : VK_NULL_HANDLE; }
    VkPipeline getMeshletShadowPipeline() const { return meshletShadowPipeline_ ? **meshletShadowPipeline_ : VK_NULL_HANDLE; }

    // Shadow culling pipeline accessors
    VkPipelineLayout getShadowCullPipelineLayout() const { return shadowCullPipelineLayout_ ? **shadowCullPipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getShadowCullPipeline() const { return shadowCullPipeline_ ? **shadowCullPipeline_ : VK_NULL_HANDLE; }
    VkPipeline getShadowCulledPipeline() const { return shadowCulledPipeline_ ? **shadowCulledPipeline_ : VK_NULL_HANDLE; }
    VkPipeline getMeshletShadowCulledPipeline() const { return meshletShadowCulledPipeline_ ? **meshletShadowCulledPipeline_ : VK_NULL_HANDLE; }

    // Check if shadow culling is available
    bool hasShadowCulling() const { return shadowCullPipeline_.has_value(); }

private:
    bool initInternal(const InitInfo& info);

    // Pipeline creation helpers
    bool createDispatcherPipeline();
    bool createSubdivisionPipeline();
    bool createSumReductionPipelines();
    bool createFrustumCullPipelines();
    bool createRenderPipeline();
    bool createWireframePipeline();
    bool createShadowPipeline();
    bool createMeshletRenderPipeline();
    bool createMeshletWireframePipeline();
    bool createMeshletShadowPipeline();
    bool createShadowCullPipelines();

    // Stored from InitInfo for pipeline creation
    const vk::raii::Device* raiiDevice_ = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout renderDescriptorSetLayout = VK_NULL_HANDLE;
    std::string shaderPath;
    bool useMeshlets = false;
    uint32_t meshletIndexCount = 0;
    const SubgroupCapabilities* subgroupCaps = nullptr;

    // Compute pipelines
    std::optional<vk::raii::PipelineLayout> dispatcherPipelineLayout_;
    std::optional<vk::raii::Pipeline> dispatcherPipeline_;

    std::optional<vk::raii::PipelineLayout> subdivisionPipelineLayout_;
    std::optional<vk::raii::Pipeline> subdivisionPipeline_;

    std::optional<vk::raii::PipelineLayout> sumReductionPipelineLayout_;
    std::optional<vk::raii::Pipeline> sumReductionPrepassPipeline_;
    std::optional<vk::raii::Pipeline> sumReductionPrepassSubgroupPipeline_;
    std::optional<vk::raii::Pipeline> sumReductionPipeline_;

    std::optional<vk::raii::PipelineLayout> sumReductionBatchedPipelineLayout_;
    std::optional<vk::raii::Pipeline> sumReductionBatchedPipeline_;

    std::optional<vk::raii::PipelineLayout> frustumCullPipelineLayout_;
    std::optional<vk::raii::Pipeline> frustumCullPipeline_;

    std::optional<vk::raii::PipelineLayout> prepareDispatchPipelineLayout_;
    std::optional<vk::raii::Pipeline> prepareDispatchPipeline_;

    // Render pipelines
    std::optional<vk::raii::PipelineLayout> renderPipelineLayout_;
    std::optional<vk::raii::Pipeline> renderPipeline_;
    std::optional<vk::raii::Pipeline> wireframePipeline_;
    std::optional<vk::raii::Pipeline> meshletRenderPipeline_;
    std::optional<vk::raii::Pipeline> meshletWireframePipeline_;

    // Shadow pipelines
    std::optional<vk::raii::PipelineLayout> shadowPipelineLayout_;
    std::optional<vk::raii::Pipeline> shadowPipeline_;
    std::optional<vk::raii::Pipeline> meshletShadowPipeline_;

    // Shadow culling pipelines
    std::optional<vk::raii::PipelineLayout> shadowCullPipelineLayout_;
    std::optional<vk::raii::Pipeline> shadowCullPipeline_;
    std::optional<vk::raii::Pipeline> shadowCulledPipeline_;
    std::optional<vk::raii::Pipeline> meshletShadowCulledPipeline_;
};
