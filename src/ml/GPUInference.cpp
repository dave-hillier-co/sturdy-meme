#include "GPUInference.h"
#include "../core/ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <array>

namespace ml {

std::unique_ptr<GPUInference> GPUInference::create(const vk::raii::Device& device,
                                                   VmaAllocator allocator,
                                                   const Config& config) {
    auto inference = std::make_unique<GPUInference>(ConstructToken{});
    if (!inference->initInternal(device, allocator, config)) {
        return nullptr;
    }
    return inference;
}

bool GPUInference::initInternal(const vk::raii::Device& device, VmaAllocator allocator, const Config& cfg) {
    device_ = *device;
    allocator_ = allocator;
    config_ = cfg;

    // Create GPU buffers
    size_t latentBufSize = cfg.maxNPCs * cfg.latentDim * sizeof(float);
    size_t obsBufSize = cfg.maxNPCs * cfg.obsDim * sizeof(float);
    size_t actionBufSize = cfg.maxNPCs * cfg.actionDim * sizeof(float);

    if (!createBuffer(latentBuffer_, latentBufSize,
                      vk::BufferUsageFlagBits::eStorageBuffer,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    if (!createBuffer(obsBuffer_, obsBufSize,
                      vk::BufferUsageFlagBits::eStorageBuffer,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    if (!createBuffer(actionBuffer_, actionBufSize,
                      vk::BufferUsageFlagBits::eStorageBuffer,
                      VMA_MEMORY_USAGE_GPU_TO_CPU)) return false;

    // Descriptor set layout: 5 storage buffers
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < 5; ++i) {
        bindings[i] = vk::DescriptorSetLayoutBinding{}
            .setBinding(i)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    }

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);
    try {
        descriptorSetLayout_.emplace(device, layoutInfo);
    } catch (const vk::SystemError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create descriptor set layout");
        return false;
    }

    // Descriptor pool
    auto poolSize = vk::DescriptorPoolSize{}
        .setType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(5);

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setMaxSets(1)
        .setPoolSizes(poolSize);
    try {
        descriptorPool_.emplace(device, poolInfo);
    } catch (const vk::SystemError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create descriptor pool");
        return false;
    }

    if (!createDescriptorSet()) return false;

    // Push constant range
    auto pushRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(InferencePushConstants));

    // Pipeline layout
    vk::DescriptorSetLayout setLayout = **descriptorSetLayout_;
    auto plInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(setLayout)
        .setPushConstantRanges(pushRange);
    try {
        pipelineLayout_.emplace(device, plInfo);
    } catch (const vk::SystemError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create pipeline layout");
        return false;
    }

    // Load compute shader (RAII module: destroyed at end of scope, after pipeline creation)
    auto shaderModule = ShaderLoader::loadShaderModule(device, cfg.shaderPath);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GPUInference: failed to load shader %s", cfg.shaderPath.c_str());
        return false;
    }

    // Specialization constants
    struct SpecData {
        uint32_t numNPCs;
        uint32_t latentDim;
        uint32_t obsDim;
        uint32_t actionDim;
        uint32_t maxHidden;
    } specData = {cfg.maxNPCs, cfg.latentDim, cfg.obsDim, cfg.actionDim, cfg.maxHiddenSize};

    std::array<vk::SpecializationMapEntry, 5> specEntries{};
    for (uint32_t i = 0; i < 5; ++i) {
        specEntries[i] = vk::SpecializationMapEntry{}
            .setConstantID(i)
            .setOffset(i * sizeof(uint32_t))
            .setSize(sizeof(uint32_t));
    }

    auto specInfo = vk::SpecializationInfo{}
        .setMapEntries(specEntries)
        .setDataSize(sizeof(specData))
        .setPData(&specData);

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(**shaderModule)
        .setPName("main")
        .setPSpecializationInfo(&specInfo);

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**pipelineLayout_);

    try {
        pipeline_.emplace(device, nullptr, pipelineInfo);
    } catch (const vk::SystemError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create compute pipeline");
        return false;
    }

    SDL_Log("GPUInference: initialized (maxNPCs=%u, latent=%u, obs=%u, action=%u)",
            cfg.maxNPCs, cfg.latentDim, cfg.obsDim, cfg.actionDim);
    return true;
}

bool GPUInference::uploadWeights(const calm::LowLevelController& llc) {
    std::vector<float> packedWeights;
    std::vector<GPULayerMeta> layerMetas;
    if (!packWeights(llc, packedWeights, layerMetas)) return false;

    size_t weightSize = packedWeights.size() * sizeof(float);
    if (!createBuffer(weightBuffer_, weightSize,
                      vk::BufferUsageFlagBits::eStorageBuffer,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    uploadToBuffer(weightBuffer_, packedWeights.data(), weightSize);

    // Pack layer metadata as flat uint32 array (5 per layer)
    std::vector<uint32_t> metaFlat;
    for (const auto& m : layerMetas) {
        metaFlat.push_back(m.weightOffset);
        metaFlat.push_back(m.biasOffset);
        metaFlat.push_back(m.inFeatures);
        metaFlat.push_back(m.outFeatures);
        metaFlat.push_back(m.activation);
    }
    size_t metaSize = metaFlat.size() * sizeof(uint32_t);
    if (!createBuffer(layerMetaBuffer_, metaSize,
                      vk::BufferUsageFlagBits::eStorageBuffer,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    uploadToBuffer(layerMetaBuffer_, metaFlat.data(), metaSize);

    updateDescriptorSet();

    SDL_Log("GPUInference: uploaded weights (%zu layers, %zu floats)",
            layerMetas.size(), packedWeights.size());
    return true;
}

void GPUInference::uploadInputs(const std::vector<float>& latents,
                                 const std::vector<float>& observations,
                                 uint32_t npcCount) {
    uploadToBuffer(latentBuffer_, latents.data(),
                   npcCount * config_.latentDim * sizeof(float));
    uploadToBuffer(obsBuffer_, observations.data(),
                   npcCount * config_.obsDim * sizeof(float));
}

void GPUInference::recordDispatch(vk::CommandBuffer cmd, uint32_t npcCount) {
    if (!pipeline_) return;

    vk::CommandBuffer vkCmd(cmd);

    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **pipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             **pipelineLayout_, 0, descriptorSet_, {});
    vkCmd.pushConstants(**pipelineLayout_, vk::ShaderStageFlagBits::eCompute,
                        0, sizeof(pushConstants_), &pushConstants_);

    vkCmd.dispatch(npcCount, 1, 1);

    // Memory barrier: compute writes -> host reads
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eHostRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eHost,
                          {}, barrier, {}, {});
}

void GPUInference::readBackActions(std::vector<float>& actions, uint32_t npcCount) {
    size_t totalFloats = npcCount * config_.actionDim;
    actions.resize(totalFloats);
    readFromBuffer(actionBuffer_, actions.data(), totalFloats * sizeof(float));
}

// --- Buffer helpers ---

bool GPUInference::createBuffer(GPUBuffer& buf, size_t size,
                                 vk::BufferUsageFlags usage,
                                 VmaMemoryUsage memUsage) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(usage);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // Replaces (and frees) any previous buffer
    VmaBuffer created;
    if (!VmaBuffer::create(allocator_, bufferInfo, allocInfo, created)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GPUInference: failed to create buffer (size=%zu)", size);
        return false;
    }
    buf.buffer = std::move(created);
    buf.size = size;
    return true;
}

void GPUInference::uploadToBuffer(GPUBuffer& buf, const void* data, size_t size) {
    VmaAllocation allocation = buf.buffer.getAllocation();
    void* mapped = nullptr;
    vmaMapMemory(allocator_, allocation, &mapped);
    std::memcpy(mapped, data, size);
    vmaUnmapMemory(allocator_, allocation);
    vmaFlushAllocation(allocator_, allocation, 0, size);
}

void GPUInference::readFromBuffer(const GPUBuffer& buf, void* data, size_t size) {
    VmaAllocation allocation = buf.buffer.getAllocation();
    vmaInvalidateAllocation(allocator_, allocation, 0, size);
    void* mapped = nullptr;
    vmaMapMemory(allocator_, allocation, &mapped);
    std::memcpy(data, mapped, size);
    vmaUnmapMemory(allocator_, allocation);
}

bool GPUInference::createDescriptorSet() {
    vk::DescriptorSetLayout setLayout = **descriptorSetLayout_;
    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(**descriptorPool_)
        .setSetLayouts(setLayout);

    // Pool-owned: released when descriptorPool_ is destroyed
    vk::DescriptorSet set;
    vk::Result result = device_.allocateDescriptorSets(&allocInfo, &set);
    descriptorSet_ = set;
    return result == vk::Result::eSuccess;
}

void GPUInference::updateDescriptorSet() {
    struct BufInfo { GPUBuffer* buf; uint32_t binding; };
    BufInfo buffers[] = {
        {&weightBuffer_, 0}, {&layerMetaBuffer_, 1},
        {&latentBuffer_, 2}, {&obsBuffer_, 3}, {&actionBuffer_, 4},
    };

    std::vector<vk::WriteDescriptorSet> writes;
    std::vector<vk::DescriptorBufferInfo> bufInfos(5);

    for (int i = 0; i < 5; ++i) {
        auto* b = buffers[i].buf;
        if (!b->buffer) continue;

        bufInfos[i] = vk::DescriptorBufferInfo{}
            .setBuffer(vk::Buffer(b->buffer.get()))
            .setOffset(0)
            .setRange(b->size);

        writes.push_back(vk::WriteDescriptorSet{}
            .setDstSet(descriptorSet_)
            .setDstBinding(buffers[i].binding)
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setPBufferInfo(&bufInfos[i]));
    }

    if (!writes.empty()) {
        device_.updateDescriptorSets(writes, {});
    }
}

bool GPUInference::packWeights(const calm::LowLevelController& llc,
                                std::vector<float>& packedWeights,
                                std::vector<GPULayerMeta>& layerMetas) {
    packedWeights.clear();
    layerMetas.clear();

    const auto& scNet = llc.network();
    const auto& styleMLP = scNet.styleMLP();
    const auto& mainMLP = scNet.mainMLP();

    // Convert ml::Activation to GPU activation code (0=None, 1=ReLU, 2=Tanh)
    auto activationToGPU = [](Activation act) -> uint32_t {
        switch (act) {
            case Activation::ReLU: return 1;
            case Activation::Tanh: return 2;
            default: return 0;
        }
    };

    auto packNetwork = [&](const MLPNetwork& net) {
        for (size_t i = 0; i < net.numLayers(); ++i) {
            const auto& layer = net.layer(i);
            GPULayerMeta meta{};
            meta.weightOffset = static_cast<uint32_t>(packedWeights.size());
            const Tensor& w = layer.weights;
            for (size_t j = 0; j < w.size(); ++j) packedWeights.push_back(w[j]);
            meta.biasOffset = static_cast<uint32_t>(packedWeights.size());
            const Tensor& b = layer.bias;
            for (size_t j = 0; j < b.size(); ++j) packedWeights.push_back(b[j]);
            meta.inFeatures = static_cast<uint32_t>(layer.inFeatures);
            meta.outFeatures = static_cast<uint32_t>(layer.outFeatures);
            meta.activation = activationToGPU(net.activation(i));
            layerMetas.push_back(meta);
        }
    };

    packNetwork(styleMLP);
    uint32_t styleLayerCount = static_cast<uint32_t>(styleMLP.numLayers());

    packNetwork(mainMLP);
    const auto& muHead = llc.muHead();
    if (muHead.numLayers() > 0) packNetwork(muHead);
    uint32_t mainLayerCount = static_cast<uint32_t>(mainMLP.numLayers() + muHead.numLayers());

    pushConstants_.numLayers = static_cast<uint32_t>(layerMetas.size());
    pushConstants_.styleLayerCount = styleLayerCount;
    pushConstants_.mainLayerCount = mainLayerCount;
    pushConstants_.styleDim = (styleLayerCount > 0)
        ? layerMetas[styleLayerCount - 1].outFeatures : 0;

    return true;
}

} // namespace ml
