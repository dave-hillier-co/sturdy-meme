#include "TerrainPipelines.h"
#include "TerrainSystem.h"  // For push constant structs and SubgroupCapabilities
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <array>
#include <vulkan/vulkan.hpp>

using ShaderLoader::loadShaderModule;

std::unique_ptr<TerrainPipelines> TerrainPipelines::create(const InitInfo& info) {
    std::unique_ptr<TerrainPipelines> pipelines(new TerrainPipelines());
    if (!pipelines->initInternal(info)) {
        return nullptr;
    }
    return pipelines;
}

bool TerrainPipelines::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainPipelines: raiiDevice is null");
        return false;
    }
    raiiDevice_ = info.raiiDevice;
    device = info.device;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    computeDescriptorSetLayout = info.computeDescriptorSetLayout;
    renderDescriptorSetLayout = info.renderDescriptorSetLayout;
    shaderPath = info.shaderPath;
    useMeshlets = info.useMeshlets;
    meshletIndexCount = info.meshletIndexCount;
    subgroupCaps = info.subgroupCaps;

    if (!createDispatcherPipeline()) return false;
    if (!createSubdivisionPipeline()) return false;
    if (!createSumReductionPipelines()) return false;
    if (!createFrustumCullPipelines()) return false;
    if (!createRenderPipeline()) return false;
    if (!createWireframePipeline()) return false;
    if (!createShadowPipeline()) return false;

    if (useMeshlets) {
        if (!createMeshletRenderPipeline()) return false;
        if (!createMeshletWireframePipeline()) return false;
        if (!createMeshletShadowPipeline()) return false;
    }

    if (!createShadowCullPipelines()) return false;

    return true;
}

bool TerrainPipelines::createDispatcherPipeline() {
    auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_dispatcher.comp.spv");
    if (!shaderModule) return false;

    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainDispatcherPushConstants));

    vk::DescriptorSetLayout setLayout(computeDescriptorSetLayout);
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout)
        .setPushConstantRanges(pushConstantRange);

    try {
        dispatcherPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create dispatcher pipeline layout: %s", e.what());
        vk::Device(device).destroyShaderModule(*shaderModule);
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(static_cast<vk::ShaderModule>(*shaderModule))
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**dispatcherPipelineLayout_);

    try {
        dispatcherPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create dispatcher pipeline: %s", e.what());
        vk::Device(device).destroyShaderModule(*shaderModule);
        return false;
    }
    vk::Device(device).destroyShaderModule(*shaderModule);

    return true;
}

bool TerrainPipelines::createSubdivisionPipeline() {
    auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_subdivision.comp.spv");
    if (!shaderModule) return false;

    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainSubdivisionPushConstants));

    vk::DescriptorSetLayout setLayout(computeDescriptorSetLayout);
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout)
        .setPushConstantRanges(pushConstantRange);

    try {
        subdivisionPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subdivision pipeline layout: %s", e.what());
        vk::Device(device).destroyShaderModule(*shaderModule);
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(static_cast<vk::ShaderModule>(*shaderModule))
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**subdivisionPipelineLayout_);

    try {
        subdivisionPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subdivision pipeline: %s", e.what());
        vk::Device(device).destroyShaderModule(*shaderModule);
        return false;
    }
    vk::Device(device).destroyShaderModule(*shaderModule);

    return true;
}

bool TerrainPipelines::createSumReductionPipelines() {
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainSumReductionPushConstants));

    vk::DescriptorSetLayout setLayout(computeDescriptorSetLayout);
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout)
        .setPushConstantRanges(pushConstantRange);

    try {
        sumReductionPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sum reduction pipeline layout: %s", e.what());
        return false;
    }

    // Prepass pipeline
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass.comp.spv");
        if (!shaderModule) return false;

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(**sumReductionPipelineLayout_);

        try {
            sumReductionPrepassPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sum reduction prepass pipeline: %s", e.what());
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        vk::Device(device).destroyShaderModule(*shaderModule);
    }

    // Subgroup-optimized prepass pipeline (processes 13 levels instead of 5)
    if (subgroupCaps && subgroupCaps->hasSubgroupArithmetic) {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass_subgroup.comp.spv");
        if (shaderModule) {
            auto stageInfo = vk::PipelineShaderStageCreateInfo{}
                .setStage(vk::ShaderStageFlagBits::eCompute)
                .setModule(static_cast<vk::ShaderModule>(*shaderModule))
                .setPName("main");

            auto pipelineInfo = vk::ComputePipelineCreateInfo{}
                .setStage(stageInfo)
                .setLayout(**sumReductionPipelineLayout_);

            try {
                sumReductionPrepassSubgroupPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
                SDL_Log("TerrainPipelines: Using subgroup-optimized sum reduction prepass");
            } catch (const vk::SystemError&) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subgroup prepass pipeline, using fallback");
            }
            vk::Device(device).destroyShaderModule(*shaderModule);
        }
    }

    // Regular sum reduction pipeline (legacy single-level per dispatch)
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction.comp.spv");
        if (!shaderModule) return false;

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(**sumReductionPipelineLayout_);

        try {
            sumReductionPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sum reduction pipeline: %s", e.what());
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        vk::Device(device).destroyShaderModule(*shaderModule);
    }

    // Batched sum reduction pipeline (multi-level per dispatch using shared memory)
    {
        // Create pipeline layout for batched push constants
        auto batchedPushConstantRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(TerrainSumReductionBatchedPushConstants));

        vk::DescriptorSetLayout batchedSetLayout(computeDescriptorSetLayout);
        auto batchedLayoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(batchedSetLayout)
            .setPushConstantRanges(batchedPushConstantRange);

        try {
            sumReductionBatchedPipelineLayout_.emplace(*raiiDevice_, batchedLayoutInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create batched sum reduction pipeline layout: %s", e.what());
            return false;
        }

        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_batched.comp.spv");
        if (!shaderModule) return false;

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(**sumReductionBatchedPipelineLayout_);

        try {
            sumReductionBatchedPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create batched sum reduction pipeline: %s", e.what());
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        vk::Device(device).destroyShaderModule(*shaderModule);
    }

    return true;
}

bool TerrainPipelines::createFrustumCullPipelines() {
    // Frustum cull pipeline (with push constants for dispatch calculation)
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_frustum_cull.comp.spv");
        if (!shaderModule) return false;

        auto pushConstantRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(TerrainFrustumCullPushConstants));

        vk::DescriptorSetLayout setLayout(computeDescriptorSetLayout);
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(setLayout)
            .setPushConstantRanges(pushConstantRange);

        try {
            frustumCullPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create frustum cull pipeline layout: %s", e.what());
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(**frustumCullPipelineLayout_);

        try {
            frustumCullPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create frustum cull pipeline: %s", e.what());
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        vk::Device(device).destroyShaderModule(*shaderModule);
    }

    // Prepare cull dispatch pipeline
    {
        auto shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_prepare_cull_dispatch.comp.spv");
        if (!shaderModule) return false;

        auto pushConstantRange = vk::PushConstantRange{}
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
            .setOffset(0)
            .setSize(sizeof(TerrainPrepareCullDispatchPushConstants));

        vk::DescriptorSetLayout setLayout(computeDescriptorSetLayout);
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(setLayout)
            .setPushConstantRanges(pushConstantRange);

        try {
            prepareDispatchPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create prepare dispatch pipeline layout: %s", e.what());
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(static_cast<vk::ShaderModule>(*shaderModule))
            .setPName("main");

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(**prepareDispatchPipelineLayout_);

        try {
            prepareDispatchPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create prepare dispatch pipeline: %s", e.what());
            vk::Device(device).destroyShaderModule(*shaderModule);
            return false;
        }
        vk::Device(device).destroyShaderModule(*shaderModule);
    }

    return true;
}

bool TerrainPipelines::createRenderPipeline() {
    // Create render pipeline layout (shared by render and wireframe pipelines)
    vk::DescriptorSetLayout setLayout(renderDescriptorSetLayout);
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout);

    try {
        renderPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render pipeline layout: %s", e.what());
        return false;
    }

    // Create filled render pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(PipelinePresets::filled(renderPass), **renderPipelineLayout_, rawPipeline)) {
        return false;
    }
    // Wrap the raw pipeline in vk::raii::Pipeline
    renderPipeline_.emplace(*raiiDevice_, rawPipeline);
    return true;
}

bool TerrainPipelines::createWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(PipelinePresets::wireframe(renderPass), **renderPipelineLayout_, rawPipeline)) {
        return false;
    }
    wireframePipeline_.emplace(*raiiDevice_, rawPipeline);
    return true;
}

bool TerrainPipelines::createShadowPipeline() {
    // Create shadow pipeline layout with push constants
    PipelineBuilder layoutBuilder(device);
    layoutBuilder.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainShadowPushConstants));

    VkPipelineLayout rawLayout = VK_NULL_HANDLE;
    if (!layoutBuilder.buildPipelineLayout({renderDescriptorSetLayout}, rawLayout)) {
        return false;
    }
    shadowPipelineLayout_.emplace(*raiiDevice_, rawLayout);

    // Create shadow pipeline
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(PipelinePresets::shadow(shadowRenderPass), **shadowPipelineLayout_, rawPipeline)) {
        return false;
    }
    shadowPipeline_.emplace(*raiiDevice_, rawPipeline);
    return true;
}

bool TerrainPipelines::createMeshletRenderPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::filled(renderPass);
    cfg.useMeshletVertexInput = true;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(cfg, **renderPipelineLayout_, rawPipeline)) {
        return false;
    }
    meshletRenderPipeline_.emplace(*raiiDevice_, rawPipeline);
    return true;
}

bool TerrainPipelines::createMeshletWireframePipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::wireframe(renderPass);
    cfg.useMeshletVertexInput = true;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(cfg, **renderPipelineLayout_, rawPipeline)) {
        return false;
    }
    meshletWireframePipeline_.emplace(*raiiDevice_, rawPipeline);
    return true;
}

bool TerrainPipelines::createMeshletShadowPipeline() {
    PipelineBuilder builder(device);
    builder.addShaderStage(shaderPath + "/terrain/terrain_meshlet_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
           .addShaderStage(shaderPath + "/terrain/terrain_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto cfg = PipelinePresets::shadow(shadowRenderPass);
    cfg.useMeshletVertexInput = true;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(cfg, **shadowPipelineLayout_, rawPipeline)) {
        return false;
    }
    meshletShadowPipeline_.emplace(*raiiDevice_, rawPipeline);
    return true;
}

bool TerrainPipelines::createShadowCullPipelines() {
    // Create shadow cull compute pipeline
    auto cullShaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_cull.comp.spv");
    if (!cullShaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load shadow cull compute shader");
        return false;
    }

    // Pipeline layout for shadow cull compute
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TerrainShadowCullPushConstants));

    vk::DescriptorSetLayout setLayout(computeDescriptorSetLayout);
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout)
        .setPushConstantRanges(pushConstantRange);

    try {
        shadowCullPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        vk::Device(device).destroyShaderModule(*cullShaderModule);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull pipeline layout: %s", e.what());
        return false;
    }

    // Specialization constant for meshlet index count
    auto specEntry = vk::SpecializationMapEntry{}
        .setConstantID(0)
        .setOffset(0)
        .setSize(sizeof(uint32_t));

    auto specInfo = vk::SpecializationInfo{}
        .setMapEntries(specEntry)
        .setDataSize(sizeof(uint32_t))
        .setPData(&meshletIndexCount);

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(static_cast<vk::ShaderModule>(*cullShaderModule))
        .setPName("main")
        .setPSpecializationInfo(&specInfo);

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**shadowCullPipelineLayout_);

    try {
        shadowCullPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError& e) {
        vk::Device(device).destroyShaderModule(*cullShaderModule);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow cull compute pipeline: %s", e.what());
        return false;
    }
    vk::Device(device).destroyShaderModule(*cullShaderModule);

    vk::Device vkDevice(device);

    // Create shadow culled graphics pipeline (non-meshlet)
    auto shadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow_culled.vert.spv");
    auto shadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
    if (!shadowCulledVertModule || !shadowFragModule) {
        if (shadowCulledVertModule) vkDevice.destroyShaderModule(*shadowCulledVertModule);
        if (shadowFragModule) vkDevice.destroyShaderModule(*shadowFragModule);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load shadow culled shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(static_cast<vk::ShaderModule>(*shadowCulledVertModule))
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(static_cast<vk::ShaderModule>(*shadowFragModule))
            .setPName("main")
    };

    // No vertex input for non-meshlet (generated in shader)
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eFront)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(VK_TRUE);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(0);

    std::array<vk::DynamicState, 3> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eDepthBias
    };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    auto gfxPipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(shaderStages)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(**shadowPipelineLayout_)
        .setRenderPass(shadowRenderPass)
        .setSubpass(0);

    auto createResult = vkDevice.createGraphicsPipeline(nullptr, gfxPipelineInfo);
    vkDevice.destroyShaderModule(*shadowCulledVertModule);
    vkDevice.destroyShaderModule(*shadowFragModule);

    if (createResult.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow culled graphics pipeline");
        return false;
    }
    shadowCulledPipeline_.emplace(*raiiDevice_, createResult.value);

    // Create meshlet shadow culled pipeline (if meshlets enabled)
    if (useMeshlets) {
        auto meshletShadowCulledVertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_meshlet_shadow_culled.vert.spv");
        auto meshletShadowFragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
        if (!meshletShadowCulledVertModule || !meshletShadowFragModule) {
            if (meshletShadowCulledVertModule) vkDevice.destroyShaderModule(*meshletShadowCulledVertModule);
            if (meshletShadowFragModule) vkDevice.destroyShaderModule(*meshletShadowFragModule);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load meshlet shadow culled shaders");
            return false;
        }

        shaderStages[0].setModule(static_cast<vk::ShaderModule>(*meshletShadowCulledVertModule));
        shaderStages[1].setModule(static_cast<vk::ShaderModule>(*meshletShadowFragModule));

        // Meshlet vertex input: vec2 for local UV
        auto bindingDesc = vk::VertexInputBindingDescription{}
            .setBinding(0)
            .setStride(sizeof(glm::vec2))
            .setInputRate(vk::VertexInputRate::eVertex);

        auto attrDesc = vk::VertexInputAttributeDescription{}
            .setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32Sfloat)
            .setOffset(0);

        vertexInputInfo
            .setVertexBindingDescriptions(bindingDesc)
            .setVertexAttributeDescriptions(attrDesc);

        gfxPipelineInfo.setStages(shaderStages);

        auto meshletCreateResult = vkDevice.createGraphicsPipeline(nullptr, gfxPipelineInfo);
        vkDevice.destroyShaderModule(*meshletShadowCulledVertModule);
        vkDevice.destroyShaderModule(*meshletShadowFragModule);

        if (meshletCreateResult.result != vk::Result::eSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet shadow culled graphics pipeline");
            return false;
        }
        meshletShadowCulledPipeline_.emplace(*raiiDevice_, meshletCreateResult.value);
    }

    SDL_Log("TerrainPipelines: Shadow culling pipelines created successfully");
    return true;
}
