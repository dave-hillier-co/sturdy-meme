#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>

// Configuration struct for graphics pipeline creation
// Captures the common variations to eliminate repetitive Vulkan boilerplate
struct GraphicsPipelineConfig {
    // Rasterization
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    bool depthBiasEnable = false;

    // Depth/stencil
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

    // Color blending
    bool hasColorAttachment = true;

    // Dynamic states
    bool dynamicDepthBias = false;

    // Vertex input (for meshlet pipelines)
    bool useMeshletVertexInput = false;

    // Render pass
    vk::RenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;
};

class PipelineBuilder {
public:
    explicit PipelineBuilder(vk::Device device);
    ~PipelineBuilder();

    PipelineBuilder& reset();

    // Set pipeline cache for faster pipeline creation
    PipelineBuilder& setPipelineCache(vk::PipelineCache cache);

    PipelineBuilder& addDescriptorBinding(uint32_t binding, VkDescriptorType type, uint32_t count,
                                          VkShaderStageFlags stageFlags, const vk::Sampler* immutableSamplers = nullptr);

    bool buildDescriptorSetLayout(vk::DescriptorSetLayout& layout) const;

    PipelineBuilder& addPushConstantRange(VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size);

    PipelineBuilder& addShaderStage(const std::string& path, VkShaderStageFlagBits stage, const char* entry = "main");

    bool buildPipelineLayout(const std::vector<vk::DescriptorSetLayout>& setLayouts, vk::PipelineLayout& layout) const;

    bool buildComputePipeline(vk::PipelineLayout layout, vk::Pipeline& pipeline);

    bool buildGraphicsPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfoBase, vk::PipelineLayout layout,
                               vk::Pipeline& pipeline);

    // Simplified graphics pipeline creation using config struct
    bool buildGraphicsPipeline(const GraphicsPipelineConfig& config, vk::PipelineLayout layout, vk::Pipeline& pipeline);

private:
    vk::Device device;
    vk::PipelineCache pipelineCacheHandle = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> descriptorBindings;
    std::vector<VkPushConstantRange> pushConstantRanges;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<vk::ShaderModule> shaderModules;

    void cleanupShaderModules();
};

// Preset configurations for common pipeline types
namespace PipelinePresets {
    // Standard filled rendering with back-face culling (terrain, meshes)
    inline GraphicsPipelineConfig filled(vk::RenderPass renderPass) {
        GraphicsPipelineConfig cfg;
        cfg.renderPass = renderPass;
        return cfg;
    }

    // Wireframe rendering without culling
    inline GraphicsPipelineConfig wireframe(vk::RenderPass renderPass) {
        GraphicsPipelineConfig cfg;
        cfg.polygonMode = VK_POLYGON_MODE_LINE;
        cfg.cullMode = VK_CULL_MODE_NONE;
        cfg.renderPass = renderPass;
        return cfg;
    }

    // Shadow pass (front-face culling, depth bias, no color)
    inline GraphicsPipelineConfig shadow(vk::RenderPass renderPass) {
        GraphicsPipelineConfig cfg;
        cfg.cullMode = VK_CULL_MODE_FRONT_BIT;
        cfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        cfg.depthBiasEnable = true;
        cfg.dynamicDepthBias = true;
        cfg.hasColorAttachment = false;
        cfg.renderPass = renderPass;
        return cfg;
    }
}

