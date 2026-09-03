#include "TreeLeafCulling.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ShaderLoader.h"
#include "Bindings.h"
#include "UBOs.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/ComputeShaderCommon.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <numeric>

std::unique_ptr<TreeLeafCulling> TreeLeafCulling::create(const InitInfo& info) {
    auto culling = std::make_unique<TreeLeafCulling>(ConstructToken{});
    if (!culling->initInternal(info)) {
        return nullptr;
    }
    return culling;
}

bool TreeLeafCulling::initInternal(const InitInfo& info) {
    raiiDevice_ = info.raiiDevice;
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling requires raiiDevice");
        return false;
    }
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    resourcePath_ = info.resourcePath;
    maxFramesInFlight_ = info.maxFramesInFlight;
    terrainSize_ = info.terrainSize;
    deferredRelease_.setFramesInFlight(maxFramesInFlight_);

    if (!createLeafCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Culling pipeline not available, using direct rendering");
        return true; // Graceful degradation
    }

    if (!createCellCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell culling pipeline not available");
    }

    if (!createTreeFilterPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Tree filter pipeline not available");
    }

    if (!createTwoPhaseLeafCullPipeline()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Two-phase leaf cull pipeline not available");
    }

    SDL_Log("TreeLeafCulling initialized successfully");
    return true;
}

bool TreeLeafCulling::createLeafCullPipeline() {
    // Use LayoutBuilder to reduce boilerplate
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::TREE_LEAF_CULL_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_LEAF_CULL_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    vk::DescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull descriptor set layout");
        return false;
    }
    cullDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**cullDescriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull pipeline layout");
        return false;
    }
    cullPipelineLayout_ = std::move(layoutOpt);

    std::string shaderPath = resourcePath_ + "/shaders/tree_leaf_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    vk::ShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(**cullPipelineLayout_);

    cullPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(computeShaderModule);

    SDL_Log("TreeLeafCulling: Created leaf culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createLeafCullBuffers(uint32_t maxLeafInstances, uint32_t numTrees) {
    numTreesForIndirect_ = numTrees;

    // Use a fixed budget for visible leaf output rather than sizing for all possible instances.
    // The GPU culling pass outputs only visible leaves, so we size for expected maximum visibility:
    // - ~50-100 trees visible at once (close enough for full leaf detail)
    // - ~2000-3000 leaves per tree on average
    // - Total: ~100k-300k visible leaf instances per type at peak
    //
    // Using 200k per type = 800k total * 48 bytes * 3 frames = ~115MB
    // This is a reasonable GPU memory budget for leaf rendering.
    // LOD-based distance dropping (like grass) reduces count with distance,
    // preventing the need for hard budget limits that cause flickering.
    constexpr uint32_t MAX_VISIBLE_LEAVES_PER_TYPE = 200000;
    maxLeavesPerType_ = MAX_VISIBLE_LEAVES_PER_TYPE;

    // Only log if input was much larger (avoid spam for reasonable inputs)
    if (maxLeafInstances > MAX_VISIBLE_LEAVES_PER_TYPE * 4) {
        SDL_Log("TreeLeafCulling: Using fixed output budget of %u leaves/type (input was %u total)",
                MAX_VISIBLE_LEAVES_PER_TYPE, maxLeafInstances);
    }

    cullOutputBufferSize_ = NUM_LEAF_TYPES * maxLeavesPerType_ * sizeof(WorldLeafInstanceGPU);

    // Use FrameIndexedBuffers for type-safe per-frame buffer management
    // This prevents the common desync bug where a separate counter gets out of sync with frameIndex
    if (!cullOutputBuffers_.resize(
            allocator_, maxFramesInFlight_, cullOutputBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull output buffers");
        return false;
    }

    vk::DeviceSize indirectBufferSize = NUM_LEAF_TYPES * sizeof(VkDrawIndexedIndirectCommand);
    if (!cullIndirectBuffers_.resize(
            allocator_, maxFramesInFlight_, indirectBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull indirect buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator_,
            BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(maxFramesInFlight_)
                .setSize(sizeof(CullingUniforms))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            cullUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull uniform buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator_,
            BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(maxFramesInFlight_)
                .setSize(sizeof(LeafCullParams))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            leafCullParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create leaf cull params buffers");
        return false;
    }

    // Triple-buffered tree data buffers to prevent race conditions.
    // These are updated every frame via vkCmdUpdateBuffer, so they must be
    // triple-buffered to avoid GPU reading from a buffer that another frame is writing to.
    treeDataBufferSize_ = numTrees * sizeof(TreeCullData);
    if (!treeDataBuffers_.resize(
            allocator_, maxFramesInFlight_, treeDataBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree cull data buffers");
        return false;
    }

    treeRenderDataBufferSize_ = numTrees * sizeof(TreeRenderDataGPU);
    if (!treeRenderDataBuffers_.resize(
            allocator_, maxFramesInFlight_, treeRenderDataBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree render data buffers");
        return false;
    }

    cullDescriptorSets_ = descriptorPool_->allocate(**cullDescriptorSetLayout_, maxFramesInFlight_);
    if (cullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate cull descriptor sets");
        return false;
    }
    cullDescriptorsDirtyMask_ = ~0u;

    // cullOutputBuffers_/cullIndirectBuffers_/cullUniformBuffers_/treeDataBuffers_ created;
    // the two-phase leaf cull sets bind these, so mark them for rewrite.
    twoPhaseLeafCullDescriptorsDirtyMask_ = ~0u;

    SDL_Log("TreeLeafCulling: Created leaf culling buffers (max %u instances, %u trees, %.2f MB output)",
            maxLeafInstances, numTrees,
            static_cast<float>(cullOutputBufferSize_ * maxFramesInFlight_) / (1024.0f * 1024.0f));
    return true;
}

bool TreeLeafCulling::createCellCullPipeline() {
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::TREE_CELL_CULL_CELLS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_VISIBLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_CELL_CULL_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    vk::DescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull descriptor set layout");
        return false;
    }
    cellCullDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**cellCullDescriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull pipeline layout");
        return false;
    }
    cellCullPipelineLayout_ = std::move(layoutOpt);

    std::string shaderPath = resourcePath_ + "/shaders/tree_cell_cull.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    vk::ShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(**cellCullPipelineLayout_);

    cellCullPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(computeShaderModule);

    SDL_Log("TreeLeafCulling: Created cell culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createCellCullBuffers() {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cannot create cell cull buffers without valid spatial index");
        return false;
    }

    // Guard against null layout (shouldn't happen, but defensive check)
    if (!cellCullDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cell cull descriptor set layout is null");
        return false;
    }

    uint32_t numCells = spatialIndex_->getCellCount();
    visibleCellBufferSize_ = (numCells + 1) * sizeof(uint32_t);

    // Triple-buffered visible cell buffer to prevent race conditions between frames
    if (!visibleCellBuffers_.resize(
            allocator_, maxFramesInFlight_, visibleCellBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create visible cell buffers");
        return false;
    }

    // Triple-buffered indirect buffer to prevent race conditions between frames
    // Includes bucket counts/offsets for distance-sorted processing
    // Layout: dispatchX, dispatchY, dispatchZ, totalVisibleTrees, bucketCounts[8], bucketOffsets[8]
    constexpr uint32_t NUM_DISTANCE_BUCKETS = 8;
    vk::DeviceSize indirectBufferSize = (4 + NUM_DISTANCE_BUCKETS * 2) * sizeof(uint32_t);  // 20 uints = 80 bytes

    if (!cellCullIndirectBuffers_.resize(
            allocator_, maxFramesInFlight_, indirectBufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull indirect buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator_,
            BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(maxFramesInFlight_)
                .setSize(sizeof(CullingUniforms))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            cellCullUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull uniform buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator_,
            BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(maxFramesInFlight_)
                .setSize(sizeof(CellCullParams))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            cellCullParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cell cull params buffers");
        return false;
    }

    if (cellCullDescriptorSets_.empty()) {
        cellCullDescriptorSets_ = descriptorPool_->allocate(**cellCullDescriptorSetLayout_, maxFramesInFlight_);
        if (cellCullDescriptorSets_.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate cell cull descriptor sets");
            return false;
        }
    }

    // Bindings are written per frame slot in recordCulling() (see
    // writeCellCullDescriptorSet): a slot's set may only be written once that
    // slot's fence has been waited.
    cellCullDescriptorsDirtyMask_ = ~0u;

    SDL_Log("TreeLeafCulling: Created cell culling buffers (%u cells, %.2f KB visible buffer x %u frames)",
            numCells, visibleCellBufferSize_ / 1024.0f, maxFramesInFlight_);
    return true;
}

bool TreeLeafCulling::createTreeFilterPipeline() {
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::TREE_FILTER_ALL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_VISIBLE_CELLS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_CELL_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_SORTED_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_VISIBLE_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::TREE_FILTER_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    vk::DescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter descriptor set layout");
        return false;
    }
    treeFilterDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**treeFilterDescriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter pipeline layout");
        return false;
    }
    treeFilterPipelineLayout_ = std::move(layoutOpt);

    std::string shaderPath = resourcePath_ + "/shaders/tree_filter.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Tree filter shader not found: %s", shaderPath.c_str());
        return false;
    }
    vk::ShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(**treeFilterPipelineLayout_);

    treeFilterPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(computeShaderModule);

    SDL_Log("TreeLeafCulling: Created tree filter compute pipeline");
    return true;
}

bool TreeLeafCulling::createTreeFilterBuffers(uint32_t maxTrees) {
    if (!spatialIndex_ || !spatialIndex_->isValid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Cannot create tree filter buffers without valid spatial index");
        return false;
    }

    // Guard against null layout
    if (!treeFilterDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Tree filter descriptor set layout is null");
        return false;
    }

    maxVisibleTrees_ = maxTrees;
    visibleTreeBufferSize_ = sizeof(uint32_t) + maxTrees * sizeof(VisibleTreeData);

    // On resize, frames in flight may still read the previous buffers: retire
    // them (destroyed after maxFramesInFlight_ frames) instead of destroying now.
    deferredRelease_.retire(std::move(visibleTreeBuffers_));
    deferredRelease_.retire(std::move(leafCullIndirectDispatchBuffers_));
    retirePerFrameBuffers(treeFilterUniformBuffers_);
    retirePerFrameBuffers(treeFilterParamsBuffers_);

    // Triple-buffered visible tree buffer to prevent race conditions between frames
    if (!visibleTreeBuffers_.resize(
            allocator_, maxFramesInFlight_, visibleTreeBufferSize_,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create visible tree buffers");
        return false;
    }

    // Triple-buffered indirect dispatch buffer to prevent race conditions between frames
    vk::DeviceSize indirectDispatchSize = 3 * sizeof(uint32_t);
    if (!leafCullIndirectDispatchBuffers_.resize(
            allocator_, maxFramesInFlight_, indirectDispatchSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create leaf cull indirect dispatch buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator_,
            BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(maxFramesInFlight_)
                .setSize(sizeof(CullingUniforms))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            treeFilterUniformBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter uniform buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator_,
            BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(maxFramesInFlight_)
                .setSize(sizeof(TreeFilterParams))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            treeFilterParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create tree filter params buffers");
        return false;
    }

    if (treeFilterDescriptorSets_.empty()) {
        treeFilterDescriptorSets_ = descriptorPool_->allocate(**treeFilterDescriptorSetLayout_, maxFramesInFlight_);
        if (treeFilterDescriptorSets_.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate tree filter descriptor sets");
            return false;
        }
    }

    // Bindings are written per frame slot in recordCulling() (see
    // writeTreeFilterDescriptorSet); the other slots' sets may still be bound
    // in executing command buffers that read the retired buffers.
    treeFilterDescriptorsDirtyMask_ = ~0u;

    SDL_Log("TreeLeafCulling: Created tree filter buffers (max %u trees, %.2f KB visible tree buffer x %u frames)",
            maxTrees, visibleTreeBufferSize_ / 1024.0f, maxFramesInFlight_);
    // visibleTreeBuffers_ (and siblings) were (re)created; the two-phase sets bind them.
    twoPhaseLeafCullDescriptorsDirtyMask_ = ~0u;
    return true;
}

bool TreeLeafCulling::createTwoPhaseLeafCullPipeline() {
    DescriptorManager::LayoutBuilder builder(device_);
    builder.addBinding(Bindings::LEAF_CULL_P3_VISIBLE_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_ALL_TREES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_INPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_INDIRECT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_CULLING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
           .addBinding(Bindings::LEAF_CULL_P3_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

    vk::DescriptorSetLayout rawLayout = builder.build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create two-phase leaf cull descriptor set layout");
        return false;
    }
    twoPhaseLeafCullDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**twoPhaseLeafCullDescriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create two-phase leaf cull pipeline layout");
        return false;
    }
    twoPhaseLeafCullPipelineLayout_ = std::move(layoutOpt);

    std::string shaderPath = resourcePath_ + "/shaders/tree_leaf_cull_phase3.comp.spv";
    auto shaderModuleOpt = ShaderLoader::loadShaderModule(device_, shaderPath);
    if (!shaderModuleOpt.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Two-phase leaf cull shader not found: %s", shaderPath.c_str());
        return false;
    }
    vk::ShaderModule computeShaderModule = shaderModuleOpt.value();

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(computeShaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStageInfo)
        .setLayout(**twoPhaseLeafCullPipelineLayout_);

    twoPhaseLeafCullPipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);

    vk::Device vkDevice(device_);
    vkDevice.destroyShaderModule(computeShaderModule);

    SDL_Log("TreeLeafCulling: Created two-phase leaf culling compute pipeline");
    return true;
}

bool TreeLeafCulling::createTwoPhaseLeafCullDescriptorSets() {
    // Guard against null layout
    if (!twoPhaseLeafCullDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Two-phase leaf cull descriptor set layout is null");
        return false;
    }

    // Create params buffer for phase 3
    if (!BufferUtils::MappedFrameBuffers::build(allocator_,
            BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator_)
                .setFrameCount(maxFramesInFlight_)
                .setSize(sizeof(LeafCullP3Params))
                .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
            leafCullP3ParamsBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create leaf cull P3 params buffers");
        return false;
    }

    twoPhaseLeafCullDescriptorSets_ = descriptorPool_->allocate(**twoPhaseLeafCullDescriptorSetLayout_, maxFramesInFlight_);
    if (twoPhaseLeafCullDescriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to allocate two-phase leaf cull descriptor sets");
        return false;
    }

    SDL_Log("TreeLeafCulling: Allocated %u two-phase leaf cull descriptor sets", maxFramesInFlight_);
    twoPhaseLeafCullDescriptorsDirtyMask_ = ~0u;
    return true;
}

void TreeLeafCulling::writeTwoPhaseLeafCullDescriptorSet(const TreeSystem& treeSystem, uint32_t frameIndex) {
    if (twoPhaseLeafCullDescriptorSets_.empty()) return;

    // Only the current frame's set may be written: its fence has been waited,
    // while the other frames' sets can still be bound in executing command
    // buffers (updating those is undefined behavior).
    const uint32_t f = frameIndex;
    DescriptorManager::SetWriter writer(device_, twoPhaseLeafCullDescriptorSets_[f]);
    writer.writeBuffer(Bindings::LEAF_CULL_P3_VISIBLE_TREES, visibleTreeBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_ALL_TREES, treeDataBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_INPUT, treeSystem.getLeafInstanceBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_OUTPUT, cullOutputBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_INDIRECT, cullIndirectBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_CULLING, cullUniformBuffers_.get(f), 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .writeBuffer(Bindings::LEAF_CULL_P3_PARAMS, leafCullP3ParamsBuffers_.get(f), 0, sizeof(LeafCullP3Params), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .update();
}

void TreeLeafCulling::writeCellCullDescriptorSet(uint32_t frameIndex) {
    if (cellCullDescriptorSets_.empty() || !spatialIndex_) return;
    const uint32_t f = frameIndex;
    DescriptorManager::SetWriter writer(device_, cellCullDescriptorSets_[f]);
    writer.writeBuffer(Bindings::TREE_CELL_CULL_CELLS, spatialIndex_->getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_CELL_CULL_VISIBLE, visibleCellBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_CELL_CULL_INDIRECT, cellCullIndirectBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_CELL_CULL_CULLING, cellCullUniformBuffers_.get(f), 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .writeBuffer(Bindings::TREE_CELL_CULL_PARAMS, cellCullParamsBuffers_.get(f), 0, sizeof(CellCullParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .update();
}

void TreeLeafCulling::writeTreeFilterDescriptorSet(uint32_t frameIndex) {
    if (treeFilterDescriptorSets_.empty() || !spatialIndex_) return;
    const uint32_t f = frameIndex;
    DescriptorManager::SetWriter writer(device_, treeFilterDescriptorSets_[f]);
    writer.writeBuffer(Bindings::TREE_FILTER_ALL_TREES, treeDataBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_FILTER_VISIBLE_CELLS, visibleCellBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_FILTER_CELL_DATA, spatialIndex_->getCellBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_FILTER_SORTED_TREES, spatialIndex_->getSortedTreeBuffer(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_FILTER_VISIBLE_TREES, visibleTreeBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_FILTER_INDIRECT, leafCullIndirectDispatchBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_FILTER_CULLING, treeFilterUniformBuffers_.get(f), 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .writeBuffer(Bindings::TREE_FILTER_PARAMS, treeFilterParamsBuffers_.get(f), 0, sizeof(TreeFilterParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .update();
}

void TreeLeafCulling::retirePerFrameBuffers(BufferUtils::MappedFrameBuffers& buffers) {
    // MappedFrameBuffers is a movable owner; the release queue destroys it after
    // maxFramesInFlight_ ticks. The member is left empty for the rebuild.
    if (!buffers.empty()) {
        deferredRelease_.retire(std::move(buffers));
    }
    buffers = BufferUtils::MappedFrameBuffers{};
}

void TreeLeafCulling::updateSpatialIndex(const TreeSystem& treeSystem) {
    const auto& leafRenderables = treeSystem.getLeafRenderables();
    const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();

    if (leafRenderables.empty()) {
        spatialIndex_.reset();
        return;
    }

    if (!spatialIndex_) {
        TreeSpatialIndex::InitInfo indexInfo{};
        indexInfo.device = device_;
        indexInfo.allocator = allocator_;
        indexInfo.cellSize = 64.0f;
        indexInfo.worldSize = terrainSize_;
        indexInfo.maxFramesInFlight = maxFramesInFlight_;

        spatialIndex_ = TreeSpatialIndex::create(indexInfo);
        if (!spatialIndex_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create spatial index");
            return;
        }
    }

    // Build transforms and scales from leafRenderables
    // CRITICAL: The spatial index must use the SAME filtering as recordCulling()
    // to ensure originalTreeIndex matches the index into the TreeCullData buffer.
    // Trees with invalid leafInstanceIndex or zero instanceCount are filtered out
    // in both places to maintain index consistency.
    std::vector<glm::mat4> transforms;
    std::vector<float> scales;
    transforms.reserve(leafRenderables.size());
    scales.reserve(leafRenderables.size());
    for (const auto& renderable : leafRenderables) {
        // Apply same filtering as recordCulling() to ensure index consistency
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                transforms.push_back(renderable.transform);
                // Estimate scale from transform (use Y-axis length as approximation)
                float scale = glm::length(glm::vec3(renderable.transform[1]));
                scales.push_back(scale);
            }
        }
    }

    spatialIndex_->rebuild(transforms, scales);

    // Grow the per-tree buffers here, before this frame's graphics descriptor
    // sets are written (frame updater order), so the culled-leaf draw binds the
    // new treeRenderDataBuffers_ slot that recordCulling() uploads into. The
    // filter above matches recordCulling(), so transforms.size() is its numTrees.
    if (!treeDataBuffers_.empty()) {
        growTreeBuffers(static_cast<uint32_t>(transforms.size()));
    }

    // Old spatial index buffers go to deferredRelease_; frames in flight may still read them.
    if (!spatialIndex_->uploadToGPU(deferredRelease_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to upload spatial index to GPU");
        return;
    }

    if (visibleCellBuffers_.empty() && cellCullPipeline_) {
        createCellCullBuffers();
    } else if (!cellCullDescriptorSets_.empty()) {
        // Spatial index buffers were recreated (uploadToGPU() retires and recreates
        // them) - each frame slot rewrites its own set on its next record.
        cellCullDescriptorsDirtyMask_ = ~0u;
    }

    uint32_t requiredTreeCapacity = static_cast<uint32_t>(leafRenderables.size());
    bool needsTreeFilterBuffers = visibleTreeBuffers_.empty() ||
                                  requiredTreeCapacity > maxVisibleTrees_;

    // Only create tree filter buffers if tree data buffers are already initialized
    // (they get created lazily during first recordCulling call)
    if (needsTreeFilterBuffers && treeFilterPipeline_ &&
        !visibleCellBuffers_.empty() && !treeDataBuffers_.empty()) {
        // Old buffers are retired inside createTreeFilterBuffers (no device wait).
        if (!visibleTreeBuffers_.empty()) {
            SDL_Log("TreeLeafCulling: Resizing visible tree buffer from %u to %u trees",
                    maxVisibleTrees_, requiredTreeCapacity);
        }
        createTreeFilterBuffers(requiredTreeCapacity);
    } else if (!treeFilterDescriptorSets_.empty()) {
        // Spatial index buffers were recreated - each frame slot rewrites its own set.
        treeFilterDescriptorsDirtyMask_ = ~0u;
    }

    if (twoPhaseLeafCullDescriptorSets_.empty() && twoPhaseLeafCullPipeline_ &&
        !visibleTreeBuffers_.empty()) {
        createTwoPhaseLeafCullDescriptorSets();
    }

    SDL_Log("TreeLeafCulling: Updated spatial index (%zu trees, %u non-empty cells)",
            leafRenderables.size(), spatialIndex_->getNonEmptyCellCount());
}

void TreeLeafCulling::writeCullDescriptorSet(const TreeSystem& treeSystem, uint32_t frameIndex) {
    if (cullDescriptorSets_.empty()) return;
    // Frame slot f's set points to slot f's buffers (triple-buffering). Only the
    // slot being recorded is written; its fence has been waited, the others may
    // still be bound in executing command buffers.
    const uint32_t f = frameIndex;
    DescriptorManager::SetWriter writer(device_, cullDescriptorSets_[f]);
    writer.writeBuffer(Bindings::TREE_LEAF_CULL_INPUT, treeSystem.getLeafInstanceBuffer(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_LEAF_CULL_OUTPUT, cullOutputBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_LEAF_CULL_INDIRECT, cullIndirectBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_LEAF_CULL_CULLING, cullUniformBuffers_.get(f), 0, sizeof(CullingUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .writeBuffer(Bindings::TREE_LEAF_CULL_TREES, treeDataBuffers_.getVk(f), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .writeBuffer(Bindings::TREE_LEAF_CULL_PARAMS, leafCullParamsBuffers_.get(f), 0, sizeof(LeafCullParams), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .update();
}

bool TreeLeafCulling::growTreeBuffers(uint32_t numTrees) {
    if (numTrees <= numTreesForIndirect_) return true;

    SDL_Log("TreeLeafCulling: Tree count increased from %u to %u, resizing buffers",
            numTreesForIndirect_, numTrees);

    // Allocate the replacements first so a failure leaves the current buffers
    // (and every descriptor set that references them) intact.
    const vk::DeviceSize treeDataSize = numTrees * sizeof(TreeCullData);
    const vk::DeviceSize renderDataSize = numTrees * sizeof(TreeRenderDataGPU);
    const auto usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;

    BufferUtils::FrameIndexedBuffers newTreeData;
    if (!newTreeData.resize(allocator_, maxFramesInFlight_, treeDataSize, usage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to resize tree data buffers");
        return false;
    }
    BufferUtils::FrameIndexedBuffers newRenderData;
    if (!newRenderData.resize(allocator_, maxFramesInFlight_, renderDataSize, usage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to resize tree render data buffers");
        return false;
    }

    // Frames in flight (and this slot's already-written graphics set) may still
    // read the old buffers: retire them (destroyed maxFramesInFlight_ frames
    // from now) rather than waiting for the device.
    deferredRelease_.retire(std::move(treeDataBuffers_));
    deferredRelease_.retire(std::move(treeRenderDataBuffers_));
    treeDataBuffers_ = std::move(newTreeData);
    treeRenderDataBuffers_ = std::move(newRenderData);
    treeDataBufferSize_ = treeDataSize;
    treeRenderDataBufferSize_ = renderDataSize;
    numTreesForIndirect_ = numTrees;

    // Buffer handles changed: every set group that binds treeDataBuffers_
    // (cull: TREE_LEAF_CULL_TREES, tree filter: TREE_FILTER_ALL_TREES,
    // two-phase: LEAF_CULL_P3_ALL_TREES) is marked dirty. Each frame slot
    // rewrites only its own set when it is next recorded - the other slots'
    // sets may still be bound in executing command buffers. The graphics-side
    // culled-leaf set is rewritten every frame by TreeRenderer from
    // getTreeRenderDataBuffer(frameIndex).
    cullDescriptorsDirtyMask_ = ~0u;
    treeFilterDescriptorsDirtyMask_ = ~0u;
    twoPhaseLeafCullDescriptorsDirtyMask_ = ~0u;
    return true;
}

void TreeLeafCulling::recordCulling(vk::CommandBuffer cmd, uint32_t frameIndex,
                                     const TreeSystem& treeSystem,
                                     const TreeLODSystem* lodSystem,
                                     const glm::vec3& cameraPos,
                                     const glm::vec4* frustumPlanes) {
    if (!isEnabled()) return;

    // Once per frame: release buffers retired maxFramesInFlight_ frames ago.
    // recordCulling() is the single per-frame entry point of this system.
    deferredRelease_.tick();

    const auto& leafRenderables = treeSystem.getLeafRenderables();
    const auto& leafDrawInfo = treeSystem.getLeafDrawInfo();

    if (leafRenderables.empty() || leafDrawInfo.empty()) return;

    vk::CommandBuffer vkCmd(cmd);

    // Build per-tree data for batched culling
    std::vector<TreeCullData> treeDataList;
    std::vector<TreeRenderDataGPU> treeRenderDataList;
    treeDataList.reserve(leafRenderables.size());
    treeRenderDataList.reserve(leafRenderables.size());

    uint32_t numTrees = 0;
    uint32_t totalLeafInstances = 0;

    for (const auto& renderable : leafRenderables) {
        if (renderable.leafInstanceIndex >= 0 &&
            static_cast<size_t>(renderable.leafInstanceIndex) < leafDrawInfo.size()) {
            const auto& drawInfo = leafDrawInfo[renderable.leafInstanceIndex];
            if (drawInfo.instanceCount > 0) {
                float lodBlendFactor = 0.0f;
                if (lodSystem) {
                    // Use leafInstanceIndex (== tree instance index) for LOD lookup
                    // This correctly maps to treeInstances_ even if some trees have no leaves
                    lodBlendFactor = lodSystem->getBlendFactor(static_cast<uint32_t>(renderable.leafInstanceIndex));
                }

                uint32_t leafTypeIdx = LEAF_TYPE_OAK;
                if (renderable.leafType == "ash") leafTypeIdx = LEAF_TYPE_ASH;
                else if (renderable.leafType == "aspen") leafTypeIdx = LEAF_TYPE_ASPEN;
                else if (renderable.leafType == "pine") leafTypeIdx = LEAF_TYPE_PINE;

                static bool loggedOnce = false;
                if (!loggedOnce && numTrees < 10) {
                    SDL_Log("TreeLeafCulling: Tree %u: leafType='%s' -> leafTypeIdx=%u, firstInst=%u, count=%u",
                            numTrees, renderable.leafType.c_str(), leafTypeIdx,
                            drawInfo.firstInstance, drawInfo.instanceCount);
                    if (numTrees == 9) loggedOnce = true;
                }

                TreeCullData treeData{};
                treeData.treeModel = renderable.transform;
                treeData.inputFirstInstance = drawInfo.firstInstance;
                treeData.inputInstanceCount = drawInfo.instanceCount;
                treeData.treeIndex = numTrees;
                treeData.leafTypeIndex = leafTypeIdx;
                treeData.lodBlendFactor = lodBlendFactor;
                treeDataList.push_back(treeData);

                TreeRenderDataGPU renderData{};
                renderData.model = renderable.transform;
                renderData.tintAndParams = glm::vec4(renderable.leafTint, renderable.autumnHueShift);
                float windOffset = glm::fract(renderable.transform[3][0] * 0.1f + renderable.transform[3][2] * 0.1f) * 6.28318f;
                renderData.windOffsetAndLOD = glm::vec4(windOffset, lodBlendFactor, 0.0f, 0.0f);
                treeRenderDataList.push_back(renderData);

                totalLeafInstances += drawInfo.instanceCount;
                numTrees++;
            }
        }
    }
    if (numTrees == 0 || totalLeafInstances == 0) return;

    // CRITICAL: Sort tree data by inputFirstInstance for binary search in shader.
    // The shader's binary search assumes trees are sorted by their leaf instance range.
    // If trees were added in non-sequential order, the search fails and defaults to
    // tree 0 (usually oak), causing all leaves to render as oak.
    std::vector<size_t> sortIndices(treeDataList.size());
    std::iota(sortIndices.begin(), sortIndices.end(), 0);
    std::sort(sortIndices.begin(), sortIndices.end(), [&](size_t a, size_t b) {
        return treeDataList[a].inputFirstInstance < treeDataList[b].inputFirstInstance;
    });

    // Reorder both lists according to sorted indices
    std::vector<TreeCullData> sortedTreeData(treeDataList.size());
    std::vector<TreeRenderDataGPU> sortedRenderData(treeRenderDataList.size());
    for (size_t i = 0; i < sortIndices.size(); ++i) {
        sortedTreeData[i] = treeDataList[sortIndices[i]];
        sortedTreeData[i].treeIndex = static_cast<uint32_t>(i);  // Update treeIndex to match new position
        sortedRenderData[i] = treeRenderDataList[sortIndices[i]];
    }

    treeDataList = std::move(sortedTreeData);
    treeRenderDataList = std::move(sortedRenderData);

    // Lazy initialization of cull buffers
    if (cullOutputBuffers_.empty()) {
        if (!createLeafCullBuffers(totalLeafInstances, numTrees)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeLeafCulling: Failed to create cull buffers");
            return;
        }
    }

    // CRITICAL: Check if tree count exceeds buffer capacity and resize if needed.
    // Without this check, vkCmdUpdateBuffer writes out of bounds when trees are added,
    // corrupting GPU memory and causing leaf type data to be misread (flickering oak leaves).
    // Fallback growth path. Normally the tree buffers are grown in
    // updateSpatialIndex() (called from the frame updater before the graphics
    // descriptor sets are written), so the culled-leaf draw of this frame binds
    // the new buffer. If the count still grew here, the graphics set for this
    // slot was written against the old buffer; growTreeBuffers() keeps that
    // buffer alive for maxFramesInFlight_ frames so the draw stays valid.
    if (numTrees > numTreesForIndirect_ && !growTreeBuffers(numTrees)) return;

    // The externally-owned leaf instance buffer is bound by the cull and
    // two-phase sets; a reallocation invalidates both.
    {
        vk::Buffer leafInstanceBuffer = treeSystem.getLeafInstanceBuffer();
        if (leafInstanceBuffer != lastLeafInstanceBuffer_) {
            cullDescriptorsDirtyMask_ = ~0u;
            twoPhaseLeafCullDescriptorsDirtyMask_ = ~0u;
            lastLeafInstanceBuffer_ = leafInstanceBuffer;
        }
    }

    // Reset all 4 indirect draw commands (one per leaf type: oak, ash, aspen, pine)
    constexpr uint32_t NUM_LEAF_TYPES = 4;

    VkDrawIndexedIndirectCommand indirectReset[NUM_LEAF_TYPES] = {};
    for (uint32_t i = 0; i < NUM_LEAF_TYPES; ++i) {
        indirectReset[i].indexCount = 6;       // Quad: 6 indices
        indirectReset[i].instanceCount = 0;    // Will be set by compute shader
        indirectReset[i].firstIndex = 0;
        indirectReset[i].vertexOffset = 0;
        indirectReset[i].firstInstance = i * maxLeavesPerType_;  // Base offset for each leaf type
    }

    vkCmd.updateBuffer(cullIndirectBuffers_.getVk(frameIndex),
                       0, sizeof(indirectReset), &indirectReset);

    // Upload per-tree data to frame-specific buffers (triple-buffered to avoid race conditions)
    vkCmd.updateBuffer(treeDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeCullData), treeDataList.data());
    vkCmd.updateBuffer(treeRenderDataBuffers_.getVk(frameIndex), 0,
                       numTrees * sizeof(TreeRenderDataGPU), treeRenderDataList.data());

    // Upload global uniforms - split into CullingUniforms and LeafCullParams
    CullingUniforms culling{};
    culling.cameraPosition = glm::vec4(cameraPos, 0.0f);
    for (int i = 0; i < 6; ++i) {
        culling.frustumPlanes[i] = frustumPlanes[i];
    }
    culling.maxDrawDistance = params_.maxDrawDistance;
    culling.lodTransitionStart = params_.lodTransitionStart;
    culling.lodTransitionEnd = params_.lodTransitionEnd;
    culling.maxLodDropRate = params_.maxLodDropRate;

    LeafCullParams leafParams{};
    leafParams.numTrees = numTrees;
    leafParams.totalLeafInstances = totalLeafInstances;
    leafParams.maxLeavesPerType = maxLeavesPerType_;

    vkCmd.updateBuffer(cullUniformBuffers_.get(frameIndex), 0,
                       sizeof(CullingUniforms), &culling);
    vkCmd.updateBuffer(leafCullParamsBuffers_.get(frameIndex), 0,
                       sizeof(LeafCullParams), &leafParams);

    // Barrier for buffer updates
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eUniformRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                          {}, barrier, {}, {});

    // Descriptor sets bind buffer[frameIndex] and are rewritten lazily per frame
    // slot when a bound buffer changes (see the *DescriptorsDirtyMask_ checks below).

    // Cell Culling (if spatial index available)
    if (isSpatialIndexEnabled() && cellCullPipeline_) {
        // Split cell cull uniforms: CullingUniforms + CellCullParams
        CullingUniforms cellCulling{};
        cellCulling.cameraPosition = glm::vec4(cameraPos, 1.0f);
        for (int i = 0; i < 6; ++i) {
            cellCulling.frustumPlanes[i] = frustumPlanes[i];
        }
        cellCulling.maxDrawDistance = 250.0f;
        cellCulling.lodTransitionStart = params_.lodTransitionStart;
        cellCulling.lodTransitionEnd = params_.lodTransitionEnd;
        cellCulling.maxLodDropRate = params_.maxLodDropRate;

        CellCullParams cellParams{};
        cellParams.numCells = spatialIndex_->getCellCount();
        cellParams.treesPerWorkgroup = 64;

        // Check if two-phase culling will be used so we can batch uniform updates
        bool useTwoPhase = twoPhaseEnabled_ && treeFilterPipeline_ &&
                           !visibleTreeBuffers_.empty() && !treeFilterDescriptorSets_.empty();

        // Reset cell cull output buffers on CPU side BEFORE dispatch
        // This is critical - shader-side initialization with barrier() only works within
        // a workgroup, not across workgroups. Other workgroups may atomicAdd before
        // workgroup 0 resets the counters, causing race conditions and flickering.
        // Using frame-indexed buffers to prevent race conditions between in-flight frames.

        // Reset visible cell buffer: first uint is visibleCellCount
        vkCmd.fillBuffer(visibleCellBuffers_.getVk(frameIndex), 0, sizeof(uint32_t), 0);

        // Reset cell cull indirect buffer: dispatchX/Y/Z, totalVisibleTrees, bucketCounts[8], bucketOffsets[8]
        // Structure: { dispatchX=0, dispatchY=1, dispatchZ=1, totalVisibleTrees=0, bucketCounts[8]=0, bucketOffsets[8]=0 }
        constexpr uint32_t NUM_DISTANCE_BUCKETS = 8;
        uint32_t cellIndirectReset[4 + NUM_DISTANCE_BUCKETS * 2] = {0, 1, 1, 0}; // dispatchX=0, Y=1, Z=1, totalTrees=0
        // bucketCounts and bucketOffsets are already 0-initialized
        vkCmd.updateBuffer(cellCullIndirectBuffers_.getVk(frameIndex), 0, sizeof(cellIndirectReset), cellIndirectReset);

        // If two-phase culling, also reset visible tree buffer and leaf cull indirect dispatch
        if (useTwoPhase) {
            // Reset visible tree buffer: first uint is visibleTreeCount
            vkCmd.fillBuffer(visibleTreeBuffers_.getVk(frameIndex), 0, sizeof(uint32_t), 0);

            // Reset leaf cull indirect dispatch: { dispatchX=0, dispatchY=1, dispatchZ=1 }
            uint32_t leafDispatchReset[3] = {0, 1, 1};
            vkCmd.updateBuffer(leafCullIndirectDispatchBuffers_.getVk(frameIndex), 0, sizeof(leafDispatchReset), leafDispatchReset);
        }

        // Use updateBuffer to avoid HOST→COMPUTE stall (keeps update on GPU timeline)
        vkCmd.updateBuffer(cellCullUniformBuffers_.get(frameIndex), 0,
                           sizeof(CullingUniforms), &cellCulling);
        vkCmd.updateBuffer(cellCullParamsBuffers_.get(frameIndex), 0,
                           sizeof(CellCullParams), &cellParams);

        // If two-phase culling is enabled, update tree filter uniforms now too
        // This allows us to combine barriers later (reducing pipeline bubbles)
        if (useTwoPhase) {
            // Split tree filter uniforms: CullingUniforms + TreeFilterParams
            CullingUniforms filterCulling{};
            filterCulling.cameraPosition = glm::vec4(cameraPos, 1.0f);
            for (int i = 0; i < 6; ++i) {
                filterCulling.frustumPlanes[i] = frustumPlanes[i];
            }
            filterCulling.maxDrawDistance = params_.maxDrawDistance;
            filterCulling.lodTransitionStart = params_.lodTransitionStart;
            filterCulling.lodTransitionEnd = params_.lodTransitionEnd;
            filterCulling.maxLodDropRate = params_.maxLodDropRate;

            TreeFilterParams filterParams{};
            filterParams.maxTreesPerCell = 64;
            filterParams.maxVisibleTrees = maxVisibleTrees_;

            vkCmd.updateBuffer(treeFilterUniformBuffers_.get(frameIndex), 0,
                               sizeof(CullingUniforms), &filterCulling);
            vkCmd.updateBuffer(treeFilterParamsBuffers_.get(frameIndex), 0,
                               sizeof(TreeFilterParams), &filterParams);
        }

        // Barrier for all buffer updates (uniforms AND storage buffers).
        // CRITICAL: Must include SHADER_READ_BIT | SHADER_WRITE_BIT because we reset
        // storage buffers (visibleCellBuffers_, visibleTreeBuffers_, etc.) via fillBuffer/updateBuffer.
        // Without this, compute shaders may read stale data before the reset completes.
        auto cellUniformBarrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eUniformRead | vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                              {}, cellUniformBarrier, {}, {});

        if (cellCullDescriptorsDirtyMask_ & (1u << frameIndex)) {
            writeCellCullDescriptorSet(frameIndex);
            cellCullDescriptorsDirtyMask_ &= ~(1u << frameIndex);
        }
        vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **cellCullPipeline_);
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **cellCullPipelineLayout_,
                                 0, vk::DescriptorSet(cellCullDescriptorSets_[frameIndex]), {});

        uint32_t cellWorkgroups = ComputeConstants::getDispatchCount1D(cellParams.numCells);
        vkCmd.dispatch(cellWorkgroups, 1, 1);

        // Tree Filtering (Two-Phase Culling)
        // Uniforms were already updated above, so we only need COMPUTE→COMPUTE barrier
        if (useTwoPhase) {
            // Single barrier: wait for cell cull shader writes before tree filter reads
            auto cellBarrier = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
            vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, cellBarrier, {}, {});

            if (treeFilterDescriptorsDirtyMask_ & (1u << frameIndex)) {
                writeTreeFilterDescriptorSet(frameIndex);
                treeFilterDescriptorsDirtyMask_ &= ~(1u << frameIndex);
            }
            vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **treeFilterPipeline_);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **treeFilterPipelineLayout_,
                                     0, vk::DescriptorSet(treeFilterDescriptorSets_[frameIndex]), {});

            vkCmd.dispatchIndirect(cellCullIndirectBuffers_.getVk(frameIndex), 0);

            auto treeFilterBarrier = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
            vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, treeFilterBarrier, {}, {});

            // Two-phase leaf culling
            if (twoPhaseLeafCullPipeline_ && !twoPhaseLeafCullDescriptorSets_.empty()) {
                // Upload phase 3 specific params
                LeafCullP3Params p3Params{};
                p3Params.maxLeavesPerType = maxLeavesPerType_;

                vkCmd.updateBuffer(leafCullP3ParamsBuffers_.get(frameIndex), 0,
                                   sizeof(LeafCullP3Params), &p3Params);

                auto p3UniformBarrier = vk::MemoryBarrier{}
                    .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                    .setDstAccessMask(vk::AccessFlagBits::eUniformRead);
                vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                                      {}, p3UniformBarrier, {}, {});

                // Descriptor bindings are frame-stable; rewrite only when a referenced
                // buffer changed (per-frame dirty bit, set above) - and only THIS
                // frame's set, since the other frames' sets may still be bound in
                // executing command buffers.
                if (twoPhaseLeafCullDescriptorsDirtyMask_ & (1u << frameIndex)) {
                    writeTwoPhaseLeafCullDescriptorSet(treeSystem, frameIndex);
                    twoPhaseLeafCullDescriptorsDirtyMask_ &= ~(1u << frameIndex);
                }

                vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **twoPhaseLeafCullPipeline_);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **twoPhaseLeafCullPipelineLayout_,
                                         0, vk::DescriptorSet(twoPhaseLeafCullDescriptorSets_[frameIndex]), {});

                vkCmd.dispatchIndirect(leafCullIndirectDispatchBuffers_.getVk(frameIndex), 0);

                barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
                vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                                      {}, barrier, {}, {});
                return;
            }
        }
    }

    // Fallback: Single-phase leaf culling
    if (cullDescriptorsDirtyMask_ & (1u << frameIndex)) {
        writeCullDescriptorSet(treeSystem, frameIndex);
        cullDescriptorsDirtyMask_ &= ~(1u << frameIndex);
    }
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **cullPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **cullPipelineLayout_,
                             0, vk::DescriptorSet(cullDescriptorSets_[frameIndex]), {});

    uint32_t workgroupCount = ComputeConstants::getDispatchCount1D(totalLeafInstances);
    vkCmd.dispatch(workgroupCount, 1, 1);

    barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
           .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                          {}, barrier, {}, {});
}
