#include "AtmosphereLUTSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <vector>

using ShaderLoader::loadShaderModule;

namespace {
// Helper to create a compute pipeline and clean up shader module
bool createComputePipeline(VkDevice device, const std::string& shaderFile,
                           VkPipelineLayout layout, VkPipeline& outPipeline,
                           const char* pipelineName) {
    auto shaderModule = loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_Log("Failed to load %s shader", pipelineName);
        return false;
    }

    vk::Device vkDevice(device);

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(layout);

    auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
    vkDevice.destroyShaderModule(*shaderModule);

    if (result.result != vk::Result::eSuccess) {
        SDL_Log("Failed to create %s pipeline", pipelineName);
        return false;
    }

    outPipeline = result.value;
    return true;
}
} // anonymous namespace

bool AtmosphereLUTSystem::createComputePipelines() {
    // Create transmittance pipeline
    if (!createComputePipeline(device, shaderPath + "/transmittance_lut.comp.spv",
                               transmittancePipelineLayout, transmittancePipeline,
                               "transmittance")) {
        return false;
    }

    // Create multi-scatter pipeline
    if (!createComputePipeline(device, shaderPath + "/multiscatter_lut.comp.spv",
                               multiScatterPipelineLayout, multiScatterPipeline,
                               "multi-scatter")) {
        return false;
    }

    // Create sky-view pipeline
    if (!createComputePipeline(device, shaderPath + "/skyview_lut.comp.spv",
                               skyViewPipelineLayout, skyViewPipeline,
                               "sky-view")) {
        return false;
    }

    // Create irradiance pipeline
    if (!createComputePipeline(device, shaderPath + "/irradiance_lut.comp.spv",
                               irradiancePipelineLayout, irradiancePipeline,
                               "irradiance")) {
        return false;
    }

    // Create cloud map pipeline
    if (!createComputePipeline(device, shaderPath + "/cloudmap_lut.comp.spv",
                               cloudMapPipelineLayout, cloudMapPipeline,
                               "cloud map")) {
        return false;
    }

    return true;
}
