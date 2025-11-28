#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class PipelineBuilder {
public:
    explicit PipelineBuilder(VkDevice device);
    ~PipelineBuilder();

    PipelineBuilder& reset();

    PipelineBuilder& addDescriptorBinding(uint32_t binding, VkDescriptorType type, uint32_t count,
                                          VkShaderStageFlags stageFlags, const VkSampler* immutableSamplers = nullptr);

    bool buildDescriptorSetLayout(VkDescriptorSetLayout& layout) const;

    PipelineBuilder& addPushConstantRange(VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size);

    PipelineBuilder& addShaderStage(const std::string& path, VkShaderStageFlagBits stage, const char* entry = "main");

    bool buildPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts, VkPipelineLayout& layout) const;

    bool buildComputePipeline(VkPipelineLayout layout, VkPipeline& pipeline);

    bool buildGraphicsPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfoBase, VkPipelineLayout layout,
                               VkPipeline& pipeline);

private:
    VkDevice device;
    std::vector<VkDescriptorSetLayoutBinding> descriptorBindings;
    std::vector<VkPushConstantRange> pushConstantRanges;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<VkShaderModule> shaderModules;

    void cleanupShaderModules();
};

