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
    maxDAGLevels_ = info.maxDAGLevels;
    raiiDevice_ = info.raiiDevice;

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create buffers");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create pipeline");
        return false;
    }

    if (!createLODSelectPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create LOD selection pipeline");
        return false;
    }

    SDL_Log("TwoPassCuller: Initialized (maxClusters=%u, maxDrawCommands=%u, maxDAGLevels=%u)",
            maxClusters_, maxDrawCommands_, maxDAGLevels_);
    return true;
}

void TwoPassCuller::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    destroyDescriptorSets();
    destroyLODSelectPipeline();
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

    // LOD selection buffers
    ok = builder
        .setSize(visibleSize)  // same capacity as visible cluster buffers
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        .setAllocationFlags(0)
        .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .build(selectedClusterBuffers_);
    if (!ok) return false;

    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(selectedCountBuffers_);
    if (!ok) return false;

    VkDeviceSize lodSelectUniformSize = sizeof(ClusterSelectUniforms);
    ok = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(lodSelectUniformSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(lodSelectUniformBuffers_);
    if (!ok) return false;

    // Top-down DAG traversal: ping-pong node buffers
    // Each buffer holds cluster indices for one level of the DAG
    ok = builder
        .setSize(visibleSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(0)
        .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .build(nodeBufferA_);
    if (!ok) return false;

    ok = builder.build(nodeBufferB_);
    if (!ok) return false;

    // Node count buffers (atomic counters for each ping-pong side)
    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(nodeCountA_);
    if (!ok) return false;

    ok = builder.build(nodeCountB_);
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
    BufferUtils::destroyBuffers(allocator_, selectedClusterBuffers_);
    BufferUtils::destroyBuffers(allocator_, selectedCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, lodSelectUniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, nodeBufferA_);
    BufferUtils::destroyBuffers(allocator_, nodeBufferB_);
    BufferUtils::destroyBuffers(allocator_, nodeCountA_);
    BufferUtils::destroyBuffers(allocator_, nodeCountB_);
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

bool TwoPassCuller::createLODSelectPipeline() {
    if (!raiiDevice_) return false;

    // Descriptor set layout matching cluster_select.comp bindings (0-8)
    std::array<vk::DescriptorSetLayoutBinding, 9> bindings;
    bindings[0] = vk::DescriptorSetLayoutBinding{}.setBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // clusters
    bindings[1] = vk::DescriptorSetLayoutBinding{}.setBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // instances
    bindings[2] = vk::DescriptorSetLayoutBinding{}.setBinding(2)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // selected clusters output
    bindings[3] = vk::DescriptorSetLayoutBinding{}.setBinding(3)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // selected count
    bindings[4] = vk::DescriptorSetLayoutBinding{}.setBinding(4)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // select uniforms
    bindings[5] = vk::DescriptorSetLayoutBinding{}.setBinding(5)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // input nodes
    bindings[6] = vk::DescriptorSetLayoutBinding{}.setBinding(6)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // input node count
    bindings[7] = vk::DescriptorSetLayoutBinding{}.setBinding(7)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // output nodes
    bindings[8] = vk::DescriptorSetLayoutBinding{}.setBinding(8)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // output node count

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);

    try {
        lodSelectDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create LOD select desc set layout: %s", e.what());
        return false;
    }

    vk::DescriptorSetLayout vkDescLayout(**lodSelectDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}.setSetLayouts(vkDescLayout);

    vk::Device vkDevice(device_);
    lodSelectPipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load compute shader
    auto compModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/cluster_select.comp.spv");
    if (!compModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to load cluster_select.comp shader");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*compModule)
        .setPName("main");

    auto computeInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(lodSelectPipelineLayout_);

    auto result = vkDevice.createComputePipeline(nullptr, computeInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create LOD select compute pipeline");
        vkDevice.destroyShaderModule(*compModule);
        return false;
    }
    lodSelectPipeline_ = static_cast<VkPipeline>(result.value);

    vkDevice.destroyShaderModule(*compModule);

    SDL_Log("TwoPassCuller: LOD selection compute pipeline created");
    return true;
}

void TwoPassCuller::destroyLODSelectPipeline() {
    if (lodSelectPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, lodSelectPipeline_, nullptr);
        lodSelectPipeline_ = VK_NULL_HANDLE;
    }
    if (lodSelectPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, lodSelectPipelineLayout_, nullptr);
        lodSelectPipelineLayout_ = VK_NULL_HANDLE;
    }
    lodSelectDescSetLayout_.reset();
}

void TwoPassCuller::setRootClusters(const std::vector<uint32_t>& rootIndices) {
    rootClusterIndices_ = rootIndices;
    SDL_Log("TwoPassCuller: Set %zu root clusters for DAG traversal",
            rootClusterIndices_.size());
}

void TwoPassCuller::recordLODSelection(VkCommandBuffer cmd, uint32_t frameIndex,
                                         uint32_t totalDAGClusters, uint32_t instanceCount) {
    // Update LOD selection uniforms
    ClusterSelectUniforms selectUniforms{};

    // Reuse the viewProjMatrix from the cull uniforms (already written this frame)
    void* cullMapped = uniformBuffers_.mappedPointers[frameIndex];
    if (cullMapped) {
        auto* cullUbo = static_cast<const ClusterCullUniforms*>(cullMapped);
        selectUniforms.viewProjMatrix = cullUbo->viewProjMatrix;
        selectUniforms.screenParams = cullUbo->screenParams;
    }

    selectUniforms.totalClusterCount = totalDAGClusters;
    selectUniforms.instanceCount = instanceCount;
    selectUniforms.errorThreshold = errorThreshold_;
    selectUniforms.maxSelectedClusters = maxClusters_;

    void* mapped = lodSelectUniformBuffers_.mappedPointers[frameIndex];
    if (mapped) {
        memcpy(mapped, &selectUniforms, sizeof(selectUniforms));
        vmaFlushAllocation(allocator_, lodSelectUniformBuffers_.allocations[frameIndex],
                           0, sizeof(selectUniforms));
    }

    // Clear selected count to 0 (accumulated across all passes)
    vkCmdFillBuffer(cmd, selectedCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);

    // Seed input buffer A with root cluster indices
    if (!rootClusterIndices_.empty()) {
        VkDeviceSize seedSize = rootClusterIndices_.size() * sizeof(uint32_t);
        vkCmdUpdateBuffer(cmd, nodeBufferA_.buffers[frameIndex], 0,
                          seedSize, rootClusterIndices_.data());

        // Write root count to nodeCountA
        uint32_t rootCount = static_cast<uint32_t>(rootClusterIndices_.size());
        vkCmdUpdateBuffer(cmd, nodeCountA_.buffers[frameIndex], 0,
                          sizeof(uint32_t), &rootCount);
    } else {
        // No roots — write 0 count
        vkCmdFillBuffer(cmd, nodeCountA_.buffers[frameIndex], 0, sizeof(uint32_t), 0);
    }

    // Barrier: transfer writes -> compute reads
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    // Bind LOD selection pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lodSelectPipeline_);

    // Multi-pass top-down traversal: one dispatch per DAG level
    // Each pass reads from the input buffer and writes children to the output buffer.
    // Selected clusters are accumulated into selectedClusterBuffers_ across all passes.
    //
    // We dispatch ceil(maxClusters/64) workgroups each pass. Threads beyond
    // the actual node count early-exit via the inputNodeCount SSBO check.
    uint32_t workGroupSize = 64;
    uint32_t maxDispatch = (maxClusters_ + workGroupSize - 1) / workGroupSize;

    // Ping-pong: even levels read A/write B, odd levels read B/write A
    auto* inputBuffers = &nodeBufferA_;
    auto* inputCountBuffers = &nodeCountA_;
    auto* outputBuffers = &nodeBufferB_;
    auto* outputCountBuffers = &nodeCountB_;

    for (uint32_t level = 0; level < maxDAGLevels_; ++level) {
        // Clear output node count for this pass
        vkCmdFillBuffer(cmd, outputCountBuffers->buffers[frameIndex], 0, sizeof(uint32_t), 0);

        // Barrier: clear -> compute
        memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &memBarrier, 0, nullptr, 0, nullptr);

        // Descriptor sets would be bound here for (input, output) pair
        // Bindings 5-8 change each pass for the ping-pong:
        //   5 = inputBuffers, 6 = inputCountBuffers,
        //   7 = outputBuffers, 8 = outputCountBuffers

        // Dispatch
        vkCmdDispatch(cmd, maxDispatch, 1, 1);

        // Barrier: compute writes -> next pass compute reads + transfer (for clear)
        memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &memBarrier, 0, nullptr, 0, nullptr);

        // Swap ping-pong: output becomes input for next level
        std::swap(inputBuffers, outputBuffers);
        std::swap(inputCountBuffers, outputCountBuffers);
    }

    // Final barrier: compute write -> compute read (selected clusters -> cull pass)
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

VkBuffer TwoPassCuller::getSelectedClusterBuffer(uint32_t frameIndex) const {
    return selectedClusterBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getSelectedCountBuffer(uint32_t frameIndex) const {
    return selectedCountBuffers_.buffers[frameIndex];
}

bool TwoPassCuller::createDescriptorSets() {
    // Descriptor sets will be allocated and updated per-frame when needed
    return true;
}

void TwoPassCuller::destroyDescriptorSets() {
    pass1DescSets_.clear();
    pass2DescSets_.clear();
    lodSelectDescSetsAB_.clear();
    lodSelectDescSetsBA_.clear();
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
