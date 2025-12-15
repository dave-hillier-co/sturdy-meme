#include "SkySystem.h"
#include "AtmosphereLUTSystem.h"
#include "GraphicsPipelineFactory.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include <SDL3/SDL.h>
#include <array>

bool SkySystem::init(const InitInfo& info) {
    device = info.device;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;
    hdrRenderPass = info.hdrRenderPass;

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;

    return true;
}

bool SkySystem::init(const InitContext& ctx, VkRenderPass hdrPass) {
    device = ctx.device;
    descriptorPool = ctx.descriptorPool;
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;
    extent = ctx.extent;
    hdrRenderPass = hdrPass;

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;

    return true;
}

void SkySystem::destroy(VkDevice device, VmaAllocator allocator) {
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    // Descriptor sets are freed when the pool is destroyed
    descriptorSets.clear();
}

bool SkySystem::createDescriptorSetLayout() {
    // Sky shader bindings:
    // 0: UBO (same as main shader)
    // 1: Transmittance LUT sampler
    // 2: Multi-scatter LUT sampler
    // 3: Sky-view LUT sampler (updated per-frame)
    // 4: Rayleigh Irradiance LUT sampler (Phase 4.1.9)
    // 5: Mie Irradiance LUT sampler (Phase 4.1.9)
    // 6: Cloud Map LUT sampler (Paraboloid projection, updated per-frame)

    auto makeBinding = [](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = stages;
        return b;
    };

    constexpr auto VF = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    constexpr auto F = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 7> bindings = {
        makeBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VF),
        makeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, F),
        makeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, F),
        makeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, F),
        makeBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, F),
        makeBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, F),
        makeBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, F)
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create sky descriptor set layout");
        return false;
    }

    // Create pipeline layout for sky shader
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create sky pipeline layout");
        return false;
    }

    return true;
}

bool SkySystem::createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                      VkDeviceSize uniformBufferSize,
                                      AtmosphereLUTSystem& atmosphereLUTSystem) {
    // Allocate sky descriptor sets using managed pool
    descriptorSets = descriptorPool->allocate(descriptorSetLayout, framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate sky descriptor sets");
        return false;
    }

    // Get LUT views and sampler from atmosphere system
    VkImageView transmittanceLUTView = atmosphereLUTSystem.getTransmittanceLUTView();
    VkImageView multiScatterLUTView = atmosphereLUTSystem.getMultiScatterLUTView();
    VkImageView skyViewLUTView = atmosphereLUTSystem.getSkyViewLUTView();
    VkImageView rayleighIrradianceLUTView = atmosphereLUTSystem.getRayleighIrradianceLUTView();
    VkImageView mieIrradianceLUTView = atmosphereLUTSystem.getMieIrradianceLUTView();
    VkImageView cloudMapLUTView = atmosphereLUTSystem.getCloudMapLUTView();
    VkSampler lutSampler = atmosphereLUTSystem.getLUTSampler();

    // Update each descriptor set
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, descriptorSets[i])
            .writeBuffer(0, uniformBuffers[i], 0, uniformBufferSize)
            .writeImage(1, transmittanceLUTView, lutSampler)
            .writeImage(2, multiScatterLUTView, lutSampler)
            .writeImage(3, skyViewLUTView, lutSampler)
            .writeImage(4, rayleighIrradianceLUTView, lutSampler)
            .writeImage(5, mieIrradianceLUTView, lutSampler)
            .writeImage(6, cloudMapLUTView, lutSampler)
            .update();
    }

    SDL_Log("Sky descriptor sets created with atmosphere LUTs (including cloud map)");
    return true;
}

bool SkySystem::createPipeline() {
    GraphicsPipelineFactory factory(device);

    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/sky.vert.spv", shaderPath + "/sky.frag.spv")
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(pipelineLayout)
        .setExtent(extent)
        .setDynamicViewport(true)
        .build(pipeline);

    if (!success) {
        SDL_Log("Failed to create sky pipeline");
        return false;
    }

    return true;
}

void SkySystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Set dynamic viewport and scissor to handle window resize
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
