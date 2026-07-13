#pragma once

#include "material/DescriptorManager.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <string>
#include <vector>

class VulkanContext;
class GPUSceneBuffer;

/**
 * InstancedScenePipeline - Graphics pipeline for GPU-driven indirect scene rendering.
 *
 * Mirrors ScenePipeline (the per-object CPU draw path) but is driven from the scene
 * instance SSBO: the vertex/fragment shaders read per-object transform and material
 * data via gl_InstanceIndex instead of push constants. Used by the indirect draw path
 * (see SceneObjectsDrawable / GPUSceneBuffer / GPUCullPass).
 *
 * Descriptor layout (two sets):
 * - Set 0: the exact common scene bindings used by ScenePipeline, so material
 *   descriptor sets from MaterialRegistry bind unchanged (shared with the CPU path).
 * - Set 1: the scene instance SSBO (BINDING_SCENE_INSTANCE_BUFFER), bound once per frame.
 *
 * No push-constant range (per-object data comes from the instance buffer). Built from
 * shader_instanced.vert/.frag instead of shader.vert/.frag.
 *
 * This is intentionally separate (composition, not inheritance) so the working CPU
 * pipeline is untouched.
 */
class InstancedScenePipeline {
public:
    InstancedScenePipeline() = default;
    ~InstancedScenePipeline() = default;

    // Non-copyable, non-movable (owns GPU resources)
    InstancedScenePipeline(const InstancedScenePipeline&) = delete;
    InstancedScenePipeline& operator=(const InstancedScenePipeline&) = delete;
    InstancedScenePipeline(InstancedScenePipeline&&) = delete;
    InstancedScenePipeline& operator=(InstancedScenePipeline&&) = delete;

    /**
     * Initialize descriptor set layout. Call before createGraphicsPipeline().
     */
    bool initLayout(VulkanContext& context);

    /**
     * Create the graphics pipeline. Requires the HDR render pass from PostProcessSystem.
     */
    bool createGraphicsPipeline(VulkanContext& context, vk::RenderPass hdrRenderPass,
                                const std::string& resourcePath);

    /**
     * Allocate and write the per-frame set-1 (instance buffer) descriptor sets.
     * Call once after the GPUSceneBuffer exists; the instance buffers are stable
     * for the lifetime of the buffer, so the sets are written once and bound by frame.
     */
    bool createInstanceDescriptorSets(vk::Device device, DescriptorManager::Pool& pool,
                                      GPUSceneBuffer& sceneBuffer, uint32_t framesInFlight);

    // Set-1 descriptor set for the given frame (VK_NULL_HANDLE if not created).
    vk::DescriptorSet getInstanceDescriptorSet(uint32_t frameIndex) const {
        return frameIndex < instanceDescriptorSets_.size() ? instanceDescriptorSets_[frameIndex]
                                                           : VK_NULL_HANDLE;
    }

    // Accessors
    // Set 0: common scene/material bindings (identical to ScenePipeline).
    vk::DescriptorSetLayout getCommonSetLayout() const {
        return commonSetLayout_ ? **commonSetLayout_ : vk::DescriptorSetLayout{};
    }

    // Set 1: scene instance SSBO. Used to allocate the per-frame instance descriptor set.
    vk::DescriptorSetLayout getInstanceSetLayout() const {
        return instanceSetLayout_ ? **instanceSetLayout_ : vk::DescriptorSetLayout{};
    }

    vk::DescriptorSetLayout getVkInstanceSetLayout() const {
        return instanceSetLayout_ ? static_cast<vk::DescriptorSetLayout>(**instanceSetLayout_) : VK_NULL_HANDLE;
    }

    vk::PipelineLayout getPipelineLayout() const {
        return pipelineLayout_ ? **pipelineLayout_ : vk::PipelineLayout{};
    }

    vk::Pipeline getGraphicsPipeline() const {
        return graphicsPipeline_ ? **graphicsPipeline_ : vk::Pipeline{};
    }

    // Pointer accessors for storing references in config structs
    const vk::Pipeline* getGraphicsPipelinePtr() const {
        return graphicsPipeline_ ? &**graphicsPipeline_ : nullptr;
    }

    const vk::PipelineLayout* getPipelineLayoutPtr() const {
        return pipelineLayout_ ? &**pipelineLayout_ : nullptr;
    }

    bool isInitialized() const { return initialized_; }
    bool hasPipeline() const { return graphicsPipeline_.has_value(); }

    // Release GPU resources. Must be called while vk::Device is still valid.
    void reset() {
        // Descriptor sets are owned by the shared pool; just drop our handles.
        instanceDescriptorSets_.clear();
        graphicsPipeline_.reset();
        pipelineLayout_.reset();
        instanceSetLayout_.reset();
        commonSetLayout_.reset();
        initialized_ = false;
    }

private:
    bool createDescriptorSetLayouts(vk::Device device, const vk::raii::Device& raiiDevice);

    std::optional<vk::raii::DescriptorSetLayout> commonSetLayout_;    // set 0
    std::optional<vk::raii::DescriptorSetLayout> instanceSetLayout_;  // set 1
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> graphicsPipeline_;

    // Per-frame set-1 descriptor sets (owned by the shared descriptor pool).
    std::vector<vk::DescriptorSet> instanceDescriptorSets_;

    bool initialized_ = false;
};
