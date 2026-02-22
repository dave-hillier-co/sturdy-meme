#include "GrassShadowPass.h"
#include "GrassBuffers.h"
#include "DescriptorManager.h"
#include "PipelineBuilder.h"
#include "QueueSubmitDiagnostics.h"
#include "vulkan/PipelineLayoutBuilder.h"
#include <SDL3/SDL.h>

namespace {
struct DescriptorBindingInfo {
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stageFlags;
    uint32_t count = 1;
};

VkDescriptorSetLayout buildDescriptorSetLayout(
    VkDevice device,
    const DescriptorBindingInfo* bindings,
    size_t bindingCount
) {
    DescriptorManager::LayoutBuilder builder(device);
    for (size_t i = 0; i < bindingCount; ++i) {
        const auto& binding = bindings[i];
        builder.addBinding(binding.binding, binding.type, binding.stageFlags, binding.count);
    }
    return builder.build();
}
}  // namespace

bool GrassShadowPass::createPipeline(const vk::raii::Device& device, vk::Device rawDevice,
                                      const std::string& shaderPath, vk::RenderPass shadowRenderPass,
                                      uint32_t shadowMapSize) {
    const std::array<DescriptorBindingInfo, 3> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT}
    }};

    VkDescriptorSetLayout rawDescSetLayout =
        buildDescriptorSetLayout(rawDevice, bindings.data(), bindings.size());
    if (rawDescSetLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass shadow descriptor set layout");
        return false;
    }

    descriptorSetLayout_.emplace(device, rawDescSetLayout);

    PipelineBuilder builder(rawDevice);
    builder.addShaderStage(shaderPath + "/grass_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(shaderPath + "/grass_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleStrip);

    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(shadowMapSize))
        .setHeight(static_cast<float>(shadowMapSize))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({shadowMapSize, shadowMapSize});

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewports(viewport)
        .setScissors(scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(true)
        .setDepthBiasConstantFactor(GrassConstants::SHADOW_DEPTH_BIAS_CONSTANT)
        .setDepthBiasSlopeFactor(GrassConstants::SHADOW_DEPTH_BIAS_SLOPE);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{};

    auto layoutOpt = PipelineLayoutBuilder(device)
        .addDescriptorSetLayout(**descriptorSetLayout_)
        .addPushConstantRange<GrassPushConstants>(vk::ShaderStageFlagBits::eVertex)
        .build();

    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass shadow pipeline layout");
        return false;
    }

    pipelineLayout_ = std::move(layoutOpt);

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setRenderPass(shadowRenderPass)
        .setSubpass(0);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(pipelineInfo, **pipelineLayout_, rawPipeline)) {
        return false;
    }
    pipeline_.emplace(device, rawPipeline);

    return true;
}

bool GrassShadowPass::allocateDescriptorSets(DescriptorManager::Pool* pool, uint32_t count) {
    descriptorSets_.resize(count);
    for (uint32_t set = 0; set < count; set++) {
        descriptorSets_[set] = pool->allocateSingle(**descriptorSetLayout_);
        if (!descriptorSets_[set]) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate grass shadow descriptor set (set %u)", set);
            return false;
        }
    }
    SDL_Log("GrassShadowPass: allocated %u shadow descriptor sets", count);
    return true;
}

void GrassShadowPass::updateDescriptorSets(vk::Device device, uint32_t count,
                                             const std::vector<vk::Buffer>& rendererUniformBuffers,
                                             const GrassBuffers& buffers,
                                             const std::vector<vk::Buffer>& windBuffers) {
    for (uint32_t set = 0; set < count; set++) {
        DescriptorManager::SetWriter shadowWriter(device, descriptorSets_[set]);
        shadowWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 160);  // sizeof(UniformBufferObject)
        shadowWriter.writeBuffer(1, buffers.instanceBuffers().buffers[set], 0,
                                 sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES,
                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        shadowWriter.writeBuffer(2, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        shadowWriter.update();
    }
}

void GrassShadowPass::recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                  uint32_t cascadeIndex, uint32_t readSet,
                                  const GrassBuffers& buffers,
                                  const std::vector<vk::Buffer>& rendererUniformBuffers,
                                  vk::Device device) {
    // Update shadow descriptor set to use this frame's renderer UBO
    if (frameIndex < rendererUniformBuffers.size()) {
        DescriptorManager::SetWriter(device, descriptorSets_[readSet])
            .writeBuffer(0, rendererUniformBuffers[frameIndex], 0, 160)
            .update();
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           **pipelineLayout_, 0,
                           descriptorSets_[readSet], {});

    GrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.cascadeIndex = static_cast<int>(cascadeIndex);
    cmd.pushConstants<GrassPushConstants>(
        **pipelineLayout_,
        vk::ShaderStageFlagBits::eVertex,
        0, grassPush);

    cmd.drawIndirect(buffers.indirectBuffers().buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
    DIAG_RECORD_DRAW();
}
