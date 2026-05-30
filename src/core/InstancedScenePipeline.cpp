#include "InstancedScenePipeline.h"
#include "ScenePipeline.h"
#include "VulkanContext.h"
#include "GPUSceneBuffer.h"
#include "UBOs.h"
#include "Bindings.h"
#include "vulkan/PipelineLayoutBuilder.h"
#include "GraphicsPipelineFactory.h"
#include "Mesh.h"
#include <SDL3/SDL.h>

bool InstancedScenePipeline::initLayout(VulkanContext& context) {
    VkDevice device = context.getVkDevice();

    if (!createDescriptorSetLayouts(device, context.getRaiiDevice())) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool InstancedScenePipeline::createDescriptorSetLayouts(VkDevice device, const vk::raii::Device& raiiDevice) {
    // Set 0: the exact common scene bindings (identical to ScenePipeline), so material
    // descriptor sets from MaterialRegistry bind to this pipeline unchanged.
    {
        DescriptorManager::LayoutBuilder builder(device);
        ScenePipeline::addCommonDescriptorBindings(builder);
        VkDescriptorSetLayout rawLayout = builder.build();
        if (rawLayout == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "InstancedScenePipeline: failed to create common (set 0) descriptor layout");
            return false;
        }
        commonSetLayout_.emplace(raiiDevice, rawLayout);
    }

    // Set 1: the scene instance SSBO only, bound once per frame.
    {
        DescriptorManager::LayoutBuilder builder(device);
        builder.addBinding(Bindings::SCENE_INSTANCE_BUFFER,
                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        VkDescriptorSetLayout rawLayout = builder.build();
        if (rawLayout == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "InstancedScenePipeline: failed to create instance (set 1) descriptor layout");
            return false;
        }
        instanceSetLayout_.emplace(raiiDevice, rawLayout);
    }

    return true;
}

bool InstancedScenePipeline::createGraphicsPipeline(VulkanContext& context, VkRenderPass hdrRenderPass,
                                                    const std::string& resourcePath) {
    if (!initialized_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "InstancedScenePipeline::createGraphicsPipeline: not initialized");
        return false;
    }

    vk::Device device(context.getVkDevice());
    VkExtent2D swapchainExtent = context.getVkSwapchainExtent();

    // Two descriptor sets (set 0 = material/common, set 1 = instance buffer).
    // No push-constant range: per-object data is read from the instance SSBO.
    auto layoutOpt = PipelineLayoutBuilder(context.getRaiiDevice())
        .addDescriptorSetLayout(**commonSetLayout_)
        .addDescriptorSetLayout(**instanceSetLayout_)
        .build();

    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "InstancedScenePipeline: failed to create pipeline layout");
        return false;
    }
    pipelineLayout_ = std::move(layoutOpt);

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    GraphicsPipelineFactory factory(device);
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Default)
        .setShaders(resourcePath + "/shaders/shader_instanced.vert.spv",
                    resourcePath + "/shaders/shader_instanced.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(**pipelineLayout_)
        .setExtent(swapchainExtent)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .build(rawPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "InstancedScenePipeline: failed to create graphics pipeline");
        return false;
    }

    graphicsPipeline_.emplace(context.getRaiiDevice(), rawPipeline);
    return true;
}

bool InstancedScenePipeline::createInstanceDescriptorSets(VkDevice device, DescriptorManager::Pool& pool,
                                                          GPUSceneBuffer& sceneBuffer, uint32_t framesInFlight) {
    if (!instanceSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "InstancedScenePipeline::createInstanceDescriptorSets: layout not initialized");
        return false;
    }

    instanceDescriptorSets_ = pool.allocate(**instanceSetLayout_, framesInFlight);
    if (instanceDescriptorSets_.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "InstancedScenePipeline: failed to allocate instance descriptor sets");
        instanceDescriptorSets_.clear();
        return false;
    }

    // The instance buffers are created once and stable, so write the sets once.
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        DescriptorManager::SetWriter(device, instanceDescriptorSets_[i])
            .writeBuffer(Bindings::SCENE_INSTANCE_BUFFER,
                         sceneBuffer.getInstanceBuffer(i), 0,
                         sceneBuffer.getInstanceBufferSize(),
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    SDL_Log("InstancedScenePipeline: instance descriptor sets created (%u frames)", framesInFlight);
    return true;
}
