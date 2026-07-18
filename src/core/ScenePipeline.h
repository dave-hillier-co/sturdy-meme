#pragma once

#include "material/DescriptorManager.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <string>

class VulkanContext;

/**
 * ScenePipeline - Owns the main scene descriptor set layout and graphics pipeline
 *
 * Groups:
 * - Main descriptor set layout (for scene rendering)
 * - Pipeline layout (wraps descriptor layout + push constants)
 * - Main graphics pipeline (for standard mesh rendering)
 *
 * The descriptor pool is owned separately by Renderer since it's a shared
 * resource allocator unrelated to pipeline configuration.
 */
class ScenePipeline {
public:
    ScenePipeline() = default;
    ~ScenePipeline() = default;

    // Non-copyable, non-movable (owns GPU resources)
    ScenePipeline(const ScenePipeline&) = delete;
    ScenePipeline& operator=(const ScenePipeline&) = delete;
    ScenePipeline(ScenePipeline&&) = delete;
    ScenePipeline& operator=(ScenePipeline&&) = delete;

    /**
     * Initialize descriptor set layout.
     * Call before createGraphicsPipeline().
     */
    bool initLayout(VulkanContext& context);

    /**
     * Create the graphics pipeline for standard scene rendering.
     * Requires PostProcessSystem to be initialized (for HDR render pass).
     */
    bool createGraphicsPipeline(VulkanContext& context, vk::RenderPass hdrRenderPass,
                                const std::string& resourcePath);

    // Accessors
    vk::DescriptorSetLayout getDescriptorSetLayout() const {
        return descriptorSetLayout_ ? **descriptorSetLayout_ : vk::DescriptorSetLayout{};
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

    // Raw handle accessors for compatibility
    vk::DescriptorSetLayout getVkDescriptorSetLayout() const {
        return descriptorSetLayout_ ? static_cast<vk::DescriptorSetLayout>(**descriptorSetLayout_) : VK_NULL_HANDLE;
    }

    vk::PipelineLayout getVkPipelineLayout() const {
        return pipelineLayout_ ? static_cast<vk::PipelineLayout>(**pipelineLayout_) : VK_NULL_HANDLE;
    }

    vk::Pipeline getVkGraphicsPipeline() const {
        return graphicsPipeline_ ? static_cast<vk::Pipeline>(**graphicsPipeline_) : VK_NULL_HANDLE;
    }

    /**
     * Add common descriptor bindings shared between main and skinned mesh layouts.
     * Provides the standard binding layout used by shader.frag.
     * Can be used by other systems (e.g., SkinnedMeshRenderer) to ensure layout compatibility.
     */
    static void addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder);

    bool isInitialized() const { return initialized_; }
    bool hasPipeline() const { return graphicsPipeline_.has_value(); }

    // Release GPU resources. Must be called while vk::Device is still valid.
    void reset() {
        graphicsPipeline_.reset();
        pipelineLayout_.reset();
        descriptorSetLayout_.reset();
        initialized_ = false;
    }

private:
    bool createDescriptorSetLayout(vk::Device device, const vk::raii::Device& raiiDevice);

    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> graphicsPipeline_;

    bool initialized_ = false;
};
