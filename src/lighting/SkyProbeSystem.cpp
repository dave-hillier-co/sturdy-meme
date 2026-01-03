#include "SkyProbeSystem.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "CommandBufferUtils.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <fstream>
#include <cstring>
#include <cmath>

std::unique_ptr<SkyProbeSystem> SkyProbeSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<SkyProbeSystem>(new SkyProbeSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<SkyProbeSystem> SkyProbeSystem::create(const InitContext& ctx, const SkyProbeConfig& config) {
    InitInfo info;
    info.device = ctx.device;
    info.physicalDevice = ctx.physicalDevice;
    info.allocator = ctx.allocator;
    info.commandPool = ctx.commandPool;
    info.computeQueue = ctx.graphicsQueue;
    info.shaderPath = ctx.shaderPath;
    info.resourcePath = ctx.resourcePath;
    info.framesInFlight = ctx.framesInFlight;
    info.descriptorPool = ctx.descriptorPool;
    info.config = config;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

SkyProbeSystem::~SkyProbeSystem() {
    cleanup();
}

bool SkyProbeSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    computeQueue_ = info.computeQueue;
    shaderPath_ = info.shaderPath;
    resourcePath_ = info.resourcePath;
    framesInFlight_ = info.framesInFlight;
    descriptorPool_ = info.descriptorPool;
    config_ = info.config;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkyProbeSystem requires raiiDevice");
        return false;
    }

    // Initialize cascade states
    uint32_t layerOffset = 0;
    for (uint32_t i = 0; i < SkyProbeConfig::NUM_CASCADES; i++) {
        cascadeStates_[i].origin = glm::vec3(0.0f);
        cascadeStates_[i].layerOffset = layerOffset;
        cascadeStates_[i].nextProbeToUpdate = 0;
        layerOffset += config_.cascades[i].gridSize;
    }

    if (!createProbeTexture()) return false;
    if (!createBuffers()) return false;

    if (config_.runtimeBaking) {
        if (!createBakePipeline()) return false;
        if (!createDescriptorSets()) return false;
    }

    SDL_Log("SkyProbeSystem initialized: %u total probes (~%zuMB)",
            config_.getTotalProbeCount(), config_.estimateMemoryMB());

    return true;
}

void SkyProbeSystem::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    bakeDescriptorSets_.clear();
    bakePipeline_.reset();
    bakePipelineLayout_.reset();
    bakeDescriptorSetLayout_.reset();
    sampler_.reset();

    if (probeTextureView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, probeTextureView_, nullptr);
        probeTextureView_ = VK_NULL_HANDLE;
    }
    if (probeTexture_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, probeTexture_, probeAllocation_);
        probeTexture_ = VK_NULL_HANDLE;
    }

    if (cascadeInfoBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, cascadeInfoBuffer_, cascadeInfoAllocation_);
        cascadeInfoBuffer_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
}

bool SkyProbeSystem::createProbeTexture() {
    // Calculate total texture dimensions
    // All cascades stacked in Z dimension
    uint32_t maxGridSize = 0;
    uint32_t totalDepth = 0;
    for (const auto& cascade : config_.cascades) {
        maxGridSize = std::max(maxGridSize, cascade.gridSize);
        totalDepth += cascade.gridSize;
    }

    // Format depends on storage type
    // SH1: RGBA16F for sh0 (visibility in alpha), separate textures or packed
    // BentNormal: RGBA16F (bent normal xyz + visibility w)
    // For simplicity, use RGBA16F for both - SH1 needs multiple samples
    vk::Format format = vk::Format::eR16G16B16A16Sfloat;

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e3D)
        .setFormat(format)
        .setExtent(vk::Extent3D{maxGridSize, maxGridSize, totalDepth})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled |
                  vk::ImageUsageFlagBits::eStorage |
                  vk::ImageUsageFlagBits::eTransferDst)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                       &probeTexture_, &probeAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky probe texture");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(probeTexture_)
        .setViewType(vk::ImageViewType::e3D)
        .setFormat(format)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device_, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                          nullptr, &probeTextureView_) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky probe texture view");
        return false;
    }

    // Create sampler with trilinear filtering
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMaxLod(0.0f);

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky probe sampler: %s", e.what());
        return false;
    }

    // Initialize to white (full visibility) using transfer
    {
        CommandScope cmdScope(device_, commandPool_, computeQueue_);
        if (!cmdScope.begin()) return false;

        Barriers::transitionImage(cmdScope.get(), probeTexture_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT);

        // Clear to white (full sky visibility)
        VkClearColorValue clearColor = {{1.0f, 1.0f, 1.0f, 1.0f}};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        vkCmdClearColorImage(cmdScope.get(), probeTexture_,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

        Barriers::transitionImage(cmdScope.get(), probeTexture_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        if (!cmdScope.end()) return false;
    }

    SDL_Log("Sky probe texture created: %ux%ux%u", maxGridSize, maxGridSize, totalDepth);
    return true;
}

bool SkyProbeSystem::createBuffers() {
    // Cascade info buffer
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(SkyProbeCascadeInfo) * SkyProbeConfig::NUM_CASCADES;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &cascadeInfoBuffer_, &cascadeInfoAllocation_, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cascade info buffer");
        return false;
    }

    // Initialize cascade info
    updateCascadeInfoBuffer();

    return true;
}

bool SkyProbeSystem::createBakePipeline() {
    // Descriptor set layout for baking:
    // 0: SDF atlas (sampler3D)
    // 1: SDF entries (SSBO)
    // 2: SDF instances (SSBO)
    // 3: Probe output (storage image)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device_)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: SDF atlas
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 1: SDF entries
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 2: SDF instances
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)          // 3: Probe output
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky probe bake descriptor layout");
        return false;
    }
    bakeDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(BakePushConstants));

    try {
        vk::DescriptorSetLayout layouts[] = { **bakeDescriptorSetLayout_ };
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(layouts)
            .setPushConstantRanges(pushConstantRange);
        bakePipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky probe bake pipeline layout: %s", e.what());
        return false;
    }

    std::string shaderFile = shaderPath_ + "/sky_probe_bake.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device_, shaderFile);
    if (!shaderModule) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Sky probe bake shader not found: %s (runtime baking disabled)", shaderFile.c_str());
        return true; // Non-fatal - can still use pre-baked probes
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**bakePipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr, &rawPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky probe bake pipeline");
        return false;
    }
    vkDestroyShaderModule(device_, *shaderModule, nullptr);
    bakePipeline_.emplace(*raiiDevice_, rawPipeline);

    SDL_Log("Sky probe bake pipeline created");
    return true;
}

bool SkyProbeSystem::createDescriptorSets() {
    if (!descriptorPool_ || !bakeDescriptorSetLayout_) {
        return true; // Baking not available
    }

    bakeDescriptorSets_ = descriptorPool_->allocate(**bakeDescriptorSetLayout_, framesInFlight_);
    if (bakeDescriptorSets_.size() != framesInFlight_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate sky probe bake descriptor sets");
        return false;
    }

    return true;
}

void SkyProbeSystem::updateCascades(const glm::vec3& cameraPos) {
    // Update each cascade's origin, snapping to grid
    for (uint32_t i = 0; i < SkyProbeConfig::NUM_CASCADES; i++) {
        float spacing = config_.cascades[i].spacing;
        float gridSize = static_cast<float>(config_.cascades[i].gridSize);
        float halfRange = (gridSize * spacing) * 0.5f;

        // Snap camera position to grid
        glm::vec3 snappedPos;
        snappedPos.x = std::floor((cameraPos.x - halfRange) / spacing) * spacing;
        snappedPos.y = std::floor((cameraPos.y - halfRange) / spacing) * spacing;
        snappedPos.z = std::floor((cameraPos.z - halfRange) / spacing) * spacing;

        cascadeStates_[i].origin = snappedPos;
    }

    updateCascadeInfoBuffer();
}

void SkyProbeSystem::updateCascadeInfoBuffer() {
    std::array<SkyProbeCascadeInfo, SkyProbeConfig::NUM_CASCADES> infos;

    uint32_t totalDepth = 0;
    for (uint32_t i = 0; i < SkyProbeConfig::NUM_CASCADES; i++) {
        totalDepth += config_.cascades[i].gridSize;
    }

    for (uint32_t i = 0; i < SkyProbeConfig::NUM_CASCADES; i++) {
        const auto& cascade = config_.cascades[i];
        const auto& state = cascadeStates_[i];

        float gridSize = static_cast<float>(cascade.gridSize);
        float invSize = 1.0f / (gridSize * cascade.spacing);

        infos[i].origin = glm::vec4(state.origin, cascade.spacing);
        infos[i].invSize = glm::vec4(invSize, invSize, invSize, config_.cascadeBlendRange);
        infos[i].params = glm::vec4(
            gridSize,
            static_cast<float>(state.layerOffset) / static_cast<float>(totalDepth),
            cascade.range,
            0.0f
        );
    }

    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, cascadeInfoAllocation_, &mapped) == VK_SUCCESS) {
        memcpy(mapped, infos.data(), sizeof(infos));
        vmaUnmapMemory(allocator_, cascadeInfoAllocation_);
    }
}

void SkyProbeSystem::recordBaking(VkCommandBuffer cmd, uint32_t frameIndex,
                                   VkImageView sdfAtlasView,
                                   VkBuffer sdfEntriesBuffer,
                                   VkBuffer sdfInstancesBuffer,
                                   uint32_t sdfInstanceCount,
                                   float sunZenith, float sunAzimuth) {
    if (!enabled_ || !bakePipeline_ || bakeDescriptorSets_.empty()) {
        return;
    }

    // Transition probe texture for compute write
    Barriers::transitionImage(cmd, probeTexture_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **bakePipeline_);

    // Update probes incrementally for each cascade
    for (uint32_t c = 0; c < SkyProbeConfig::NUM_CASCADES; c++) {
        auto& state = cascadeStates_[c];
        const auto& cascade = config_.cascades[c];

        uint32_t totalProbes = cascade.gridSize * cascade.gridSize * cascade.gridSize;
        uint32_t probesToUpdate = std::min(config_.probesPerFrame, totalProbes);

        // Update descriptor set
        DescriptorManager::SetWriter(device_, bakeDescriptorSets_[frameIndex])
            .writeImage(0, sdfAtlasView, **sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .writeBuffer(1, sdfEntriesBuffer, 0, VK_WHOLE_SIZE)
            .writeBuffer(2, sdfInstancesBuffer, 0, VK_WHOLE_SIZE)
            .writeStorageImage(3, probeTextureView_)
            .update();

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                 **bakePipelineLayout_, 0,
                                 vk::DescriptorSet(bakeDescriptorSets_[frameIndex]), {});

        BakePushConstants pc{};
        pc.cascadeOrigin = glm::vec4(state.origin, cascade.spacing);
        pc.cascadeParams = glm::vec4(
            static_cast<float>(cascade.gridSize),
            static_cast<float>(state.layerOffset),
            16.0f,  // numSamples for SDF tracing
            static_cast<float>(sdfInstanceCount)
        );
        pc.skyParams = glm::vec4(sunZenith, sunAzimuth, 2.0f, 0.0f);
        pc.probeStartIndex = state.nextProbeToUpdate;
        pc.probeCount = probesToUpdate;

        vkCmd.pushConstants<BakePushConstants>(
            **bakePipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pc);

        // Dispatch - one thread per probe
        uint32_t groups = (probesToUpdate + 63) / 64;
        vkCmd.dispatch(groups, 1, 1);

        // Advance probe index
        state.nextProbeToUpdate = (state.nextProbeToUpdate + probesToUpdate) % totalProbes;
    }

    // Transition back for fragment shader read
    Barriers::transitionImage(cmd, probeTexture_,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

bool SkyProbeSystem::loadBakedProbes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open baked probes: %s", path.c_str());
        return false;
    }

    // Read header
    uint32_t numCascades, format;
    file.read(reinterpret_cast<char*>(&numCascades), sizeof(numCascades));
    file.read(reinterpret_cast<char*>(&format), sizeof(format));

    if (numCascades != SkyProbeConfig::NUM_CASCADES) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Baked probe cascade count mismatch");
        return false;
    }

    // Calculate data size
    size_t bytesPerProbe = (config_.format == SkyProbeConfig::Format::SH1_RGB) ? 48 : 16;
    size_t totalBytes = config_.getTotalProbeCount() * bytesPerProbe;

    std::vector<char> data(totalBytes);
    file.read(data.data(), totalBytes);
    file.close();

    // Upload to GPU
    // TODO: Implement staging buffer upload

    SDL_Log("Loaded baked sky probes from: %s", path.c_str());
    return true;
}

bool SkyProbeSystem::saveBakedProbes(const std::string& path) {
    // TODO: Download from GPU and save
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "saveBakedProbes not yet implemented");
    return false;
}
