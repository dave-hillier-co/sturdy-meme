#include "VolumetricSnowSystem.h"
#include "SamplerFactory.h"
#include "DescriptorManager.h"
#include "core/vulkan/VmaBufferFactory.h"
#include "core/vulkan/DescriptorSetLayoutBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <array>
#include <vector>

std::unique_ptr<VolumetricSnowSystem> VolumetricSnowSystem::create(const InitInfo& info) {
    auto system = std::make_unique<VolumetricSnowSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

bool VolumetricSnowSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RAII device not available for cascade sampler");
        return false;
    }

    // Compute-only system: buffers, layout, pipeline, then descriptor sets
    if (!createBuffers()) return false;
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createComputePipeline()) return false;
    if (!createDescriptorSets()) return false;

    return true;
}

bool VolumetricSnowSystem::createBuffers() {
    vk::DeviceSize uniformBufferSize = sizeof(VolumetricSnowUniforms);
    vk::DeviceSize interactionBufferSize = sizeof(VolumetricSnowInteraction) * MAX_INTERACTIONS;

    uniformBuffers_.resize(getFramesInFlight());
    uniformMapped_.resize(getFramesInFlight(), nullptr);
    for (uint32_t i = 0; i < getFramesInFlight(); ++i) {
        if (!VmaBufferFactory::createUniformBuffer(getAllocator(), uniformBufferSize, uniformBuffers_[i])) {
            SDL_Log("Failed to create volumetric snow uniform buffers");
            return false;
        }
        uniformMapped_[i] = uniformBuffers_[i].map();
        if (!uniformMapped_[i]) {
            SDL_Log("Failed to create volumetric snow uniform buffers");
            return false;
        }
    }

    interactionBuffers_.resize(getFramesInFlight());
    interactionMapped_.resize(getFramesInFlight(), nullptr);
    for (uint32_t i = 0; i < getFramesInFlight(); ++i) {
        if (!BufferBuilder(getAllocator())
                 .setSize(interactionBufferSize)
                 .asStorage()
                 .hostVisible()
                 .build(interactionBuffers_[i])) {
            SDL_Log("Failed to create volumetric snow interaction buffers");
            return false;
        }
        interactionMapped_[i] = interactionBuffers_[i].map();
        if (!interactionMapped_[i]) {
            SDL_Log("Failed to create volumetric snow interaction buffers");
            return false;
        }
    }

    return createCascadeTextures();
}

bool VolumetricSnowSystem::createCascadeTextures() {
    // Create cascade textures (R16F height in meters)
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setExtent(vk::Extent3D{SNOW_CASCADE_SIZE, SNOW_CASCADE_SIZE, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setFormat(vk::Format::eR16Sfloat)  // R16F for height value
            .setTiling(vk::ImageTiling::eOptimal)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setSamples(vk::SampleCountFlagBits::e1);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (!ManagedImage::create(getAllocator(), imageInfo, allocInfo, cascadeImages_[i])) {
            SDL_Log("Failed to create volumetric snow cascade %d image", i);
            return false;
        }

        // Create image view
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(cascadeImages_[i].get())
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        try {
            cascadeViews_[i].emplace(*raiiDevice_, viewInfo);
        } catch (const vk::SystemError& e) {
            SDL_Log("Failed to create volumetric snow cascade %d image view: %s", i, e.what());
            return false;
        }
    }

    // Create shared sampler for all cascades
    cascadeSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!cascadeSampler_) {
        SDL_Log("Failed to create volumetric snow cascade sampler");
        return false;
    }

    // Initialize cascade origins at world center
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float halfSize = SNOW_CASCADE_COVERAGE[i] * 0.5f;
        cascadeOrigins[i] = glm::vec2(-halfSize, -halfSize);
    }

    return true;
}

bool VolumetricSnowSystem::createComputeDescriptorSetLayout() {
    return DescriptorSetLayoutBuilder()
        // binding 0: cascade 0 storage image (read/write)
        .addBinding(BindingBuilder::storageImage(0, vk::ShaderStageFlagBits::eCompute))
        // binding 1: cascade 1 storage image (read/write)
        .addBinding(BindingBuilder::storageImage(1, vk::ShaderStageFlagBits::eCompute))
        // binding 2: cascade 2 storage image (read/write)
        .addBinding(BindingBuilder::storageImage(2, vk::ShaderStageFlagBits::eCompute))
        // binding 3: uniform buffer
        .addBinding(BindingBuilder::uniformBuffer(3, vk::ShaderStageFlagBits::eCompute))
        // binding 4: interaction sources SSBO
        .addBinding(BindingBuilder::storageBuffer(4, vk::ShaderStageFlagBits::eCompute))
        .buildInto(*raiiDevice_, computeSetLayout_);
}

bool VolumetricSnowSystem::createComputePipeline() {
    if (!PipelineLayoutBuilder(*raiiDevice_)
             .addDescriptorSetLayout(**computeSetLayout_)
             .buildInto(computePipelineLayout_)) {
        return false;
    }

    return ComputePipelineBuilder(*raiiDevice_)
        .setShader(getShaderPath() + "/volumetric_snow.comp.spv")
        .setPipelineLayout(**computePipelineLayout_)
        .buildInto(computePipeline_);
}

bool VolumetricSnowSystem::createDescriptorSets() {
    // Allocate descriptor sets using managed pool
    computeDescriptorSets = getDescriptorPool()->allocate(**computeSetLayout_, getFramesInFlight());
    if (computeDescriptorSets.size() != getFramesInFlight()) {
        SDL_Log("Failed to allocate volumetric snow descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < getFramesInFlight(); i++) {
        DescriptorManager::SetWriter(getDevice(), computeDescriptorSets[i])
            .writeStorageImage(0, **cascadeViews_[0])
            .writeStorageImage(1, **cascadeViews_[1])
            .writeStorageImage(2, **cascadeViews_[2])
            .writeBuffer(3, uniformBuffers_[i].get(), 0, sizeof(VolumetricSnowUniforms))
            .writeBuffer(4, interactionBuffers_[i].get(), 0, sizeof(VolumetricSnowInteraction) * MAX_INTERACTIONS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    return true;
}

void VolumetricSnowSystem::updateCascadeOrigins(const glm::vec3& cameraPos) {
    // Each cascade is centered on the camera position
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float halfSize = SNOW_CASCADE_COVERAGE[i] * 0.5f;
        cascadeOrigins[i] = glm::vec2(cameraPos.x - halfSize, cameraPos.z - halfSize);
    }
    lastCameraPosition = cameraPos;
}

void VolumetricSnowSystem::setCameraPosition(const glm::vec3& worldPos) {
    updateCascadeOrigins(worldPos);
}

void VolumetricSnowSystem::updateUniforms(uint32_t frameIndex, float deltaTime, bool isSnowing,
                                           float weatherIntensity, const EnvironmentSettings& settings) {
    VolumetricSnowUniforms uniforms{};

    // Cascade regions
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float texelSize = SNOW_CASCADE_COVERAGE[i] / static_cast<float>(SNOW_CASCADE_SIZE);
        glm::vec4 region = glm::vec4(cascadeOrigins[i].x, cascadeOrigins[i].y,
                                      SNOW_CASCADE_COVERAGE[i], texelSize);
        if (i == 0) uniforms.cascade0Region = region;
        else if (i == 1) uniforms.cascade1Region = region;
        else uniforms.cascade2Region = region;
    }

    // Convert coverage-based accumulation to height-based
    // Target height = snowAmount * MAX_SNOW_HEIGHT
    float targetHeight = settings.snowAmount * MAX_SNOW_HEIGHT;

    uniforms.accumulationParams = glm::vec4(
        settings.snowAccumulationRate * MAX_SNOW_HEIGHT,  // Height accumulation rate
        settings.snowMeltRate * MAX_SNOW_HEIGHT,          // Height melt rate
        deltaTime,
        isSnowing ? 1.0f : 0.0f
    );

    uniforms.snowParams = glm::vec4(
        targetHeight,
        weatherIntensity,
        static_cast<float>(currentInteractions.size()),
        MAX_SNOW_HEIGHT
    );

    // Wind parameters
    uniforms.windParams = glm::vec4(
        windDirection.x,
        windDirection.y,
        windStrength,
        driftRate
    );

    uniforms.cameraPosition = glm::vec4(lastCameraPosition, 0.0f);

    // Bounds check: frameIndex must be within range
    if (frameIndex >= uniformMapped_.size()) return;
    memcpy(uniformMapped_[frameIndex], &uniforms, sizeof(VolumetricSnowUniforms));

    // Copy interaction sources to buffer
    if (!currentInteractions.empty() && frameIndex < interactionMapped_.size()) {
        size_t copySize = sizeof(VolumetricSnowInteraction) * std::min(currentInteractions.size(),
                                                                        static_cast<size_t>(MAX_INTERACTIONS));
        memcpy(interactionMapped_[frameIndex], currentInteractions.data(), copySize);
    }
}

void VolumetricSnowSystem::addInteraction(const glm::vec3& position, float radius, float strength, float depthFactor) {
    if (currentInteractions.size() >= MAX_INTERACTIONS) {
        return;
    }

    VolumetricSnowInteraction interaction{};
    interaction.positionAndRadius = glm::vec4(position, radius);
    interaction.strengthAndDepth = glm::vec4(strength, depthFactor, 0.0f, 0.0f);

    currentInteractions.push_back(interaction);
}

void VolumetricSnowSystem::clearInteractions() {
    currentInteractions.clear();
}

std::array<glm::vec4, NUM_SNOW_CASCADES> VolumetricSnowSystem::getCascadeParams() const {
    std::array<glm::vec4, NUM_SNOW_CASCADES> params;
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float texelSize = SNOW_CASCADE_COVERAGE[i] / static_cast<float>(SNOW_CASCADE_SIZE);
        params[i] = glm::vec4(cascadeOrigins[i].x, cascadeOrigins[i].y,
                              SNOW_CASCADE_COVERAGE[i], texelSize);
    }
    return params;
}

void VolumetricSnowSystem::recordCompute(vk::CommandBuffer cmd, uint32_t frameIndex) {
    barrierCascadesForCompute(cmd);

    // Bind compute pipeline and descriptor set
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             **computePipelineLayout_, 0,
                             vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

    // Dispatch for each cascade (same shader, different region in uniforms)
    // All cascades are the same resolution so same dispatch count
    uint32_t workgroupCount = SNOW_CASCADE_SIZE / WORKGROUP_SIZE;
    vkCmd.dispatch(workgroupCount, workgroupCount, NUM_SNOW_CASCADES);

    barrierCascadesForSampling(cmd);

    // Mark first frame as done
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        isFirstFrame[i] = false;
    }

    // Clear interactions for next frame
    clearInteractions();
}

void VolumetricSnowSystem::barrierCascadesForCompute(vk::CommandBuffer cmd) {
    vk::CommandBuffer vkCmd(cmd);
    vk::PipelineStageFlags srcStage = isFirstFrame[0] ?
        vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eFragmentShader;

    std::vector<vk::ImageMemoryBarrier> barriers;
    barriers.reserve(NUM_SNOW_CASCADES);
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        vk::ImageLayout oldLayout = isFirstFrame[i] ?
            vk::ImageLayout::eUndefined : vk::ImageLayout::eShaderReadOnlyOptimal;
        vk::AccessFlags srcAccess = isFirstFrame[i] ? vk::AccessFlags{} : vk::AccessFlagBits::eShaderRead;

        barriers.push_back(vk::ImageMemoryBarrier{}
            .setSrcAccessMask(srcAccess)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(oldLayout)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascadeImages_[i].get())
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
    }
    vkCmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, barriers);
}

void VolumetricSnowSystem::barrierCascadesForSampling(vk::CommandBuffer cmd) {
    vk::CommandBuffer vkCmd(cmd);
    std::vector<vk::ImageMemoryBarrier> barriers;
    barriers.reserve(NUM_SNOW_CASCADES);
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        barriers.push_back(vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascadeImages_[i].get())
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
    }
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, barriers);
}
