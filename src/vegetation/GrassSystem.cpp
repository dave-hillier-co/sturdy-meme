#include "GrassSystem.h"
#include "DisplacementSystem.h"
#include "GrassTileManager.h"
#include "WindSystem.h"
#include "InitContext.h"
#include "CullCommon.h"
#include "PipelineBuilder.h"
#include "DescriptorManager.h"
#include "QueueSubmitDiagnostics.h"
#include "UBOs.h"
#include "vulkan/PipelineLayoutBuilder.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

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

std::optional<VkPipelineLayout> buildPipelineLayoutRaw(
    const vk::raii::Device& device,
    vk::DescriptorSetLayout layout,
    vk::ShaderStageFlags pushStages,
    uint32_t pushSize
) {
    auto layoutOpt = PipelineLayoutBuilder(device)
        .addDescriptorSetLayout(layout)
        .addPushConstantRange(pushStages, pushSize)
        .build();

    if (!layoutOpt) {
        return std::nullopt;
    }

    return layoutOpt->release();
}
}  // namespace

GrassSystem::GrassSystem(ConstructToken) {}

std::unique_ptr<GrassSystem> GrassSystem::create(const InitInfo& info) {
    auto system = std::make_unique<GrassSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::optional<GrassSystem::Bundle> GrassSystem::createWithDependencies(
    const InitContext& ctx,
    vk::RenderPass hdrRenderPass,
    vk::RenderPass shadowRenderPass,
    uint32_t shadowMapSize
) {
    // Create wind system
    WindSystem::InitInfo windInfo{};
    windInfo.device = ctx.device;
    windInfo.allocator = ctx.allocator;
    windInfo.framesInFlight = ctx.framesInFlight;

    auto windSystem = WindSystem::create(windInfo);
    if (!windSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WindSystem");
        return std::nullopt;
    }

    // Create grass system
    InitInfo grassInfo{};
    grassInfo.device = ctx.device;
    grassInfo.allocator = ctx.allocator;
    grassInfo.renderPass = hdrRenderPass;
    grassInfo.shadowRenderPass = shadowRenderPass;
    grassInfo.descriptorPool = ctx.descriptorPool;
    grassInfo.extent = ctx.extent;
    grassInfo.shadowMapSize = shadowMapSize;
    grassInfo.shaderPath = ctx.shaderPath;
    grassInfo.framesInFlight = ctx.framesInFlight;
    grassInfo.raiiDevice = ctx.raiiDevice;

    auto grassSystem = create(grassInfo);
    if (!grassSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GrassSystem");
        return std::nullopt;
    }

    // Wire environment settings from wind to grass
    grassSystem->setEnvironmentSettings(&windSystem->getEnvironmentSettings());

    return Bundle{
        std::move(windSystem),
        std::move(grassSystem)
    };
}

GrassSystem::~GrassSystem() {
    cleanup();
}

bool GrassSystem::initInternal(const InitInfo& info) {
    SDL_Log("GrassSystem::init() starting, device=%p, pool=%p", (void*)(VkDevice)info.device, (void*)info.descriptorPool);
    shadowRenderPass_ = info.shadowRenderPass;
    shadowMapSize_ = info.shadowMapSize;

    // Store init info for accessors used during initialization
    device_ = info.device;
    allocator_ = info.allocator;
    renderPass_ = info.renderPass;
    descriptorPool_ = info.descriptorPool;
    extent_ = info.extent;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GrassSystem requires raiiDevice");
        return false;
    }

    // Set up lifecycle hooks - delegates to composed components
    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() {
        return buffers_.create(allocator_, framesInFlight_);
    };
    hooks.createComputeDescriptorSetLayout = [this]() {
        return computePass_.createDescriptorSetLayout(device_, lifecycle_.getComputePipeline());
    };
    hooks.createComputePipeline = [this]() {
        return computePass_.createPipeline(*raiiDevice_, shaderPath_, lifecycle_.getComputePipeline());
    };
    hooks.createGraphicsDescriptorSetLayout = [this]() {
        return createGraphicsDescriptorSetLayout(lifecycle_.getGraphicsPipeline());
    };
    hooks.createGraphicsPipeline = [this]() {
        return createGraphicsPipeline(lifecycle_.getGraphicsPipeline());
    };
    hooks.createExtraPipelines = [this]() {
        return createExtraPipelines(lifecycle_.getComputePipeline(),
                                     lifecycle_.getGraphicsPipeline());
    };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { buffers_.destroy(allocator); };

    // Build lifecycle init info from GrassSystem::InitInfo
    SystemLifecycleHelper::InitInfo lifecycleInfo{};
    lifecycleInfo.device = info.device;
    lifecycleInfo.allocator = info.allocator;
    lifecycleInfo.renderPass = info.renderPass;
    lifecycleInfo.descriptorPool = info.descriptorPool;
    lifecycleInfo.extent = VkExtent2D{info.extent.width, info.extent.height};
    lifecycleInfo.shaderPath = info.shaderPath;
    lifecycleInfo.framesInFlight = info.framesInFlight;
    lifecycleInfo.raiiDevice = info.raiiDevice;

    if (!lifecycle_.init(lifecycleInfo, hooks)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GrassSystem: lifecycle_.init() failed");
        return false;
    }

    SDL_Log("GrassSystem::init() - lifecycle initialized successfully");

    // Write compute descriptor sets now that lifecycle is fully initialized
    computePass_.writeInitialDescriptorSets(device_, buffers_, buffers_.getBufferSetCount());
    SDL_Log("GrassSystem::init() - done writing compute descriptor sets");
    return true;
}

void GrassSystem::cleanup() {
    if (!device_) return;  // Not initialized

    // Reset composed component RAII resources
    computePass_.cleanup();
    shadowPass_.cleanup();

    // Destroy lifecycle resources (pipelines and buffers)
    lifecycle_.destroy();

    device_ = nullptr;
    raiiDevice_ = nullptr;
}

bool GrassSystem::createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    const std::array<DescriptorBindingInfo, 10> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT}
    }};

    handles.descriptorSetLayout =
        buildDescriptorSetLayout(getDevice(), bindings.data(), bindings.size());

    if (handles.descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass graphics descriptor set layout");
        return false;
    }

    return true;
}

bool GrassSystem::createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/grass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleStrip);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachments(colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GrassSystem requires raiiDevice for graphics pipeline");
        return false;
    }

    auto layoutOpt = buildPipelineLayoutRaw(
        *raiiDevice_,
        vk::DescriptorSetLayout(handles.descriptorSetLayout),
        vk::ShaderStageFlagBits::eVertex,
        sizeof(TiledGrassPushConstants));

    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass graphics pipeline layout");
        return false;
    }

    handles.pipelineLayout = *layoutOpt;

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setRenderPass(getRenderPass())
        .setSubpass(0);

    return builder.buildGraphicsPipeline(pipelineInfo, handles.pipelineLayout, handles.pipeline);
}

bool GrassSystem::createDescriptorSets() {
    uint32_t bufferSetCount = getFramesInFlight();

    SDL_Log("GrassSystem::createDescriptorSets - pool=%p, bufferSetCount=%u", (void*)getDescriptorPool(), bufferSetCount);

    // Allocate compute descriptor sets via component
    if (!computePass_.allocateDescriptorSets(getDescriptorPool(),
                                              lifecycle_.getComputePipeline().descriptorSetLayout,
                                              bufferSetCount)) {
        return false;
    }

    // Allocate graphics descriptor sets
    graphicsDescriptorSets_.resize(bufferSetCount);
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        graphicsDescriptorSets_[set] = getDescriptorPool()->allocateSingle(lifecycle_.getGraphicsPipeline().descriptorSetLayout);
        if (!graphicsDescriptorSets_[set]) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate grass graphics descriptor set (set %u)", set);
            return false;
        }
    }
    SDL_Log("GrassSystem::createDescriptorSets - allocated %u graphics sets", bufferSetCount);

    // Allocate shadow descriptor sets via component
    if (!shadowPass_.allocateDescriptorSets(getDescriptorPool(), bufferSetCount)) {
        return false;
    }

    return true;
}

bool GrassSystem::createExtraPipelines(SystemLifecycleHelper::PipelineHandles& computeHandles,
                                        SystemLifecycleHelper::PipelineHandles& graphicsHandles) {
    // Create shadow pipeline via component
    if (!shadowPass_.createPipeline(*raiiDevice_, getDevice(), getShaderPath(),
                                     shadowRenderPass_, shadowMapSize_)) {
        return false;
    }

    // Create tiled grass compute pipeline via component
    if (tiledModeEnabled_) {
        if (!computePass_.createTiledPipeline(*raiiDevice_, getShaderPath(),
                                               computeHandles.pipelineLayout)) {
            return false;
        }

        // Initialize tile manager
        GrassTileManager::InitInfo tileInfo{};
        tileInfo.device = getDevice();
        tileInfo.allocator = getAllocator();
        tileInfo.descriptorPool = getDescriptorPool();
        tileInfo.framesInFlight = getFramesInFlight();
        tileInfo.shaderPath = getShaderPath();
        tileInfo.computeDescriptorSetLayout = computeHandles.descriptorSetLayout;
        tileInfo.computePipelineLayout = computeHandles.pipelineLayout;
        tileInfo.computePipeline = computePass_.getTiledPipeline();
        tileInfo.graphicsDescriptorSetLayout = graphicsHandles.descriptorSetLayout;
        tileInfo.graphicsPipelineLayout = graphicsHandles.pipelineLayout;
        tileInfo.graphicsPipeline = graphicsHandles.pipeline;

        tileManager_ = std::make_unique<GrassTileManager>();
        if (!tileManager_->init(tileInfo)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GrassTileManager");
            tileManager_.reset();
            tiledModeEnabled_ = false;
        }
    }

    return true;
}

void GrassSystem::updateDescriptorSets(vk::Device dev, const std::vector<vk::Buffer>& rendererUniformBuffers,
                                        vk::ImageView shadowMapView, vk::Sampler shadowSampler,
                                        const std::vector<vk::Buffer>& windBuffers,
                                        const std::vector<vk::Buffer>& lightBuffersParam,
                                        vk::ImageView terrainHeightMapViewParam, vk::Sampler terrainHeightMapSamplerParam,
                                        const std::vector<vk::Buffer>& snowBuffersParam,
                                        const std::vector<vk::Buffer>& cloudShadowBuffersParam,
                                        vk::ImageView cloudShadowMapView, vk::Sampler cloudShadowMapSampler,
                                        vk::ImageView tileArrayViewParam,
                                        vk::Sampler tileSamplerParam,
                                        const std::array<vk::Buffer, 3>& tileInfoBuffersParam,
                                        const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO,
                                        vk::ImageView holeMaskViewParam,
                                        vk::Sampler holeMaskSamplerParam) {
    // Store resources needed for later use
    terrainHeightMapView_ = terrainHeightMapViewParam;
    terrainHeightMapSampler_ = terrainHeightMapSamplerParam;
    tileArrayView_ = tileArrayViewParam;
    tileSampler_ = tileSamplerParam;
    tileInfoBuffers_.resize(tileInfoBuffersParam.size());
    for (size_t i = 0; i < tileInfoBuffersParam.size(); ++i) {
        tileInfoBuffers_[i] = tileInfoBuffersParam[i];
    }
    holeMaskView_ = holeMaskViewParam;
    holeMaskSampler_ = holeMaskSamplerParam;
    rendererUniformBuffers_ = rendererUniformBuffers;
    dynamicRendererUBO_ = dynamicRendererUBO;

    uint32_t bufferSetCount = buffers_.getBufferSetCount();

    // Update compute descriptor sets via component
    computePass_.updateDescriptorSets(dev, bufferSetCount,
                                       terrainHeightMapView_, terrainHeightMapSampler_,
                                       displacementSystem_,
                                       tileArrayView_, tileSampler_,
                                       tileInfoBuffers_,
                                       holeMaskView_, holeMaskSampler_);

    // Update graphics descriptor sets
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        DescriptorManager::SetWriter graphicsWriter(dev, getGraphicsDescriptorSet(set));
        if (dynamicRendererUBO && dynamicRendererUBO->isValid()) {
            graphicsWriter.writeBuffer(0, dynamicRendererUBO->buffer, 0, dynamicRendererUBO->alignedSize,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        } else {
            graphicsWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 160,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        }
        graphicsWriter.writeBuffer(1, buffers_.instanceBuffers().buffers[set], 0,
                                   sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES,
                                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeImage(2, shadowMapView, shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        graphicsWriter.writeBuffer(3, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        graphicsWriter.writeBuffer(4, lightBuffersParam[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeImage(6, cloudShadowMapView, cloudShadowMapSampler);
        if (screenShadowView_) {
            graphicsWriter.writeImage(7, screenShadowView_, screenShadowSampler_);
        } else {
            graphicsWriter.writeImage(7, cloudShadowMapView, cloudShadowMapSampler);
        }
        graphicsWriter.writeBuffer(10, snowBuffersParam[0], 0, sizeof(SnowUBO));
        graphicsWriter.writeBuffer(11, cloudShadowBuffersParam[0], 0, sizeof(CloudShadowUBO));
        graphicsWriter.update();
    }

    // Update shadow descriptor sets via component
    shadowPass_.updateDescriptorSets(dev, bufferSetCount, rendererUniformBuffers, buffers_, windBuffers);

    // Update tile manager descriptor sets if in tiled mode
    if (tiledModeEnabled_ && tileManager_) {
        uint32_t firstBufferSet = 0;
        tileManager_->setSharedBuffers(
            buffers_.instanceBuffers().buffers[firstBufferSet],
            buffers_.indirectBuffers().buffers[firstBufferSet]
        );

        std::vector<vk::Buffer> vkCullingBuffers(buffers_.uniformBuffers().buffers.begin(),
                                                   buffers_.uniformBuffers().buffers.end());
        std::vector<vk::Buffer> vkParamsBuffers(buffers_.paramsBuffers().buffers.begin(),
                                                  buffers_.paramsBuffers().buffers.end());

        std::array<vk::Buffer, 3> tileInfoArray{};
        for (size_t i = 0; i < tileInfoBuffers_.size() && i < 3; ++i) {
            tileInfoArray[i] = tileInfoBuffers_[i];
        }

        vk::ImageView dispView = displacementSystem_ ? displacementSystem_->getImageView() : vk::ImageView{};
        vk::Sampler dispSampler = displacementSystem_ ? displacementSystem_->getSampler() : vk::Sampler{};
        tileManager_->updateDescriptorSets(
            terrainHeightMapView_, terrainHeightMapSampler_,
            dispView, dispSampler,
            tileArrayView_, tileSampler_,
            tileInfoArray,
            vkCullingBuffers,
            vkParamsBuffers
        );
    }
}

void GrassSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                                  float terrainSize, float terrainHeightScale, float time) {
    lastCameraPos_ = cameraPos;
    buffers_.updateUniforms(frameIndex, cameraPos, viewProj, terrainSize, terrainHeightScale, time,
                             displacementSystem_);
}

void GrassSystem::recordResetAndCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time) {
    uint32_t writeSet = buffers_.getComputeBufferSet();

    // Update compute descriptor set with per-frame buffers before dispatch
    DescriptorManager::SetWriter writer(getDevice(), computePass_.getDescriptorSet(writeSet));
    writer.writeBuffer(2, buffers_.uniformBuffers().buffers[frameIndex], 0, sizeof(CullingUniforms));
    writer.writeBuffer(7, buffers_.paramsBuffers().buffers[frameIndex], 0, sizeof(GrassParams));
    if (!tileInfoBuffers_.empty() && tileInfoBuffers_.at(frameIndex)) {
        writer.writeBuffer(6, tileInfoBuffers_.at(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }
    writer.update();

    computePass_.recordResetAndCompute(cmd, frameIndex, time, buffers_, tileInfoBuffers_,
                                        lastCameraPos_, getComputePipelineHandles());
}

void GrassSystem::recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time) {
    uint32_t readSet = buffers_.getRenderBufferSet();

    vk::Extent2D ext = getExtent();
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(ext.width))
        .setHeight(static_cast<float>(ext.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    cmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(ext);
    cmd.setScissor(0, scissor);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, getGraphicsPipelineHandles().pipeline);

    VkDescriptorSet graphicsSet = getGraphicsDescriptorSet(readSet);

    if (dynamicRendererUBO_ && dynamicRendererUBO_->isValid()) {
        uint32_t dynamicOffset = dynamicRendererUBO_->getDynamicOffset(frameIndex);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               getGraphicsPipelineHandles().pipelineLayout, 0,
                               vk::DescriptorSet(graphicsSet), dynamicOffset);
    } else {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               getGraphicsPipelineHandles().pipelineLayout, 0,
                               vk::DescriptorSet(graphicsSet), {});
    }

    TiledGrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.tileOriginX = 0.0f;
    grassPush.tileOriginZ = 0.0f;
    grassPush.tileSize = GrassConstants::TILE_SIZE;
    grassPush.spacing = GrassConstants::SPACING;
    grassPush.tileIndex = 0;
    grassPush.unused1 = 0.0f;
    grassPush.unused2 = 0.0f;
    cmd.pushConstants<TiledGrassPushConstants>(
        getGraphicsPipelineHandles().pipelineLayout,
        vk::ShaderStageFlagBits::eVertex,
        0, grassPush);

    cmd.drawIndirect(buffers_.indirectBuffers().buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
    DIAG_RECORD_DRAW();
}

void GrassSystem::recordShadowDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex) {
    uint32_t readSet = buffers_.getRenderBufferSet();
    shadowPass_.recordDraw(cmd, frameIndex, time, cascadeIndex, readSet,
                            buffers_, rendererUniformBuffers_, getDevice());
}

void GrassSystem::setSnowMask(vk::Device device, vk::ImageView snowMaskView, vk::Sampler snowMaskSampler) {
    uint32_t bufferSetCount = buffers_.getBufferSetCount();
    for (uint32_t setIndex = 0; setIndex < bufferSetCount; setIndex++) {
        DescriptorManager::SetWriter(device, getGraphicsDescriptorSet(setIndex))
            .writeImage(5, snowMaskView, snowMaskSampler)
            .update();
    }
}

void GrassSystem::advanceBufferSet() {
    buffers_.advanceBufferSet();
}

vk::ImageView GrassSystem::getDisplacementImageView() const {
    return displacementSystem_ ? displacementSystem_->getImageView() : vk::ImageView{};
}

vk::Sampler GrassSystem::getDisplacementSampler() const {
    return displacementSystem_ ? displacementSystem_->getSampler() : vk::Sampler{};
}
