#pragma once

#include "GrassTypes.h"
#include "GrassConstants.h"
#include "DescriptorManager.h"
#include "SystemLifecycleHelper.h"
#include "core/FrameBuffered.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <optional>

class GrassBuffers;
class DisplacementSystem;

/**
 * GrassComputePass - Manages the compute pipeline and dispatch for grass generation.
 *
 * Handles:
 * - Compute descriptor set layout and pipeline creation
 * - Tiled compute pipeline creation
 * - Compute descriptor set allocation and writing
 * - Per-frame compute dispatch recording (tile grid around camera)
 */
class GrassComputePass {
public:
    bool createDescriptorSetLayout(vk::Device device,
                                    SystemLifecycleHelper::PipelineHandles& handles);

    bool createPipeline(const vk::raii::Device& device, const std::string& shaderPath,
                        SystemLifecycleHelper::PipelineHandles& handles);

    bool createTiledPipeline(const vk::raii::Device& device, const std::string& shaderPath,
                              VkPipelineLayout pipelineLayout);

    bool allocateDescriptorSets(DescriptorManager::Pool* pool,
                                VkDescriptorSetLayout layout, uint32_t count);

    void writeInitialDescriptorSets(vk::Device device, const GrassBuffers& buffers, uint32_t count);

    void updateDescriptorSets(vk::Device device, uint32_t count,
                               vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
                               DisplacementSystem* displacementSystem,
                               vk::ImageView tileArrayView, vk::Sampler tileSampler,
                               const TripleBuffered<vk::Buffer>& tileInfoBuffers,
                               vk::ImageView holeMaskView, vk::Sampler holeMaskSampler);

    void recordResetAndCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                const GrassBuffers& buffers,
                                const TripleBuffered<vk::Buffer>& tileInfoBuffers,
                                const glm::vec3& cameraPos,
                                const SystemLifecycleHelper::PipelineHandles& computeHandles);

    // Descriptor set access
    VkDescriptorSet getDescriptorSet(uint32_t index) const { return descriptorSets_[index]; }

    // Tiled pipeline access
    bool hasTiledPipeline() const { return tiledPipeline_.has_value(); }
    vk::Pipeline getTiledPipeline() const { return **tiledPipeline_; }

    void cleanup() { tiledPipeline_.reset(); }

private:
    std::vector<VkDescriptorSet> descriptorSets_;
    std::optional<vk::raii::Pipeline> tiledPipeline_;
};
