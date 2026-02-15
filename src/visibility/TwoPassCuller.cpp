#include "TwoPassCuller.h"
#include "ShaderLoader.h"
#include "shaders/bindings.h"

#include <SDL3/SDL_log.h>
#include <cstring>

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<TwoPassCuller> TwoPassCuller::create(const InitInfo& info) {
    auto culler = std::make_unique<TwoPassCuller>(ConstructToken{});
    if (!culler->initInternal(info)) {
        return nullptr;
    }
    return culler;
}

std::unique_ptr<TwoPassCuller> TwoPassCuller::create(const InitContext& ctx,
                                                       uint32_t maxClusters,
                                                       uint32_t maxDrawCommands) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.maxClusters = maxClusters;
    info.maxDrawCommands = maxDrawCommands;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

TwoPassCuller::~TwoPassCuller() {
    cleanup();
}

// ============================================================================
// Initialization
// ============================================================================

bool TwoPassCuller::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    maxClusters_ = info.maxClusters;
    maxDrawCommands_ = info.maxDrawCommands;
    raiiDevice_ = info.raiiDevice;

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create buffers");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create pipeline");
        return false;
    }

    SDL_Log("TwoPassCuller: Initialized (maxClusters=%u, maxDrawCommands=%u)",
            maxClusters_, maxDrawCommands_);
    return true;
}

void TwoPassCuller::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    destroyDescriptorSets();
    destroyPipeline();
    destroyBuffers();
}

// ============================================================================
// Buffers
// ============================================================================

bool TwoPassCuller::createBuffers() {
    VkDeviceSize indirectSize = maxDrawCommands_ * sizeof(VkDrawIndexedIndirectCommand);
    VkDeviceSize countSize = sizeof(uint32_t);
    VkDeviceSize visibleSize = maxClusters_ * sizeof(uint32_t);
    VkDeviceSize uniformSize = sizeof(ClusterCullUniforms);

    auto builder = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_);

    // Indirect command buffers (GPU-written, GPU-read for vkCmdDrawIndexedIndirectCount)
    bool ok = builder
        .setSize(indirectSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        .setAllocationFlags(0)
        .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .build(pass1IndirectBuffers_);
    if (!ok) return false;

    ok = builder.build(pass2IndirectBuffers_);
    if (!ok) return false;

    // Draw count buffers (atomic counter, GPU-written)
    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(pass1DrawCountBuffers_);
    if (!ok) return false;

    ok = builder.build(pass2DrawCountBuffers_);
    if (!ok) return false;

    // Visible cluster ID buffers
    ok = builder
        .setSize(visibleSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        .build(visibleClusterBuffers_);
    if (!ok) return false;

    ok = builder.build(prevVisibleClusterBuffers_);
    if (!ok) return false;

    // Visible count buffers
    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(visibleCountBuffers_);
    if (!ok) return false;

    ok = builder.build(prevVisibleCountBuffers_);
    if (!ok) return false;

    // Uniform buffers (CPU-written each frame)
    ok = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(uniformSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(uniformBuffers_);
    if (!ok) return false;

    return true;
}

void TwoPassCuller::destroyBuffers() {
    BufferUtils::destroyBuffers(allocator_, pass1IndirectBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass1DrawCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass2IndirectBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass2DrawCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, visibleClusterBuffers_);
    BufferUtils::destroyBuffers(allocator_, visibleCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, prevVisibleClusterBuffers_);
    BufferUtils::destroyBuffers(allocator_, prevVisibleCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);
}

// ============================================================================
// Pipeline
// ============================================================================

bool TwoPassCuller::createPipeline() {
    if (!raiiDevice_) return false;

    // Descriptor set layout matching cluster_cull.comp bindings
    std::array<vk::DescriptorSetLayoutBinding, 10> bindings;
    bindings[0] = vk::DescriptorSetLayoutBinding{}.setBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // clusters
    bindings[1] = vk::DescriptorSetLayoutBinding{}.setBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // instances
    bindings[2] = vk::DescriptorSetLayoutBinding{}.setBinding(2)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // indirect commands
    bindings[3] = vk::DescriptorSetLayoutBinding{}.setBinding(3)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // draw count
    bindings[4] = vk::DescriptorSetLayoutBinding{}.setBinding(4)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // visible clusters
    bindings[5] = vk::DescriptorSetLayoutBinding{}.setBinding(5)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // visible count
    bindings[6] = vk::DescriptorSetLayoutBinding{}.setBinding(6)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // cull uniforms
    bindings[7] = vk::DescriptorSetLayoutBinding{}.setBinding(7)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // Hi-Z pyramid
    bindings[8] = vk::DescriptorSetLayoutBinding{}.setBinding(8)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // prev visible clusters
    bindings[9] = vk::DescriptorSetLayoutBinding{}.setBinding(9)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // prev visible count

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);

    try {
        descSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create desc set layout: %s", e.what());
        return false;
    }

    vk::DescriptorSetLayout vkDescLayout(**descSetLayout_);

    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}.setSetLayouts(vkDescLayout);

    vk::Device vkDevice(device_);
    pipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load compute shader
    auto compModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/cluster_cull.comp.spv");
    if (!compModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to load cluster_cull.comp shader");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*compModule)
        .setPName("main");

    auto computeInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(pipelineLayout_);

    auto result = vkDevice.createComputePipeline(nullptr, computeInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create compute pipeline");
        vkDevice.destroyShaderModule(*compModule);
        return false;
    }
    pipeline_ = static_cast<VkPipeline>(result.value);

    vkDevice.destroyShaderModule(*compModule);

    SDL_Log("TwoPassCuller: Compute pipeline created");
    return true;
}

void TwoPassCuller::destroyPipeline() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    descSetLayout_.reset();
}

bool TwoPassCuller::createDescriptorSets() {
    // Descriptor sets will be allocated and updated per-frame when needed
    return true;
}

void TwoPassCuller::destroyDescriptorSets() {
    pass1DescSets_.clear();
    pass2DescSets_.clear();
}

// ============================================================================
// Per-frame operations
// ============================================================================

void TwoPassCuller::updateUniforms(uint32_t frameIndex,
                                     const glm::mat4& view, const glm::mat4& proj,
                                     const glm::vec3& cameraPos,
                                     const glm::vec4 frustumPlanes[6],
                                     uint32_t clusterCount, uint32_t instanceCount,
                                     float nearPlane, float farPlane, uint32_t hiZMipLevels) {
    ClusterCullUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    for (int i = 0; i < 6; ++i) {
        uniforms.frustumPlanes[i] = frustumPlanes[i];
    }
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.screenParams = glm::vec4(0.0f);  // Set by caller based on render target size
    uniforms.depthParams = glm::vec4(nearPlane, farPlane, static_cast<float>(hiZMipLevels), 0.0f);
    uniforms.clusterCount = clusterCount;
    uniforms.instanceCount = instanceCount;
    uniforms.enableHiZ = 0;  // Pass 1 doesn't use Hi-Z
    uniforms.maxDrawCommands = maxDrawCommands_;

    void* mapped = uniformBuffers_.mappedPointers[frameIndex];
    if (mapped) {
        memcpy(mapped, &uniforms, sizeof(uniforms));
        vmaFlushAllocation(allocator_, uniformBuffers_.allocations[frameIndex],
                           0, sizeof(uniforms));
    }
}

void TwoPassCuller::recordPass1(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Clear draw count to 0
    vkCmdFillBuffer(cmd, pass1DrawCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);
    vkCmdFillBuffer(cmd, visibleCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);

    // Barrier: transfer -> compute
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    // Descriptor sets would be bound here (pass 1 set with passIndex=0)
    // For now, the dispatch count is based on previous frame's visible count
    uint32_t workGroupSize = 64;
    uint32_t dispatchCount = (maxClusters_ + workGroupSize - 1) / workGroupSize;
    vkCmdDispatch(cmd, dispatchCount, 1, 1);

    // Barrier: compute write -> indirect read
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void TwoPassCuller::recordPass2(VkCommandBuffer cmd, uint32_t frameIndex, VkImageView hiZView) {
    (void)hiZView;  // Will be used when descriptor sets are fully wired

    // Clear pass 2 draw count
    vkCmdFillBuffer(cmd, pass2DrawCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    // Descriptor sets for pass 2 (with Hi-Z and passIndex=1)
    uint32_t workGroupSize = 64;
    uint32_t dispatchCount = (maxClusters_ + workGroupSize - 1) / workGroupSize;
    vkCmdDispatch(cmd, dispatchCount, 1, 1);

    // Barrier: compute -> indirect draw
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void TwoPassCuller::swapBuffers() {
    currentBufferIndex_ = 1 - currentBufferIndex_;
}

VkBuffer TwoPassCuller::getPass1IndirectBuffer(uint32_t frameIndex) const {
    return pass1IndirectBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass1DrawCountBuffer(uint32_t frameIndex) const {
    return pass1DrawCountBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass2IndirectBuffer(uint32_t frameIndex) const {
    return pass2IndirectBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass2DrawCountBuffer(uint32_t frameIndex) const {
    return pass2DrawCountBuffers_.buffers[frameIndex];
}
