#include "ShadowCullPass.h"
#include "core/GPUSceneBuffer.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "CullCommon.h"  // extractFrustumPlanes (src/vegetation)
#include "shaders/bindings.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <array>

std::unique_ptr<ShadowCullPass> ShadowCullPass::create(const InitInfo& info) {
    auto pass = std::make_unique<ShadowCullPass>(ConstructToken{});
    if (!pass->initInternal(info)) {
        return nullptr;
    }
    return pass;
}

ShadowCullPass::~ShadowCullPass() {
    cleanup();
}

bool ShadowCullPass::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    cascadeCount_ = info.cascadeCount;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowCullPass requires raiiDevice");
        return false;
    }
    if (cascadeCount_ == 0 || framesInFlight_ == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowCullPass: invalid cascade/frame count");
        return false;
    }

    if (!createPipeline())       { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowCullPass: pipeline failed"); return false; }
    if (!createBuffers())        { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowCullPass: buffers failed"); return false; }
    if (!createDescriptorSets()) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowCullPass: descriptor sets failed"); return false; }

    SDL_Log("ShadowCullPass: Initialized with %u cascades x %u frames", cascadeCount_, framesInFlight_);
    return true;
}

void ShadowCullPass::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    for (auto& set : uniformBuffers_) BufferUtils::destroyBuffers(allocator_, set);
    for (auto& set : indirectBuffers_) BufferUtils::destroyBuffers(allocator_, set);
    for (auto& set : countBuffers_) BufferUtils::destroyBuffers(allocator_, set);
    uniformBuffers_.clear();
    indirectBuffers_.clear();
    countBuffers_.clear();
    descSets_.clear();
    pipeline_.reset();
    pipelineLayout_.reset();
    descSetLayout_.reset();
    device_ = VK_NULL_HANDLE;
}

bool ShadowCullPass::createPipeline() {
    // Same descriptor layout as the color cull pass (scene_cull.comp is shared).
    vk::DescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device_)
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 0: Uniforms
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 1: Object data
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 2: Indirect draw buffer
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 3: Draw count buffer
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 4: Hi-Z (placeholder)
        .build();

    if (rawLayout == VK_NULL_HANDLE) return false;
    descSetLayout_.emplace(*raiiDevice_, rawLayout);

    if (!PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**descSetLayout_)
            .buildInto(pipelineLayout_)) {
        return false;
    }

    return ComputePipelineBuilder(*raiiDevice_)
        .setShader(shaderPath_ + "/scene_cull.comp.spv")
        .setPipelineLayout(**pipelineLayout_)
        .buildInto(pipeline_);
}

bool ShadowCullPass::createBuffers() {
    uniformBuffers_.resize(cascadeCount_);
    indirectBuffers_.resize(cascadeCount_);
    countBuffers_.resize(cascadeCount_);

    const vk::DeviceSize indirectSize = sizeof(GPUDrawIndexedIndirectCommand) * MAX_OBJECTS;

    for (uint32_t c = 0; c < cascadeCount_; ++c) {
        if (!BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(framesInFlight_)
                .setSize(sizeof(GPUCullUniforms))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
                .build(uniformBuffers_[c])) {
            return false;
        }

        if (!BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(framesInFlight_)
                .setSize(indirectSize)
                .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                .setAllocationFlags(0)  // GPU-only
                .build(indirectBuffers_[c])) {
            return false;
        }

        if (!BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(framesInFlight_)
                .setSize(sizeof(uint32_t))
                .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
                .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                    VMA_ALLOCATION_CREATE_MAPPED_BIT)
                .build(countBuffers_[c])) {
            return false;
        }
    }
    return true;
}

bool ShadowCullPass::createDescriptorSets() {
    descSets_ = descriptorPool_->allocate(**descSetLayout_, cascadeCount_ * framesInFlight_);
    if (descSets_.size() != cascadeCount_ * framesInFlight_) {
        return false;
    }
    // Bind uniform buffers now; scene/output buffers bound per-frame via bindSceneBuffer().
    for (uint32_t c = 0; c < cascadeCount_; ++c) {
        for (uint32_t f = 0; f < framesInFlight_; ++f) {
            DescriptorManager::SetWriter(device_, descSets_[flatIndex(c, f)])
                .writeBuffer(BINDING_SCENE_CULL_UNIFORMS, uniformBuffers_[c].buffers[f], 0, sizeof(GPUCullUniforms))
                .update();
        }
    }
    return true;
}

void ShadowCullPass::updateUniforms(uint32_t frameIndex, uint32_t cascade,
                                    const glm::mat4& lightViewProj, uint32_t objectCount) {
    if (cascade >= cascadeCount_ || frameIndex >= framesInFlight_) return;

    GPUCullUniforms uniforms{};
    uniforms.viewMatrix = glm::mat4(1.0f);
    uniforms.projMatrix = glm::mat4(1.0f);
    uniforms.viewProjMatrix = lightViewProj;
    uniforms.cameraPosition = glm::vec4(0.0f);
    uniforms.screenParams = glm::vec4(0.0f);
    uniforms.objectCount = objectCount;
    uniforms.enableHiZ = 0;  // no occlusion culling for shadows
    uniforms.maxDrawCommands = MAX_OBJECTS;
    uniforms.cullMode = 1;   // shadow pass: also reject non-casters

    extractFrustumPlanes(lightViewProj, uniforms.frustumPlanes);

    // CSM caster extension: a shadow caster between the light and the visible cascade slice
    // (or beyond its far extent along the light's depth axis) must still be drawn into the
    // shadow map even though it lies outside the tight, view-frustum-fitted ortho box on that
    // axis. Neutralize the light-space NEAR (4) and FAR (5) planes so the cull bounds objects
    // by the four side planes (L/R/T/B) only. A plane of (0,0,0,large) makes dot(n,c)+w = large,
    // always >= -radius for any sphere and >= 0 for any AABB, so it never culls on depth.
    // The caster-flag test (cullMode==1) is untouched, so non-casters are still rejected.
    constexpr float kNeverCull = 1.0e9f;
    uniforms.frustumPlanes[4] = glm::vec4(0.0f, 0.0f, 0.0f, kNeverCull);  // near
    uniforms.frustumPlanes[5] = glm::vec4(0.0f, 0.0f, 0.0f, kNeverCull);  // far

    lastObjectCount_ = objectCount;

    void* dst = uniformBuffers_[cascade].mappedPointers[frameIndex];
    memcpy(dst, &uniforms, sizeof(GPUCullUniforms));
    vmaFlushAllocation(allocator_, uniformBuffers_[cascade].allocations[frameIndex], 0, sizeof(GPUCullUniforms));
}

bool ShadowCullPass::prepareDescriptors(GPUSceneBuffer* sceneBuffer) {
    if (!sceneBuffer) return false;
    currentSceneBuffer_ = sceneBuffer;

    vk::Buffer cullObjBuffer = sceneBuffer->getCullObjectBuffer();
    if (!cullObjBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowCullPass::prepareDescriptors: null cull-object buffer");
        return false;
    }
    vk::ImageView imageView(placeholderImageView_);
    vk::Sampler sampler(placeholderSampler_);
    if (!imageView || !sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowCullPass::prepareDescriptors: placeholder image not set");
        return false;
    }

    // All buffers are stable for their lifetime, so write every (cascade, frame) set once here.
    for (uint32_t c = 0; c < cascadeCount_; ++c) {
        for (uint32_t f = 0; f < framesInFlight_; ++f) {
            std::array<vk::DescriptorBufferInfo, 4> bufferInfos = {{
                {uniformBuffers_[c].buffers[f], 0, sizeof(GPUCullUniforms)},
                {cullObjBuffer, 0, VK_WHOLE_SIZE},
                {indirectBuffers_[c].buffers[f], 0, VK_WHOLE_SIZE},
                {countBuffers_[c].buffers[f], 0, sizeof(uint32_t)}
            }};
            vk::DescriptorImageInfo imageInfo{sampler, imageView, vk::ImageLayout::eShaderReadOnlyOptimal};

            vk::DescriptorSet set = descSets_[flatIndex(c, f)];
            std::array<vk::WriteDescriptorSet, 5> writes = {{
                vk::WriteDescriptorSet{}.setDstSet(set).setDstBinding(BINDING_SCENE_CULL_UNIFORMS)
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(1).setPBufferInfo(&bufferInfos[0]),
                vk::WriteDescriptorSet{}.setDstSet(set).setDstBinding(BINDING_SCENE_CULL_OBJECTS)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1).setPBufferInfo(&bufferInfos[1]),
                vk::WriteDescriptorSet{}.setDstSet(set).setDstBinding(BINDING_SCENE_CULL_INDIRECT)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1).setPBufferInfo(&bufferInfos[2]),
                vk::WriteDescriptorSet{}.setDstSet(set).setDstBinding(BINDING_SCENE_CULL_COUNT)
                    .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1).setPBufferInfo(&bufferInfos[3]),
                vk::WriteDescriptorSet{}.setDstSet(set).setDstBinding(BINDING_SCENE_CULL_HIZ)
                    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(1).setPImageInfo(&imageInfo)
            }};
            vk::Device(device_).updateDescriptorSets(writes, {});
        }
    }
    return true;
}

void ShadowCullPass::recordCulling(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t cascade) {
    if (!currentSceneBuffer_ || cascade >= cascadeCount_ || frameIndex >= framesInFlight_) return;
    if (lastObjectCount_ == 0) return;

    vk::CommandBuffer vkCmd(cmd);

    // Reset this cascade's draw count, then barrier before the compute reads/writes.
    vkCmd.fillBuffer(countBuffers_[cascade].buffers[frameIndex], 0, sizeof(uint32_t), 0);
    BarrierHelpers::fillBufferToCompute(vkCmd);

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **pipelineLayout_, 0,
                             vk::DescriptorSet(descSets_[flatIndex(cascade, frameIndex)]), {});

    uint32_t groupCount = (lastObjectCount_ + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmd.dispatch(groupCount, 1, 1);

    BarrierHelpers::computeToIndirectDraw(vkCmd);
}

vk::Buffer ShadowCullPass::getIndirectBuffer(uint32_t frameIndex, uint32_t cascade) const {
    if (cascade >= cascadeCount_ || frameIndex >= framesInFlight_) return VK_NULL_HANDLE;
    return indirectBuffers_[cascade].buffers[frameIndex];
}

uint32_t ShadowCullPass::getVisibleCount(uint32_t frameIndex, uint32_t cascade) const {
    if (cascade >= cascadeCount_ || frameIndex >= framesInFlight_) return 0;
    const auto& mapped = countBuffers_[cascade].mappedPointers;
    if (frameIndex >= mapped.size() || !mapped[frameIndex]) return 0;
    return *static_cast<uint32_t*>(mapped[frameIndex]);
}

void ShadowCullPass::setPlaceholderImage(vk::ImageView view, vk::Sampler sampler) {
    placeholderImageView_ = view;
    placeholderSampler_ = sampler;
}
