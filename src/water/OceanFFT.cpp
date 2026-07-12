#include "OceanFFT.h"
#include "DescriptorManager.h"
#include "SamplerFactory.h"
#include "VmaBufferFactory.h"
#include "CommandBufferUtils.h"
#include "core/ImageBuilder.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/vulkan/DescriptorSetLayoutBuilder.h"
#include "shaders/bindings.h"
#include <SDL3/SDL_log.h>
#include <cmath>
#include <cstring>
#include <array>
#include <vulkan/vulkan.hpp>

namespace {

// Memory barrier between dependent compute dispatches (all images stay in GENERAL)
void computeToComputeBarrier(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader,
                        {}, barrier, {}, {});
}

} // namespace

std::unique_ptr<OceanFFT> OceanFFT::create(const InitInfo& info) {
    auto ocean = std::make_unique<OceanFFT>(ConstructToken{});
    if (!ocean->initInternal(info)) {
        return nullptr;
    }
    return ocean;
}

OceanFFT::~OceanFFT() {
    cleanup();
}

bool OceanFFT::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    commandPool = info.commandPool;
    computeQueue = info.computeQueue;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    params = info.params;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: raiiDevice is required");
        return false;
    }

    if (params.resolution <= 0 || (params.resolution & (params.resolution - 1)) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: resolution must be a power of two (got %d)", params.resolution);
        return false;
    }

    // Set up cascades. Patch sizes match the water shader push constant
    // defaults (256m / 64m / 16m, see WaterSystem::setUseFFTOcean).
    if (info.useCascades) {
        cascadeCount = 3;
        cascades.resize(cascadeCount);

        // Cascade 0: Large swells (long wavelength)
        cascades[0].config = {
            params.oceanSize,           // 256m patch
            params.heightScale,         // Full height
            params.choppiness * 0.8f    // Slightly less choppy
        };

        // Cascade 1: Medium waves
        cascades[1].config = {
            params.oceanSize / 4.0f,    // 64m patch
            params.heightScale * 0.4f,  // Smaller waves
            params.choppiness
        };

        // Cascade 2: Small ripples (high frequency detail)
        cascades[2].config = {
            params.oceanSize / 16.0f,   // 16m patch
            params.heightScale * 0.15f, // Tiny ripples
            params.choppiness * 1.5f    // More choppy for detail
        };
    } else {
        cascadeCount = 1;
        cascades.resize(1);
        cascades[0].config = {
            params.oceanSize,
            params.heightScale,
            params.choppiness
        };
    }

    // Sampler for output textures (tiling ocean with anisotropic filtering)
    auto sampler = SamplerFactory::createSamplerLinearRepeatAnisotropic(*raiiDevice_, 16.0f, 0.0f);
    if (!sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create sampler");
        return false;
    }
    sampler_ = std::move(*sampler);

    if (!createComputePipelines()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create compute pipelines");
        return false;
    }

    for (int i = 0; i < cascadeCount; i++) {
        if (!createCascade(cascades[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create cascade %d", i);
            return false;
        }
    }

    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create descriptor sets");
        return false;
    }

    // Move every simulation image into GENERAL layout once. They stay there
    // permanently (storage read/write in compute, sampled with GENERAL-layout
    // descriptors elsewhere), so per-frame work needs only memory barriers.
    if (!transitionImagesToGeneral()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to transition images");
        return false;
    }

    SDL_Log("OceanFFT: Initialized with %d cascades, resolution %d", cascadeCount, params.resolution);
    return true;
}

void OceanFFT::cleanup() {
    if (!raiiDevice_) return;

    raiiDevice_->waitIdle();

    cascades.clear();
    descriptorPool_.reset();
    spectrumUBOs.clear();
    spectrumUBOMapped.clear();

    spectrumPipeline_.reset();
    spectrumPipelineLayout_.reset();
    spectrumDescLayout_.reset();

    timeEvolutionPipeline_.reset();
    timeEvolutionPipelineLayout_.reset();
    timeEvolutionDescLayout_.reset();

    fftPipeline_.reset();
    fftPipelineLayout_.reset();
    fftDescLayout_.reset();

    displacementPipeline_.reset();
    displacementPipelineLayout_.reset();
    displacementDescLayout_.reset();

    sampler_.reset();

    device = VK_NULL_HANDLE;
    raiiDevice_ = nullptr;
}

bool OceanFFT::createImage(ManagedImage& image, std::optional<vk::raii::ImageView>& view,
                           VkFormat format, VkImageUsageFlags usage) {
    uint32_t res = static_cast<uint32_t>(params.resolution);
    return ImageBuilder(allocator)
        .setExtent(res, res)
        .setFormat(format)
        .setUsage(usage)
        .setGpuOnly()
        .build(*raiiDevice_, image, view);
}

bool OceanFFT::createCascade(Cascade& cascade) {
    constexpr VkImageUsageFlags kStorageSampled = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // Spectrum textures (RGBA32F for complex H0 + conjugate)
    if (!createImage(cascade.h0Spectrum, cascade.h0SpectrumView,
                     VK_FORMAT_R32G32B32A32_SFLOAT, kStorageSampled)) {
        return false;
    }

    // Angular frequency (R32F)
    if (!createImage(cascade.omegaSpectrum, cascade.omegaSpectrumView,
                     VK_FORMAT_R32_SFLOAT, kStorageSampled)) {
        return false;
    }

    // Time-evolved spectra and FFT scratch (RG32F complex). Each component has
    // its own scratch image so the three IFFTs cannot clobber each other.
    for (int c = 0; c < kComponents; c++) {
        if (!createImage(cascade.hkt[c], cascade.hktView[c],
                         VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT)) {
            return false;
        }
        if (!createImage(cascade.scratch[c], cascade.scratchView[c],
                         VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT)) {
            return false;
        }
    }

    // Output textures
    // Displacement: RGBA16F (xyz = displacement, w = jacobian)
    if (!createImage(cascade.displacementMap, cascade.displacementMapView,
                     VK_FORMAT_R16G16B16A16_SFLOAT, kStorageSampled)) {
        return false;
    }

    // Normal: RGBA16F (xyz = normal encoded [0,1])
    if (!createImage(cascade.normalMap, cascade.normalMapView,
                     VK_FORMAT_R16G16B16A16_SFLOAT, kStorageSampled)) {
        return false;
    }

    // Foam: R16F
    if (!createImage(cascade.foamMap, cascade.foamMapView,
                     VK_FORMAT_R16_SFLOAT, kStorageSampled)) {
        return false;
    }

    return true;
}

bool OceanFFT::createComputePipelines() {
    // =========================================================================
    // Spectrum Generation Pipeline
    // =========================================================================
    {
        if (!DescriptorSetLayoutBuilder()
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_SPECTRUM_H0, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_SPECTRUM_OMEGA, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::uniformBuffer(Bindings::OCEAN_SPECTRUM_PARAMS, vk::ShaderStageFlagBits::eCompute))
                .buildInto(*raiiDevice_, spectrumDescLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create spectrum descriptor layout");
            return false;
        }

        if (!PipelineLayoutBuilder(*raiiDevice_)
                .addDescriptorSetLayout(**spectrumDescLayout_)
                .buildInto(spectrumPipelineLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create spectrum pipeline layout");
            return false;
        }

        if (!ComputePipelineBuilder(*raiiDevice_)
                .setShader(shaderPath + "/ocean_spectrum.comp.spv")
                .setPipelineLayout(**spectrumPipelineLayout_)
                .buildInto(spectrumPipeline_)) {
            return false;
        }
    }

    // =========================================================================
    // Time Evolution Pipeline
    // =========================================================================
    {
        if (!DescriptorSetLayoutBuilder()
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_HKT_DY, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_HKT_DX, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_HKT_DZ, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::combinedImageSampler(Bindings::OCEAN_H0_INPUT, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::combinedImageSampler(Bindings::OCEAN_OMEGA_INPUT, vk::ShaderStageFlagBits::eCompute))
                .buildInto(*raiiDevice_, timeEvolutionDescLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create time evolution descriptor layout");
            return false;
        }

        if (!PipelineLayoutBuilder(*raiiDevice_)
                .addDescriptorSetLayout(**timeEvolutionDescLayout_)
                .addPushConstantRange<OceanTimeEvolutionPushConstants>(vk::ShaderStageFlagBits::eCompute)
                .buildInto(timeEvolutionPipelineLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create time evolution pipeline layout");
            return false;
        }

        if (!ComputePipelineBuilder(*raiiDevice_)
                .setShader(shaderPath + "/ocean_time_evolution.comp.spv")
                .setPipelineLayout(**timeEvolutionPipelineLayout_)
                .buildInto(timeEvolutionPipeline_)) {
            return false;
        }
    }

    // =========================================================================
    // FFT Pipeline
    // =========================================================================
    {
        if (!DescriptorSetLayoutBuilder()
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_FFT_INPUT, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_FFT_OUTPUT, vk::ShaderStageFlagBits::eCompute))
                .buildInto(*raiiDevice_, fftDescLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create FFT descriptor layout");
            return false;
        }

        if (!PipelineLayoutBuilder(*raiiDevice_)
                .addDescriptorSetLayout(**fftDescLayout_)
                .addPushConstantRange<OceanFFTPushConstants>(vk::ShaderStageFlagBits::eCompute)
                .buildInto(fftPipelineLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create FFT pipeline layout");
            return false;
        }

        if (!ComputePipelineBuilder(*raiiDevice_)
                .setShader(shaderPath + "/ocean_fft.comp.spv")
                .setPipelineLayout(**fftPipelineLayout_)
                .buildInto(fftPipeline_)) {
            return false;
        }
    }

    // =========================================================================
    // Displacement Generation Pipeline
    // =========================================================================
    {
        if (!DescriptorSetLayoutBuilder()
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_DISP_DY, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_DISP_DX, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_DISP_DZ, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_DISP_OUTPUT, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_NORMAL_OUTPUT, vk::ShaderStageFlagBits::eCompute))
                .addBinding(BindingBuilder::storageImage(Bindings::OCEAN_FOAM_OUTPUT, vk::ShaderStageFlagBits::eCompute))
                .buildInto(*raiiDevice_, displacementDescLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create displacement descriptor layout");
            return false;
        }

        if (!PipelineLayoutBuilder(*raiiDevice_)
                .addDescriptorSetLayout(**displacementDescLayout_)
                .addPushConstantRange<OceanDisplacementPushConstants>(vk::ShaderStageFlagBits::eCompute)
                .buildInto(displacementPipelineLayout_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create displacement pipeline layout");
            return false;
        }

        if (!ComputePipelineBuilder(*raiiDevice_)
                .setShader(shaderPath + "/ocean_displacement.comp.spv")
                .setPipelineLayout(**displacementPipelineLayout_)
                .buildInto(displacementPipeline_)) {
            return false;
        }
    }

    return true;
}

bool OceanFFT::createDescriptorSets() {
    const uint32_t fftSetsPerCascade = kComponents * 2;
    const uint32_t totalSets = static_cast<uint32_t>(cascadeCount) * (3 + fftSetsPerCascade);

    std::array<vk::DescriptorPoolSize, 3> poolSizes = {
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(totalSets * 6),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(static_cast<uint32_t>(cascadeCount) * 2),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(static_cast<uint32_t>(cascadeCount))
    };

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setMaxSets(totalSets)
        .setPoolSizes(poolSizes);

    try {
        descriptorPool_.emplace(*raiiDevice_, poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to create descriptor pool: %s", e.what());
        return false;
    }

    // UBOs for spectrum parameters (one per cascade; only rewritten on regen)
    spectrumUBOs.resize(cascadeCount);
    spectrumUBOMapped.resize(cascadeCount);
    for (int i = 0; i < cascadeCount; i++) {
        if (!VmaBufferFactory::createUniformBuffer(allocator, sizeof(SpectrumUBO), spectrumUBOs[i])) {
            return false;
        }
        spectrumUBOMapped[i] = spectrumUBOs[i].map();
        if (!spectrumUBOMapped[i]) {
            return false;
        }
    }

    vk::Device vkDevice(device);
    auto allocateOne = [&](vk::DescriptorSetLayout layout, VkDescriptorSet& outSet) {
        auto allocInfo = vk::DescriptorSetAllocateInfo{}
            .setDescriptorPool(**descriptorPool_)
            .setSetLayouts(layout);
        try {
            outSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
            return true;
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OceanFFT: Failed to allocate descriptor set: %s", e.what());
            return false;
        }
    };

    spectrumDescSets.resize(cascadeCount);
    timeEvolutionDescSets.resize(cascadeCount);
    displacementDescSets.resize(cascadeCount);
    fftDescSets.resize(cascadeCount * fftSetsPerCascade);

    for (int i = 0; i < cascadeCount; i++) {
        Cascade& cascade = cascades[i];

        // Spectrum descriptor set
        if (!allocateOne(**spectrumDescLayout_, spectrumDescSets[i])) return false;
        DescriptorManager::SetWriter(device, spectrumDescSets[i])
            .writeStorageImage(Bindings::OCEAN_SPECTRUM_H0, **cascade.h0SpectrumView)
            .writeStorageImage(Bindings::OCEAN_SPECTRUM_OMEGA, **cascade.omegaSpectrumView)
            .writeBuffer(Bindings::OCEAN_SPECTRUM_PARAMS, spectrumUBOs[i].get(), 0, sizeof(SpectrumUBO))
            .update();

        // Time evolution descriptor set (samples h0/omega which stay in GENERAL)
        if (!allocateOne(**timeEvolutionDescLayout_, timeEvolutionDescSets[i])) return false;
        DescriptorManager::SetWriter(device, timeEvolutionDescSets[i])
            .writeStorageImage(Bindings::OCEAN_HKT_DY, **cascade.hktView[0])
            .writeStorageImage(Bindings::OCEAN_HKT_DX, **cascade.hktView[1])
            .writeStorageImage(Bindings::OCEAN_HKT_DZ, **cascade.hktView[2])
            .writeImage(Bindings::OCEAN_H0_INPUT, **cascade.h0SpectrumView, **sampler_, VK_IMAGE_LAYOUT_GENERAL)
            .writeImage(Bindings::OCEAN_OMEGA_INPUT, **cascade.omegaSpectrumView, **sampler_, VK_IMAGE_LAYOUT_GENERAL)
            .update();

        // Displacement descriptor set - the IFFT result for each component is
        // back in its hkt image (even number of ping-pong passes).
        if (!allocateOne(**displacementDescLayout_, displacementDescSets[i])) return false;
        DescriptorManager::SetWriter(device, displacementDescSets[i])
            .writeStorageImage(Bindings::OCEAN_DISP_DY, **cascade.hktView[0])
            .writeStorageImage(Bindings::OCEAN_DISP_DX, **cascade.hktView[1])
            .writeStorageImage(Bindings::OCEAN_DISP_DZ, **cascade.hktView[2])
            .writeStorageImage(Bindings::OCEAN_DISP_OUTPUT, **cascade.displacementMapView)
            .writeStorageImage(Bindings::OCEAN_NORMAL_OUTPUT, **cascade.normalMapView)
            .writeStorageImage(Bindings::OCEAN_FOAM_OUTPUT, **cascade.foamMapView)
            .update();

        // FFT descriptor sets: hkt -> scratch (pingpong 0) and scratch -> hkt
        // (pingpong 1) for each component. Written once, never rewritten.
        for (int c = 0; c < kComponents; c++) {
            for (int p = 0; p < 2; p++) {
                VkDescriptorSet& set = fftDescSets[(i * kComponents + c) * 2 + p];
                if (!allocateOne(**fftDescLayout_, set)) return false;
                VkImageView inputView = (p == 0) ? **cascade.hktView[c] : **cascade.scratchView[c];
                VkImageView outputView = (p == 0) ? **cascade.scratchView[c] : **cascade.hktView[c];
                DescriptorManager::SetWriter(device, set)
                    .writeStorageImage(Bindings::OCEAN_FFT_INPUT, inputView)
                    .writeStorageImage(Bindings::OCEAN_FFT_OUTPUT, outputView)
                    .update();
            }
        }
    }

    return true;
}

bool OceanFFT::transitionImagesToGeneral() {
    CommandScope cmd{vk::Device(device), vk::CommandPool(commandPool), vk::Queue(computeQueue)};
    if (!cmd.begin()) return false;

    auto transition = [&](const ManagedImage& image) {
        BarrierHelpers::transitionImageLayout(cmd.get(), image.get(),
            vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eVertexShader |
                vk::PipelineStageFlagBits::eTessellationEvaluationShader | vk::PipelineStageFlagBits::eFragmentShader,
            vk::AccessFlags{},
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    };

    for (Cascade& cascade : cascades) {
        transition(cascade.h0Spectrum);
        transition(cascade.omegaSpectrum);
        for (int c = 0; c < kComponents; c++) {
            transition(cascade.hkt[c]);
            transition(cascade.scratch[c]);
        }
        transition(cascade.displacementMap);
        transition(cascade.normalMap);
        transition(cascade.foamMap);
    }

    return cmd.end();
}

void OceanFFT::recordCompute(VkCommandBuffer cmd, float time) {
    if (!enabled) return;

    vk::CommandBuffer vkCmd(cmd);

    // Execution dependency against last frame's sampling of the output maps
    // (vertex/tess-eval/fragment) before this frame overwrites them.
    vkCmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eTessellationEvaluationShader |
            vk::PipelineStageFlagBits::eFragmentShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, {}, {});

    if (spectrumDirty) {
        for (int i = 0; i < cascadeCount; i++) {
            recordSpectrumGeneration(cmd, i);
        }
        computeToComputeBarrier(vkCmd);
        spectrumDirty = false;
    }

    for (int i = 0; i < cascadeCount; i++) {
        recordTimeEvolution(cmd, i, time);
        computeToComputeBarrier(vkCmd);

        for (int c = 0; c < kComponents; c++) {
            recordFFT(cmd, i, c);
        }

        recordDisplacementGeneration(cmd, i);
    }

    // Make the output maps visible to the water vertex/tessellation/fragment shaders.
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    vkCmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eTessellationEvaluationShader |
            vk::PipelineStageFlagBits::eFragmentShader,
        {}, barrier, {}, {});
}

void OceanFFT::recordSpectrumGeneration(VkCommandBuffer cmd, int cascadeIndex) {
    // Update UBO with current parameters. The UBO is only read by this
    // dispatch; regeneration is rare (GUI parameter changes), so a single
    // buffer per cascade is sufficient.
    SpectrumUBO ubo{};
    ubo.resolution = params.resolution;
    ubo.oceanSize = cascades[cascadeIndex].config.oceanSize;
    ubo.windSpeed = params.windSpeed;
    ubo.windDirection = glm::normalize(params.windDirection);
    ubo.amplitude = params.amplitude;
    ubo.gravity = params.gravity;
    ubo.smallWaveCutoff = params.smallWaveCutoff;
    ubo.alignment = params.alignment;
    ubo.seed = static_cast<uint32_t>(cascadeIndex * 12345 + 67890);
    std::memcpy(spectrumUBOMapped[cascadeIndex], &ubo, sizeof(SpectrumUBO));

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **spectrumPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **spectrumPipelineLayout_,
                             0, vk::DescriptorSet(spectrumDescSets[cascadeIndex]), {});

    uint32_t groupCount = (params.resolution + 15) / 16;
    vkCmd.dispatch(groupCount, groupCount, 1);
}

void OceanFFT::recordTimeEvolution(VkCommandBuffer cmd, int cascadeIndex, float time) {
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **timeEvolutionPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **timeEvolutionPipelineLayout_,
                             0, vk::DescriptorSet(timeEvolutionDescSets[cascadeIndex]), {});

    OceanTimeEvolutionPushConstants pushConstants = {
        time,
        params.resolution,
        cascades[cascadeIndex].config.oceanSize,
        cascades[cascadeIndex].config.choppiness
    };
    vkCmd.pushConstants<OceanTimeEvolutionPushConstants>(
        **timeEvolutionPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    uint32_t groupCount = (params.resolution + 15) / 16;
    vkCmd.dispatch(groupCount, groupCount, 1);
}

void OceanFFT::recordFFT(VkCommandBuffer cmd, int cascadeIndex, int component) {
    // 2D inverse FFT: log2(N) horizontal butterfly passes, then log2(N)
    // vertical ones. Each pass ping-pongs between the component's hkt image
    // and its private scratch image; 2*log2(N) is even, so the final result
    // lands back in the hkt image (which the displacement pass reads).
    int numStages = 0;
    for (int n = params.resolution; n > 1; n >>= 1) numStages++;

    uint32_t groupCount = (params.resolution + 15) / 16;

    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **fftPipeline_);

    int pingpong = 0;  // 0 = hkt -> scratch, 1 = scratch -> hkt
    for (int direction = 0; direction < 2; direction++) {
        for (int stage = 0; stage < numStages; stage++) {
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **fftPipelineLayout_,
                                     0, vk::DescriptorSet(fftSet(cascadeIndex, component, pingpong)), {});

            OceanFFTPushConstants pushData = {stage, direction, params.resolution, 1};
            vkCmd.pushConstants<OceanFFTPushConstants>(
                **fftPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pushData);

            vkCmd.dispatch(groupCount, groupCount, 1);
            computeToComputeBarrier(vkCmd);

            pingpong = 1 - pingpong;
        }
    }
}

void OceanFFT::recordDisplacementGeneration(VkCommandBuffer cmd, int cascadeIndex) {
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **displacementPipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **displacementPipelineLayout_,
                             0, vk::DescriptorSet(displacementDescSets[cascadeIndex]), {});

    OceanDisplacementPushConstants pushConstants = {
        params.resolution,
        cascades[cascadeIndex].config.oceanSize,
        cascades[cascadeIndex].config.heightScale,
        params.foamThreshold,
        0.9f,  // foam decay
        params.normalStrength
    };
    vkCmd.pushConstants<OceanDisplacementPushConstants>(
        **displacementPipelineLayout_, vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    uint32_t groupCount = (params.resolution + 15) / 16;
    vkCmd.dispatch(groupCount, groupCount, 1);
}

VkImageView OceanFFT::getDisplacementView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].displacementMapView ? **cascades[cascade].displacementMapView : VK_NULL_HANDLE;
}

VkImageView OceanFFT::getNormalView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].normalMapView ? **cascades[cascade].normalMapView : VK_NULL_HANDLE;
}

VkImageView OceanFFT::getFoamView(int cascade) const {
    if (cascade < 0 || cascade >= cascadeCount) return VK_NULL_HANDLE;
    return cascades[cascade].foamMapView ? **cascades[cascade].foamMapView : VK_NULL_HANDLE;
}

void OceanFFT::setParams(const OceanParams& newParams) {
    bool needsRegen = (newParams.resolution != params.resolution ||
                       newParams.oceanSize != params.oceanSize ||
                       newParams.windSpeed != params.windSpeed ||
                       newParams.windDirection != params.windDirection ||
                       newParams.amplitude != params.amplitude ||
                       newParams.smallWaveCutoff != params.smallWaveCutoff ||
                       newParams.alignment != params.alignment);

    params = newParams;

    if (needsRegen) {
        spectrumDirty = true;
    }
}

void OceanFFT::setWindSpeed(float speed) {
    if (speed != params.windSpeed) {
        params.windSpeed = speed;
        spectrumDirty = true;
    }
}

void OceanFFT::setWindDirection(const glm::vec2& dir) {
    glm::vec2 normalized = glm::normalize(dir);
    if (normalized != params.windDirection) {
        params.windDirection = normalized;
        spectrumDirty = true;
    }
}

void OceanFFT::setAmplitude(float amp) {
    if (amp != params.amplitude) {
        params.amplitude = amp;
        spectrumDirty = true;
    }
}

void OceanFFT::setChoppiness(float chop) {
    params.choppiness = chop;
    if (cascadeCount >= 1) cascades[0].config.choppiness = chop * 0.8f;
    if (cascadeCount >= 2) cascades[1].config.choppiness = chop;
    if (cascadeCount >= 3) cascades[2].config.choppiness = chop * 1.5f;
}

void OceanFFT::setHeightScale(float scale) {
    params.heightScale = scale;
    if (cascadeCount >= 1) cascades[0].config.heightScale = scale;
    if (cascadeCount >= 2) cascades[1].config.heightScale = scale * 0.4f;
    if (cascadeCount >= 3) cascades[2].config.heightScale = scale * 0.15f;
}

void OceanFFT::setFoamThreshold(float threshold) {
    params.foamThreshold = threshold;
}
