#pragma once

#include "GrassTypes.h"
#include "GrassConstants.h"
#include "DescriptorManager.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <optional>

class GrassBuffers;

/**
 * GrassShadowPass - Manages the shadow rendering pipeline for grass.
 *
 * Handles:
 * - Shadow descriptor set layout, pipeline layout, and pipeline creation
 * - Shadow descriptor set allocation and writing
 * - Shadow draw command recording
 */
class GrassShadowPass {
public:
    bool createPipeline(const vk::raii::Device& device, vk::Device rawDevice,
                        const std::string& shaderPath, vk::RenderPass shadowRenderPass,
                        uint32_t shadowMapSize);

    bool allocateDescriptorSets(DescriptorManager::Pool* pool, uint32_t count);

    void updateDescriptorSets(vk::Device device, uint32_t count,
                               const std::vector<vk::Buffer>& rendererUniformBuffers,
                               const GrassBuffers& buffers,
                               const std::vector<vk::Buffer>& windBuffers);

    void recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                    uint32_t cascadeIndex, uint32_t readSet,
                    const GrassBuffers& buffers,
                    const std::vector<vk::Buffer>& rendererUniformBuffers,
                    vk::Device device);

    vk::DescriptorSetLayout getDescriptorSetLayout() const { return **descriptorSetLayout_; }

    void cleanup() {
        pipeline_.reset();
        pipelineLayout_.reset();
        descriptorSetLayout_.reset();
    }

private:
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;
    std::vector<vk::DescriptorSet> descriptorSets_;
};
