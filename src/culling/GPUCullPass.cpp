#include "GPUCullPass.h"
#include "core/GPUSceneBuffer.h"
#include "core/vulkan/VmaBufferFactory.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/InitInfoBuilder.h"
#include "shaders/bindings.h"
#include <SDL3/SDL_log.h>
#include <cstring>

std::unique_ptr<GPUCullPass> GPUCullPass::create(const InitInfo& info) {
    auto pass = std::make_unique<GPUCullPass>(ConstructToken{});
    if (!pass->initInternal(info)) {
        return nullptr;
    }
    return pass;
}

std::unique_ptr<GPUCullPass> GPUCullPass::create(const InitContext& ctx) {
    InitInfo info = InitInfoBuilder::fromContext<InitInfo>(ctx);
    return create(info);
}

GPUCullPass::~GPUCullPass() {
    cleanup();
}

bool GPUCullPass::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass requires raiiDevice");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass: Failed to create pipeline");
        return false;
    }

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass: Failed to create buffers");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("GPUCullPass: Initialized with %u frames", framesInFlight_);
    return true;
}

void GPUCullPass::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    destroyDescriptorSets();
    destroyBuffers();
    destroyPipeline();

    device_ = VK_NULL_HANDLE;
}

bool GPUCullPass::createPipeline() {
    // Descriptor set layout for culling:
    // Binding 0: Uniforms (UBO)
    // Binding 1: Object data (SSBO, read-only)
    // Binding 2: Indirect draw buffer (SSBO, write)
    // Binding 3: Draw count buffer (SSBO, atomic)
    // Binding 4: Hi-Z pyramid (optional sampler)
    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device_)
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 0: Uniforms
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 1: Object data
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 2: Indirect draw buffer
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 3: Draw count buffer
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 4: Hi-Z pyramid
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass: Failed to create descriptor set layout");
        return false;
    }
    descSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Pipeline layout (no push constants)
    if (!PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**descSetLayout_)
            .buildInto(pipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass: Failed to create pipeline layout");
        return false;
    }

    // Compute pipeline
    return ComputePipelineBuilder(*raiiDevice_)
        .setShader(shaderPath_ + "/scene_cull.comp.spv")
        .setPipelineLayout(**pipelineLayout_)
        .buildInto(pipeline_);
}

bool GPUCullPass::createBuffers() {
    // Create per-frame uniform buffers
    bool success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(sizeof(GPUCullUniforms))
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(uniformBuffers_);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass: Failed to create uniform buffers");
        return false;
    }

    return true;
}

bool GPUCullPass::createDescriptorSets() {
    // Allocate descriptor sets (one per frame)
    descSets_ = descriptorPool_->allocate(**descSetLayout_, framesInFlight_);
    if (descSets_.size() != framesInFlight_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUCullPass: Failed to allocate descriptor sets");
        return false;
    }

    // Initial update with uniform buffers only
    // Scene buffers will be bound later via bindSceneBuffer()
    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        DescriptorManager::SetWriter(device_, descSets_[i])
            .writeBuffer(BINDING_SCENE_CULL_UNIFORMS, uniformBuffers_.buffers[i], 0, sizeof(GPUCullUniforms))
            .update();
    }

    return true;
}

void GPUCullPass::destroyPipeline() {
    pipeline_.reset();
    pipelineLayout_.reset();
    descSetLayout_.reset();
}

void GPUCullPass::destroyBuffers() {
    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);
}

void GPUCullPass::destroyDescriptorSets() {
    descSets_.clear();
}

void GPUCullPass::updateUniforms(uint32_t frameIndex,
                                  const glm::mat4& view,
                                  const glm::mat4& proj,
                                  const glm::vec3& cameraPos,
                                  uint32_t objectCount) {
    GPUCullUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.screenParams = glm::vec4(1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f);  // Default, update with actual screen size
    uniforms.objectCount = objectCount;
    uniforms.enableHiZ = hiZEnabled_ ? 1 : 0;
    uniforms.maxDrawCommands = MAX_OBJECTS;
    uniforms.padding = 0;

    // Extract frustum planes from view-projection matrix
    extractFrustumPlanes(uniforms.viewProjMatrix, uniforms.frustumPlanes);

    // Copy to GPU
    memcpy(uniformBuffers_.mappedPointers[frameIndex], &uniforms, sizeof(GPUCullUniforms));
}

void GPUCullPass::bindSceneBuffer(GPUSceneBuffer* sceneBuffer, uint32_t frameIndex) {
    if (!sceneBuffer) return;

    currentSceneBuffer_ = sceneBuffer;

    // Update descriptor set with scene buffer bindings
    auto writer = DescriptorManager::SetWriter(device_, descSets_[frameIndex])
        .writeBuffer(BINDING_SCENE_CULL_UNIFORMS, uniformBuffers_.buffers[frameIndex], 0, sizeof(GPUCullUniforms))
        .writeBuffer(BINDING_SCENE_CULL_OBJECTS, sceneBuffer->getCullObjectBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(BINDING_SCENE_CULL_INDIRECT, sceneBuffer->getIndirectBuffer(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(BINDING_SCENE_CULL_COUNT, sceneBuffer->getDrawCountBuffer(frameIndex), 0, sizeof(uint32_t), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    // Add Hi-Z pyramid if available
    if (hiZPyramidView_ != VK_NULL_HANDLE && hiZSampler_ != VK_NULL_HANDLE) {
        writer.writeImage(BINDING_SCENE_CULL_HIZ, hiZPyramidView_, hiZSampler_);
    }

    writer.update();
}

void GPUCullPass::recordCulling(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!currentSceneBuffer_ || currentSceneBuffer_->getObjectCount() == 0) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Reset draw count to zero
    currentSceneBuffer_->resetDrawCount(vkCmd);

    // Barrier after reset
    BarrierHelpers::fillBufferToCompute(vkCmd);

    // Bind pipeline and descriptor set
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             **pipelineLayout_, 0, vk::DescriptorSet(descSets_[frameIndex]), {});

    // Dispatch
    uint32_t objectCount = currentSceneBuffer_->getObjectCount();
    uint32_t groupCount = (objectCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmd.dispatch(groupCount, 1, 1);

    // Barrier: compute -> indirect draw
    BarrierHelpers::computeToIndirectDraw(vkCmd);
}

VkBuffer GPUCullPass::getUniformBuffer(uint32_t frameIndex) const {
    return uniformBuffers_.buffers[frameIndex];
}

GPUCullPass::CullingStats GPUCullPass::getStats(uint32_t frameIndex) const {
    CullingStats stats{};
    if (currentSceneBuffer_) {
        stats.totalObjects = currentSceneBuffer_->getObjectCount();
        stats.visibleObjects = currentSceneBuffer_->getVisibleCount(frameIndex);
    }
    return stats;
}

void GPUCullPass::setHiZPyramid(VkImageView pyramidView, VkSampler sampler) {
    hiZPyramidView_ = pyramidView;
    hiZSampler_ = sampler;
}

void GPUCullPass::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // Left plane
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );

    // Right plane
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );

    // Bottom plane
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );

    // Top plane
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );

    // Near plane
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );

    // Far plane
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0001f) {
            planes[i] /= len;
        }
    }
}
