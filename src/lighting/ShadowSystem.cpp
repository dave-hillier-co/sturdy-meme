#include "ShadowSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "Mesh.h"
#include "GraphicsPipelineFactory.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "debug/QueueSubmitDiagnostics.h"
#include "shaders/bindings.h"
#include "core/vulkan/DescriptorSetLayoutBuilder.h"
#include "core/vulkan/RenderPassBuilder.h"
#include "core/vulkan/DescriptorWriter.h"
#include "core/GPUSceneBuffer.h"
#include "culling/ShadowCullPass.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>

namespace {
// Component-wise near-equality for two 4x4 matrices (used for shadow change detection).
bool matricesNearlyEqual(const glm::mat4& a, const glm::mat4& b, float epsilon) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::fabs(a[c][r] - b[c][r]) > epsilon) return false;
        }
    }
    return true;
}

bool vec3NearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon) {
    return std::fabs(a.x - b.x) <= epsilon &&
           std::fabs(a.y - b.y) <= epsilon &&
           std::fabs(a.z - b.z) <= epsilon;
}
}  // namespace

// Factory implementations
std::unique_ptr<ShadowSystem> ShadowSystem::create(const InitInfo& info) {
    auto system = std::make_unique<ShadowSystem>(ConstructToken{}, info);
    if (!system->initialized_) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<ShadowSystem> ShadowSystem::create(const InitContext& ctx,
                                                    vk::DescriptorSetLayout mainDescriptorSetLayout_,
                                                    vk::DescriptorSetLayout skinnedDescriptorSetLayout_) {
    InitInfo info{};
    info.raiiDevice = ctx.raiiDevice;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.mainDescriptorSetLayout = mainDescriptorSetLayout_;
    info.skinnedDescriptorSetLayout = skinnedDescriptorSetLayout_;
    return create(info);
}

ShadowSystem::ShadowSystem(ConstructToken, const InitInfo& info)
    : initInfo_(info) {
    if (!initInfo_.device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem requires a valid vk::Device");
        return;
    }
    if (!initInfo_.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem requires raiiDevice");
        return;
    }

    if (!createShadowRenderPass()
        || !createShadowResources()
        || !createDynamicShadowResources()
        || !createInstancedShadowResources()
        || !createShadowPipeline()
        || !createSkinnedShadowPipeline()
        || !createDynamicShadowPipeline()
        || !createInstancedShadowPipeline()) {
        return;
    }

    initialized_ = true;
}

bool ShadowSystem::createShadowRenderPass() {
    // Depth-only render pass for shadow mapping, outputs to shader read for sampling
    auto renderPassOpt = RenderPassBuilder::depthOnly(vk::Format::eD32Sfloat)
        .build(*initInfo_.raiiDevice);

    if (!renderPassOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow render pass");
        return false;
    }
    shadowRenderPass_ = std::move(*renderPassOpt);
    return true;
}

std::optional<vk::raii::Framebuffer> ShadowSystem::createShadowFramebuffer(vk::ImageView layerView, uint32_t size) const {
    auto framebufferInfo = vk::FramebufferCreateInfo{}
        .setRenderPass(**shadowRenderPass_)
        .setAttachments(layerView)
        .setWidth(size)
        .setHeight(size)
        .setLayers(1);
    try {
        return vk::raii::Framebuffer(*initInfo_.raiiDevice, framebufferInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow framebuffer: %s", e.what());
        return std::nullopt;
    }
}

bool ShadowSystem::createShadowResources() {
    if (!initInfo_.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem::createShadowResources: raiiDevice is null");
        return false;
    }

    DepthArrayConfig cfg;
    cfg.extent = vk::Extent2D{SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};
    cfg.format = vk::Format::eD32Sfloat;
    cfg.arrayLayers = NUM_SHADOW_CASCADES;

    if (!::createDepthArrayResources(*initInfo_.raiiDevice, initInfo_.allocator, cfg, csmResources)) {
        return false;
    }

    // Create framebuffers for each cascade
    cascadeFramebuffers_.clear();
    cascadeFramebuffers_.reserve(NUM_SHADOW_CASCADES);
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        auto fb = createShadowFramebuffer(*csmResources.layerViews[i], SHADOW_MAP_SIZE);
        if (!fb) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cascade framebuffer %u", i);
            return false;
        }
        cascadeFramebuffers_.push_back(std::move(*fb));
    }
    return true;
}

void ShadowSystem::recordInitialClearIfNeeded(vk::CommandBuffer cmd) {
    if (csmInitialized_) return;
    csmInitialized_ = true;

    // Empty render passes clear each cascade to depth 1.0 (fully lit) and leave the
    // image in SHADER_READ_ONLY_OPTIMAL via the render pass final layout, matching
    // what the regular shadow pass produces.
    vk::CommandBuffer vkCmd(cmd);
    vk::ClearValue shadowClear;
    shadowClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    for (uint32_t cascade = 0; cascade < NUM_SHADOW_CASCADES; cascade++) {
        auto passInfo = vk::RenderPassBeginInfo{}
            .setRenderPass(**shadowRenderPass_)
            .setFramebuffer(*cascadeFramebuffers_[cascade])
            .setRenderArea({{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}})
            .setClearValues(shadowClear);
        vkCmd.beginRenderPass(passInfo, vk::SubpassContents::eInline);
        vkCmd.endRenderPass();
    }
}

bool ShadowSystem::createShadowPipelineCommon(
    const std::string& vertShader,
    const std::string& fragShader,
    vk::DescriptorSetLayout descriptorSetLayout,
    const VkVertexInputBindingDescription& binding,
    const std::vector<VkVertexInputAttributeDescription>& attributes,
    std::optional<vk::raii::PipelineLayout>& outLayout,
    std::optional<vk::raii::Pipeline>& outPipeline)
{
    outPipeline.reset();
    outLayout.reset();

    if (!PipelineLayoutBuilder(*initInfo_.raiiDevice)
            .addDescriptorSetLayout(descriptorSetLayout)
            .addPushConstantRange<ShadowPushConstants>(vk::ShaderStageFlagBits::eVertex)
            .buildInto(outLayout)) {
        return false;
    }

    GraphicsPipelineFactory factory(initInfo_.device);
    factory.applyPreset(GraphicsPipelineFactory::Preset::Shadow)
           .setShaders(initInfo_.shaderPath + "/" + vertShader, initInfo_.shaderPath + "/" + fragShader)
           .setRenderPass(**shadowRenderPass_)
           .setPipelineLayout(**outLayout)
           .setExtent({SHADOW_MAP_SIZE, SHADOW_MAP_SIZE})
           .setVertexInput({binding}, attributes)
           .setDepthBias(1.25f, 1.75f);

    vk::Pipeline rawPipeline;
    if (!factory.build(rawPipeline)) return false;
    outPipeline.emplace(*initInfo_.raiiDevice, rawPipeline);
    return true;
}

bool ShadowSystem::createShadowPipeline() {
    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    return createShadowPipelineCommon("shadow.vert.spv", "shadow.frag.spv",
        initInfo_.mainDescriptorSetLayout, binding, attrs, shadowPipelineLayout_, shadowPipeline_);
}

bool ShadowSystem::createSkinnedShadowPipeline() {
    if (!initInfo_.skinnedDescriptorSetLayout) {
        SDL_Log("Skinned shadow pipeline skipped (no skinned descriptor set layout)");
        return true;
    }
    auto binding = SkinnedVertex::getBindingDescription();
    auto attrsArr = SkinnedVertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    bool result = createShadowPipelineCommon("skinned_shadow.vert.spv", "shadow.frag.spv",
        initInfo_.skinnedDescriptorSetLayout, binding, attrs, skinnedShadowPipelineLayout_, skinnedShadowPipeline_);
    if (result) SDL_Log("Created skinned shadow pipeline for GPU-skinned character shadows");
    return result;
}

bool ShadowSystem::createDynamicShadowPipeline() {
    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());
    return createShadowPipelineCommon("shadow.vert.spv", "shadow.frag.spv",
        initInfo_.mainDescriptorSetLayout, binding, attrs, dynamicShadowPipelineLayout_, dynamicShadowPipeline_);
}

bool ShadowSystem::createDynamicShadowResources() {
    if (!initInfo_.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem::createDynamicShadowResources: raiiDevice is null");
        return false;
    }

    pointShadowResources.resize(initInfo_.framesInFlight);
    spotShadowResources.resize(initInfo_.framesInFlight);
    pointShadowFramebuffers_.clear();
    spotShadowFramebuffers_.clear();
    pointShadowFramebuffers_.resize(initInfo_.framesInFlight);
    spotShadowFramebuffers_.resize(initInfo_.framesInFlight);

    for (uint32_t frame = 0; frame < initInfo_.framesInFlight; frame++) {
        // Point lights: cubemap array (6 faces per light)
        DepthArrayConfig pointCfg;
        pointCfg.extent = vk::Extent2D{DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};
        pointCfg.format = vk::Format::eD32Sfloat;
        pointCfg.arrayLayers = MAX_SHADOW_CASTING_LIGHTS * 6;
        pointCfg.cubeCompatible = true;
        pointCfg.createSampler = (frame == 0);  // Only first frame needs sampler

        if (!::createDepthArrayResources(*initInfo_.raiiDevice, initInfo_.allocator, pointCfg, pointShadowResources[frame])) {
            return false;
        }

        // Create point shadow framebuffers (only first 6 layers for now)
        pointShadowFramebuffers_[frame].reserve(6);
        for (uint32_t i = 0; i < 6; i++) {
            auto fb = createShadowFramebuffer(*pointShadowResources[frame].layerViews[i], DYNAMIC_SHADOW_MAP_SIZE);
            if (!fb) return false;
            pointShadowFramebuffers_[frame].push_back(std::move(*fb));
        }

        // Spot lights: 2D array
        DepthArrayConfig spotCfg;
        spotCfg.extent = vk::Extent2D{DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE};
        spotCfg.format = vk::Format::eD32Sfloat;
        spotCfg.arrayLayers = MAX_SHADOW_CASTING_LIGHTS;
        spotCfg.createSampler = (frame == 0);

        if (!::createDepthArrayResources(*initInfo_.raiiDevice, initInfo_.allocator, spotCfg, spotShadowResources[frame])) {
            return false;
        }

        // Create spot shadow framebuffers
        spotShadowFramebuffers_[frame].reserve(MAX_SHADOW_CASTING_LIGHTS);
        for (uint32_t i = 0; i < MAX_SHADOW_CASTING_LIGHTS; i++) {
            auto fb = createShadowFramebuffer(*spotShadowResources[frame].layerViews[i], DYNAMIC_SHADOW_MAP_SIZE);
            if (!fb) return false;
            spotShadowFramebuffers_[frame].push_back(std::move(*fb));
        }
    }

    return true;
}

bool ShadowSystem::createInstancedShadowResources() {
    vk::Device vkDevice(initInfo_.device);

    // Create descriptor set layout for instanced shadow rendering
    instancedShadowDescriptorSetLayout_ = DescriptorSetLayoutBuilder()
        .addBinding(BindingBuilder::storageBuffer(Bindings::SHADOW_INSTANCES, vk::ShaderStageFlagBits::eVertex))
        .build(*initInfo_.raiiDevice);
    if (!instancedShadowDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow descriptor set layout");
        return false;
    }

    // Create per-frame instance buffers (persistently mapped for fast CPU writes)
    instanceBuffers_.resize(initInfo_.framesInFlight);
    instanceMappedPtrs.assign(initInfo_.framesInFlight, nullptr);

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(MAX_SHADOW_INSTANCES * sizeof(glm::mat4))
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (uint32_t i = 0; i < initInfo_.framesInFlight; i++) {
        if (!VmaBuffer::create(initInfo_.allocator, bufferInfo, allocInfo, instanceBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instance buffer %u", i);
            return false;
        }
        VmaAllocationInfo allocResult{};
        vmaGetAllocationInfo(initInfo_.allocator, instanceBuffers_[i].getAllocation(), &allocResult);
        instanceMappedPtrs[i] = allocResult.pMappedData;
    }

    // Allocate descriptor sets from a private pool owned by this system (sets die with the pool)
    auto poolSize = vk::DescriptorPoolSize{}
        .setType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(initInfo_.framesInFlight);

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setMaxSets(initInfo_.framesInFlight)
        .setPoolSizes(poolSize);

    try {
        instancedShadowPool_.emplace(*initInfo_.raiiDevice, poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow descriptor pool: %s", e.what());
        return false;
    }

    std::vector<vk::DescriptorSetLayout> layouts(initInfo_.framesInFlight, **instancedShadowDescriptorSetLayout_);
    auto allocInfoDS = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(**instancedShadowPool_)
        .setSetLayouts(layouts);

    try {
        instancedShadowDescriptorSets = vkDevice.allocateDescriptorSets(allocInfoDS);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate instanced shadow descriptor sets: %s", e.what());
        return false;
    }

    // Update descriptor sets with buffer bindings
    for (uint32_t i = 0; i < initInfo_.framesInFlight; i++) {
        DescriptorWriter()
            .add(WriteBuilder::storageBuffer(Bindings::SHADOW_INSTANCES,
                makeBufferInfo(instanceBuffers_[i].get())))
            .update(initInfo_.device, instancedShadowDescriptorSets[i]);
    }

    SDL_Log("Created instanced shadow resources: %u frames, %u max instances", initInfo_.framesInFlight, MAX_SHADOW_INSTANCES);
    return true;
}

bool ShadowSystem::createInstancedShadowPipeline() {
    // Create pipeline layout with both main descriptor set (for UBO) and instanced set (for SSBO)
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(InstancedShadowPushConstants));

    std::array<vk::DescriptorSetLayout, 2> setLayouts = {
        vk::DescriptorSetLayout(initInfo_.mainDescriptorSetLayout),  // Set 0: UBO with cascade matrices
        **instancedShadowDescriptorSetLayout_                        // Set 1: Instance SSBO
    };

    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayouts)
        .setPushConstantRanges(pushConstantRange);

    try {
        instancedShadowPipelineLayout_.emplace(*initInfo_.raiiDevice, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow pipeline layout: %s", e.what());
        return false;
    }

    // Create pipeline using the instanced shader
    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());

    GraphicsPipelineFactory factory(initInfo_.device);
    factory.applyPreset(GraphicsPipelineFactory::Preset::Shadow)
           .setShaders(initInfo_.shaderPath + "/shadow_instanced.vert.spv", initInfo_.shaderPath + "/shadow.frag.spv")
           .setRenderPass(**shadowRenderPass_)
           .setPipelineLayout(**instancedShadowPipelineLayout_)
           .setExtent({SHADOW_MAP_SIZE, SHADOW_MAP_SIZE})
           .setVertexInput({binding}, attrs)
           .setDepthBias(1.25f, 1.75f);

    vk::Pipeline rawPipeline;
    if (!factory.build(rawPipeline)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create instanced shadow pipeline");
        return false;
    }
    instancedShadowPipeline_.emplace(*initInfo_.raiiDevice, rawPipeline);

    SDL_Log("Created instanced shadow pipeline");
    return true;
}

void ShadowSystem::drawShadowSceneInstanced(
    vk::CommandBuffer cmd,
    uint32_t frameIndex,
    uint32_t cascadeIndex,
    const std::vector<ecs::RenderData>& sceneObjects)
{
    if (sceneObjects.empty() || !instancedShadowPipeline_) return;
    if (frameIndex >= instanceMappedPtrs.size()) return;

    vk::CommandBuffer vkCmd(cmd);

    // Group objects by mesh pointer (objects sharing the same mesh can be instanced)
    std::unordered_map<const Mesh*, std::vector<const ecs::RenderData*>> meshGroups;
    for (const auto& obj : sceneObjects) {
        if (!obj.castsShadow || !obj.mesh) continue;
        meshGroups[obj.mesh].push_back(&obj);
    }

    if (meshGroups.empty()) return;

    // Upload all instance transforms to the buffer
    auto* instanceData = static_cast<glm::mat4*>(instanceMappedPtrs[frameIndex]);
    uint32_t totalInstances = 0;

    // Build instance data and track offsets per mesh group
    struct MeshBatch {
        const Mesh* mesh;
        uint32_t instanceOffset;
        uint32_t instanceCount;
    };
    std::vector<MeshBatch> batches;
    batches.reserve(meshGroups.size());

    for (const auto& [mesh, objects] : meshGroups) {
        if (totalInstances + objects.size() > MAX_SHADOW_INSTANCES) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Shadow instance limit reached (%u), skipping remaining objects", MAX_SHADOW_INSTANCES);
            break;
        }

        MeshBatch batch;
        batch.mesh = mesh;
        batch.instanceOffset = totalInstances;
        batch.instanceCount = static_cast<uint32_t>(objects.size());

        for (const auto* obj : objects) {
            instanceData[totalInstances++] = obj->transform;
        }

        batches.push_back(batch);
    }

    if (batches.empty()) return;

    // Bind instanced shadow pipeline and descriptor sets
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **instancedShadowPipeline_);

    // Draw each mesh batch with instancing
    for (const auto& batch : batches) {
        vk::Buffer vb[] = {batch.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, 1, vb, offsets);
        vkCmd.bindIndexBuffer(batch.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        InstancedShadowPushConstants push{};
        push.cascadeIndex = cascadeIndex;
        push.instanceOffset = batch.instanceOffset;

        vkCmd.pushConstants<InstancedShadowPushConstants>(
            **instancedShadowPipelineLayout_,
            vk::ShaderStageFlagBits::eVertex,
            0, push);

        vkCmd.drawIndexed(batch.mesh->getIndexCount(), batch.instanceCount, 0, 0, 0);
        DIAG_RECORD_DRAW(); // One draw call, multiple instances
    }
}

bool ShadowSystem::initIndirectShadowPath(GPUSceneBuffer& sceneBuffer) {
    vk::Device vkDevice(initInfo_.device);

    // Deferred create: release anything from an earlier call so a repeat cannot leak.
    indirectShadowReady_ = false;
    indirectInstanceDescriptorSets.clear();
    indirectShadowPipeline_.reset();
    indirectShadowPipelineLayout_.reset();
    indirectShadowPool_.reset();

    // Pipeline layout: set 0 = main UBO (cascade matrices), set 1 = scene instance SSBO
    // (reuse the instanced shadow layout: one storage buffer at binding 0, vertex stage).
    auto pushRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(IndirectShadowPushConstants));

    std::array<vk::DescriptorSetLayout, 2> setLayouts = {
        vk::DescriptorSetLayout(initInfo_.mainDescriptorSetLayout),
        **instancedShadowDescriptorSetLayout_
    };

    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayouts)
        .setPushConstantRanges(pushRange);
    try {
        indirectShadowPipelineLayout_.emplace(*initInfo_.raiiDevice, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect shadow pipeline layout: %s", e.what());
        return false;
    }

    auto binding = Vertex::getBindingDescription();
    auto attrsArr = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrs(attrsArr.begin(), attrsArr.end());

    GraphicsPipelineFactory factory(initInfo_.device);
    factory.applyPreset(GraphicsPipelineFactory::Preset::Shadow)
           .setShaders(initInfo_.shaderPath + "/shadow_indirect.vert.spv", initInfo_.shaderPath + "/shadow.frag.spv")
           .setRenderPass(**shadowRenderPass_)
           .setPipelineLayout(**indirectShadowPipelineLayout_)
           .setExtent({SHADOW_MAP_SIZE, SHADOW_MAP_SIZE})
           .setVertexInput({binding}, attrs)
           .setDepthBias(1.25f, 1.75f);
    vk::Pipeline rawPipeline;
    if (!factory.build(rawPipeline)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect shadow pipeline");
        return false;
    }
    indirectShadowPipeline_.emplace(*initInfo_.raiiDevice, rawPipeline);

    // Per-frame instance descriptor sets bound to the GPUSceneBuffer instance buffers.
    auto poolSize = vk::DescriptorPoolSize{}
        .setType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(initInfo_.framesInFlight);
    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setMaxSets(initInfo_.framesInFlight)
        .setPoolSizes(poolSize);
    try {
        indirectShadowPool_.emplace(*initInfo_.raiiDevice, poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect shadow descriptor pool: %s", e.what());
        return false;
    }

    std::vector<vk::DescriptorSetLayout> layouts(initInfo_.framesInFlight,
        **instancedShadowDescriptorSetLayout_);
    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(**indirectShadowPool_)
        .setSetLayouts(layouts);
    try {
        indirectInstanceDescriptorSets = vkDevice.allocateDescriptorSets(allocInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate indirect shadow descriptor sets: %s", e.what());
        return false;
    }

    for (uint32_t i = 0; i < initInfo_.framesInFlight; ++i) {
        auto bufInfo = vk::DescriptorBufferInfo{}
            .setBuffer(sceneBuffer.getInstanceBuffer(i))
            .setOffset(0)
            .setRange(sceneBuffer.getInstanceBufferSize());
        auto write = vk::WriteDescriptorSet{}
            .setDstSet(indirectInstanceDescriptorSets[i])
            .setDstBinding(BINDING_SHADOW_INSTANCES)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setBufferInfo(bufInfo);
        vkDevice.updateDescriptorSets(write, {});
    }

    indirectShadowReady_ = true;
    SDL_Log("ShadowSystem: GPU-driven indirect shadow path initialized");
    return true;
}

void ShadowSystem::recordShadowSceneIndirect(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t cascade,
                                             vk::DescriptorSet uboDescriptorSet,
                                             GPUSceneBuffer& sceneBuffer,
                                             vk::Buffer indirectBuffer, bool canMultiDrawIndirect) {
    if (!indirectShadowReady_ || !indirectShadowPipeline_) return;
    if (frameIndex >= indirectInstanceDescriptorSets.size()) return;

    const auto& batches = sceneBuffer.getBatches();
    if (batches.empty()) return;

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **indirectShadowPipeline_);

    // set 0 = UBO (cascade matrices), set 1 = instance SSBO (bound once).
    std::array<vk::DescriptorSet, 2> sets = {
        vk::DescriptorSet(uboDescriptorSet),
        indirectInstanceDescriptorSets[frameIndex]
    };
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **indirectShadowPipelineLayout_, 0, sets, {});

    IndirectShadowPushConstants push{cascade};
    vkCmd.pushConstants<IndirectShadowPushConstants>(
        **indirectShadowPipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, push);

    // One draw per (mesh, material) batch. The per-cascade cull pass wrote one command per
    // object into a stable slot (command k -> instance k), toggling instanceCount (0/1) for
    // visibility; each batch owns the contiguous range [firstObject, firstObject+objectCount)
    // and firstInstance selects the instance via gl_InstanceIndex. Direct fallback draws the
    // whole batch where multiDrawIndirect is unavailable.
    constexpr uint32_t kCmdStride = sizeof(GPUDrawIndexedIndirectCommand);
    for (const auto& batch : batches) {
        if (!batch.mesh || batch.objectCount == 0) continue;

        vk::Buffer vb[] = {batch.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, 1, vb, offsets);
        vkCmd.bindIndexBuffer(batch.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        if (canMultiDrawIndirect && indirectBuffer) {
            vkCmd.drawIndexedIndirect(
                vk::Buffer(indirectBuffer),
                static_cast<vk::DeviceSize>(batch.firstObject) * kCmdStride,
                batch.objectCount,
                kCmdStride);
        } else {
            vkCmd.drawIndexed(batch.mesh->getIndexCount(), batch.objectCount, 0, 0, batch.firstObject);
        }
        DIAG_RECORD_DRAW();
    }
}

void ShadowSystem::calculateCascadeSplits(float nearClip, float farClip, float lambda, std::vector<float>& splits) {
    splits.resize(NUM_SHADOW_CASCADES + 1);
    splits[0] = nearClip;

    float clipRange = farClip - nearClip;
    float ratio = farClip / nearClip;

    for (uint32_t i = 1; i <= NUM_SHADOW_CASCADES; i++) {
        float p = static_cast<float>(i) / NUM_SHADOW_CASCADES;
        float logSplit = nearClip * std::pow(ratio, p);
        float uniformSplit = nearClip + clipRange * p;
        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}

glm::mat4 ShadowSystem::calculateCascadeMatrix(const glm::vec3& lightDir, const Camera& camera, const glm::mat4& invView, float nearSplit, float farSplit) {
    glm::vec3 lightDirNorm = glm::normalize(lightDir);
    if (glm::length(lightDirNorm) < std::numeric_limits<float>::epsilon()) {
        lightDirNorm = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    glm::mat4 cameraProj = camera.getProjectionMatrix();
    cameraProj[1][1] *= -1.0f;

    float tanHalfFov = 1.0f / cameraProj[1][1];
    float aspect = cameraProj[1][1] / cameraProj[0][0];

    float nearHeight = nearSplit * tanHalfFov;
    float nearWidth = nearHeight * aspect;
    float farHeight = farSplit * tanHalfFov;
    float farWidth = farHeight * aspect;

    glm::vec3 camPos = glm::vec3(invView[3]);
    glm::vec3 camForward = -glm::vec3(invView[2]);
    glm::vec3 camRight = glm::vec3(invView[0]);
    glm::vec3 camUp = glm::vec3(invView[1]);

    glm::vec3 nearCenter = camPos + camForward * nearSplit;
    glm::vec3 farCenter = camPos + camForward * farSplit;

    std::array<glm::vec3, 8> frustumCorners{
        nearCenter - camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth - camUp * nearHeight,
        nearCenter + camRight * nearWidth + camUp * nearHeight,
        nearCenter - camRight * nearWidth + camUp * nearHeight,
        farCenter - camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth - camUp * farHeight,
        farCenter + camRight * farWidth + camUp * farHeight,
        farCenter - camRight * farWidth + camUp * farHeight,
    };

    glm::vec3 center(0.0f);
    for (const auto& corner : frustumCorners) center += corner;
    center /= 8.0f;

    float radius = 0.0f;
    for (const auto& corner : frustumCorners) {
        radius = std::max(radius, glm::length(corner - center));
    }
    // Keep the ortho extent (and therefore the texel size) stable across frames:
    // the sphere radius is constant for a rigid frustum, but float noise in the
    // corner positions would otherwise wiggle it and break texel snapping below.
    radius = std::ceil(radius * 16.0f) / 16.0f;

    glm::vec3 up = (std::abs(lightDirNorm.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 lightPos = center + lightDirNorm * (radius + 50.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, center, up);

    float orthoSize = radius * 1.1f;
    float zRange = radius * 2.0f + 100.0f;

    glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, zRange);
    lightProjection[1][1] *= -1.0f;
    lightProjection[2][2] = lightProjection[2][2] * 0.5f;
    lightProjection[3][2] = lightProjection[3][2] * 0.5f + 0.5f;

    // Snap the light-space origin to whole shadow-map texels so sub-texel camera
    // movement doesn't shift the rasterization grid (shadow edge shimmer).
    glm::mat4 lightMatrix = lightProjection * lightView;
    glm::vec4 shadowOrigin = lightMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin *= static_cast<float>(SHADOW_MAP_SIZE) * 0.5f;
    glm::vec2 roundedOrigin = glm::round(glm::vec2(shadowOrigin));
    glm::vec2 roundOffset = (roundedOrigin - glm::vec2(shadowOrigin)) * (2.0f / static_cast<float>(SHADOW_MAP_SIZE));
    lightMatrix[3][0] += roundOffset.x;
    lightMatrix[3][1] += roundOffset.y;

    return lightMatrix;
}

void ShadowSystem::updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera) {
    const float shadowNear = 0.1f;
    const float shadowFar = 150.0f;
    const float lambda = 0.5f;

    // The cascade matrices are a pure function of the sun direction and the camera
    // view/projection. When none of those changed (static camera, paused time of day),
    // the previously computed matrices are still valid, so skip the work.
    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 proj = camera.getProjectionMatrix();
    constexpr float kEpsilon = 1e-6f;
    if (cascadesValid_ &&
        vec3NearlyEqual(lightDir, lastLightDir_, kEpsilon) &&
        matricesNearlyEqual(view, lastView_, kEpsilon) &&
        matricesNearlyEqual(proj, lastProj_, kEpsilon)) {
        return;
    }
    lastLightDir_ = lightDir;
    lastView_ = view;
    lastProj_ = proj;
    cascadesValid_ = true;

    calculateCascadeSplits(shadowNear, shadowFar, lambda, cascadeSplitDepths);

    // Inverse view is identical across all cascades; compute it once instead of per-cascade.
    const glm::mat4 invView = glm::inverse(view);

    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        cascadeMatrices[i] = calculateCascadeMatrix(lightDir, camera, invView, cascadeSplitDepths[i], cascadeSplitDepths[i + 1]);
    }
}

void ShadowSystem::drawShadowScene(
    vk::CommandBuffer cmd,
    vk::PipelineLayout layout,
    uint32_t cascadeOrFaceIndex,
    const glm::mat4& lightMatrix,
    const std::vector<ecs::RenderData>& sceneObjects,
    const DrawCallback& terrainCallback,
    const DrawCallback& grassCallback,
    const DrawCallback& treeCallback,
    const DrawCallback& skinnedCallback)
{
    vk::CommandBuffer vkCmd(cmd);

    for (const auto& obj : sceneObjects) {
        if (!obj.castsShadow) continue;

        ShadowPushConstants push{};
        push.model = obj.transform;
        push.cascadeIndex = static_cast<int>(cascadeOrFaceIndex);
        vkCmd.pushConstants<ShadowPushConstants>(
            layout, vk::ShaderStageFlagBits::eVertex, 0, push);

        vk::Buffer vb[] = {obj.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, 1, vb, offsets);
        vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
        vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);
        DIAG_RECORD_DRAW();
    }

    if (terrainCallback) terrainCallback(cmd, cascadeOrFaceIndex, lightMatrix);
    if (grassCallback) grassCallback(cmd, cascadeOrFaceIndex, lightMatrix);
    if (treeCallback) treeCallback(cmd, cascadeOrFaceIndex, lightMatrix);
    if (skinnedCallback) skinnedCallback(cmd, cascadeOrFaceIndex, lightMatrix);
}

void ShadowSystem::recordShadowPass(vk::CommandBuffer cmd, uint32_t frameIndex,
                                     vk::DescriptorSet descriptorSet,
                                     const std::vector<ecs::RenderData>& sceneObjects,
                                     const DrawCallback& terrainDrawCallback,
                                     const DrawCallback& grassDrawCallback,
                                     const DrawCallback& treeDrawCallback,
                                     const DrawCallback& skinnedDrawCallback,
                                     const ComputeCallback& preCascadeComputeCallback,
                                     const IndirectShadowParams& indirect) {
    vk::CommandBuffer vkCmd(cmd);

    // Each cascade render pass below clears and writes the shadow map
    csmInitialized_ = true;

    // GPU-driven indirect scene-object path (per-cascade frustum culling + indirect draw).
    // When active it replaces the instanced/per-object scene-object draw below.
    const bool useIndirect = indirect.enabled && indirectShadowReady_ &&
                             indirect.cullPass && indirect.sceneBuffer &&
                             indirect.sceneBuffer->getObjectCount() > 0;
    const uint32_t cullObjectCount = useIndirect ? indirect.sceneBuffer->getObjectCount() : 0;

    // Check if instanced rendering is available (only used when not on the indirect path)
    bool useInstanced = !useIndirect &&
                        instancedShadowPipeline_.has_value() &&
                        frameIndex < instancedShadowDescriptorSets.size() &&
                        !sceneObjects.empty();

    for (uint32_t cascade = 0; cascade < NUM_SHADOW_CASCADES; cascade++) {
        // Run pre-cascade compute pass (GPU culling) BEFORE the render pass
        if (preCascadeComputeCallback) {
            preCascadeComputeCallback(cmd, frameIndex, cascade, cascadeMatrices[cascade]);
        }

        // GPU-driven scene-object shadow culling for this cascade (also before the render pass).
        // Each cascade culls the shared cull-object buffer against its own light-space frustum
        // into its own indirect command buffer.
        if (useIndirect) {
            // Descriptors were written once at init (prepareDescriptors); only the per-frame
            // uniform contents change here. No descriptor updates during recording.
            indirect.cullPass->updateUniforms(frameIndex, cascade, cascadeMatrices[cascade], cullObjectCount);
            indirect.cullPass->recordCulling(cmd, frameIndex, cascade);
        }

        vk::ClearValue shadowClear;
        shadowClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        auto shadowPassInfo = vk::RenderPassBeginInfo{}
            .setRenderPass(**shadowRenderPass_)
            .setFramebuffer(*cascadeFramebuffers_[cascade])
            .setRenderArea({{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}})
            .setClearValues(shadowClear);

        vkCmd.beginRenderPass(shadowPassInfo, vk::SubpassContents::eInline);

        if (useIndirect) {
            // GPU-driven indirect draw of the GPUSceneBuffer objects (legacy + scatter) for
            // this cascade.
            recordShadowSceneIndirect(cmd, frameIndex, cascade, descriptorSet,
                                      *indirect.sceneBuffer,
                                      indirect.cullPass->getIndirectBuffer(frameIndex, cascade),
                                      indirect.canMultiDrawIndirect);

            // Supplementary shadow casters NOT mirrored into GPUSceneBuffer (e.g. ECS scene
            // entities) are drawn via the instanced path so they still cast shadows.
            if (!sceneObjects.empty() && instancedShadowPipeline_ &&
                frameIndex < instancedShadowDescriptorSets.size()) {
                std::array<vk::DescriptorSet, 2> descSets = {
                    vk::DescriptorSet(descriptorSet),
                    vk::DescriptorSet(instancedShadowDescriptorSets[frameIndex])
                };
                vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **instancedShadowPipeline_);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **instancedShadowPipelineLayout_,
                                         0, descSets, {});
                drawShadowSceneInstanced(cmd, frameIndex, cascade, sceneObjects);
            }

            // Leave the regular shadow pipeline bound for the terrain/grass/tree callbacks.
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **shadowPipeline_);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **shadowPipelineLayout_,
                                     0, vk::DescriptorSet(descriptorSet), {});
        } else if (useInstanced) {
            // Use instanced rendering for scene objects (rocks, detritus, etc.)
            // Bind descriptor sets for instanced pipeline
            std::array<vk::DescriptorSet, 2> descSets = {
                vk::DescriptorSet(descriptorSet),
                vk::DescriptorSet(instancedShadowDescriptorSets[frameIndex])
            };
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **instancedShadowPipeline_);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **instancedShadowPipelineLayout_,
                                     0, descSets, {});

            drawShadowSceneInstanced(cmd, frameIndex, cascade, sceneObjects);

            // Switch back to regular pipeline for callbacks (terrain, grass, trees)
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **shadowPipeline_);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **shadowPipelineLayout_,
                                     0, vk::DescriptorSet(descriptorSet), {});
        } else {
            // Fallback: per-object rendering
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **shadowPipeline_);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **shadowPipelineLayout_,
                                     0, vk::DescriptorSet(descriptorSet), {});

            // Draw scene objects individually
            for (const auto& obj : sceneObjects) {
                if (!obj.castsShadow) continue;

                ShadowPushConstants push{};
                push.model = obj.transform;
                push.cascadeIndex = static_cast<int>(cascade);
                vkCmd.pushConstants<ShadowPushConstants>(
                    **shadowPipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, push);

                vk::Buffer vb[] = {obj.mesh->getVertexBuffer()};
                vk::DeviceSize offsets[] = {0};
                vkCmd.bindVertexBuffers(0, 1, vb, offsets);
                vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
                vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);
                DIAG_RECORD_DRAW();
            }
        }

        // Call terrain/grass/tree/skinned callbacks
        if (terrainDrawCallback) terrainDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        if (grassDrawCallback) grassDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        if (treeDrawCallback) treeDrawCallback(cmd, cascade, cascadeMatrices[cascade]);
        if (skinnedDrawCallback) skinnedDrawCallback(cmd, cascade, cascadeMatrices[cascade]);

        vkCmd.endRenderPass();
    }
}

void ShadowSystem::bindSkinnedShadowPipeline(vk::CommandBuffer cmd, vk::DescriptorSet descriptorSet,
                                              uint32_t boneMatrixOffset) {
    if (!skinnedShadowPipeline_) return;
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **skinnedShadowPipeline_);
    // Pass dynamic offset for the bone matrices dynamic UBO (binding 12)
    // This allows selecting different character's bone matrices for each draw
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **skinnedShadowPipelineLayout_,
                             0, vk::DescriptorSet(descriptorSet), boneMatrixOffset);
}

void ShadowSystem::recordSkinnedMeshShadow(vk::CommandBuffer cmd, uint32_t cascade,
                                            const glm::mat4& modelMatrix,
                                            const SkinnedMesh& mesh) {
    if (!skinnedShadowPipelineLayout_) return;

    vk::CommandBuffer vkCmd(cmd);

    ShadowPushConstants shadowPush{};
    shadowPush.model = modelMatrix;
    shadowPush.cascadeIndex = static_cast<int>(cascade);
    vkCmd.pushConstants<ShadowPushConstants>(
        **skinnedShadowPipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, shadowPush);

    vk::Buffer vertexBuffers[] = {mesh.getVertexBuffer()};
    vk::DeviceSize offsets[] = {0};
    vkCmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    vkCmd.bindIndexBuffer(mesh.getIndexBuffer(), 0, vk::IndexType::eUint32);
    vkCmd.drawIndexed(mesh.getIndexCount(), 1, 0, 0, 0);
    DIAG_RECORD_DRAW();
}

void ShadowSystem::renderDynamicShadows(vk::CommandBuffer cmd, uint32_t frameIndex,
                                        vk::DescriptorSet descriptorSet,
                                        const std::vector<ecs::RenderData>& sceneObjects,
                                        const DrawCallback& terrainDrawCallback,
                                        const DrawCallback& grassDrawCallback,
                                        const DrawCallback& skinnedDrawCallback,
                                        const std::vector<Light>& visibleLights) {
    if (!dynamicShadowPipeline_) return;

    vk::CommandBuffer vkCmd(cmd);

    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE))
        .setHeight(static_cast<float>(DYNAMIC_SHADOW_MAP_SIZE))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE});

    uint32_t lightCount = static_cast<uint32_t>(std::min<size_t>(visibleLights.size(), MAX_SHADOW_CASTING_LIGHTS));

    for (uint32_t lightIndex = 0; lightIndex < lightCount; lightIndex++) {
        const Light& light = visibleLights[lightIndex];
        if (!light.castsShadows) continue;

        if (light.type == LightType::Point) {
            if (frameIndex >= pointShadowFramebuffers_.size()) continue;
            for (uint32_t face = 0; face < pointShadowFramebuffers_[frameIndex].size(); face++) {
                vk::ClearValue clear;
                clear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

                auto passInfo = vk::RenderPassBeginInfo{}
                    .setRenderPass(**shadowRenderPass_)
                    .setFramebuffer(*pointShadowFramebuffers_[frameIndex][face])
                    .setRenderArea({{0, 0}, {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE}})
                    .setClearValues(clear);

                vkCmd.beginRenderPass(passInfo, vk::SubpassContents::eInline);
                vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **dynamicShadowPipeline_);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **dynamicShadowPipelineLayout_,
                                         0, vk::DescriptorSet(descriptorSet), {});
                vkCmd.setViewport(0, viewport);
                vkCmd.setScissor(0, scissor);

                drawShadowScene(cmd, **dynamicShadowPipelineLayout_, face, glm::mat4(1.0f),
                                sceneObjects, terrainDrawCallback, grassDrawCallback, nullptr, skinnedDrawCallback);

                vkCmd.endRenderPass();
            }
        } else {
            if (frameIndex >= spotShadowFramebuffers_.size() || lightIndex >= spotShadowFramebuffers_[frameIndex].size()) continue;

            vk::ClearValue clear;
            clear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

            auto passInfo = vk::RenderPassBeginInfo{}
                .setRenderPass(**shadowRenderPass_)
                .setFramebuffer(*spotShadowFramebuffers_[frameIndex][lightIndex])
                .setRenderArea({{0, 0}, {DYNAMIC_SHADOW_MAP_SIZE, DYNAMIC_SHADOW_MAP_SIZE}})
                .setClearValues(clear);

            vkCmd.beginRenderPass(passInfo, vk::SubpassContents::eInline);
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **dynamicShadowPipeline_);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **dynamicShadowPipelineLayout_,
                                     0, vk::DescriptorSet(descriptorSet), {});
            vkCmd.setViewport(0, viewport);
            vkCmd.setScissor(0, scissor);

            drawShadowScene(cmd, **dynamicShadowPipelineLayout_, lightIndex, glm::mat4(1.0f),
                            sceneObjects, terrainDrawCallback, grassDrawCallback, nullptr, skinnedDrawCallback);

            vkCmd.endRenderPass();
        }
    }
}
