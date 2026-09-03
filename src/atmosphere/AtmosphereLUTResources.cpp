#include "AtmosphereLUTSystem.h"
#include "VmaImage.h"
#include "SamplerFactory.h"
#include "core/ImageBuilder.h"
#include "core/vulkan/VmaBufferFactory.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

bool AtmosphereLUTSystem::createTransmittanceLUT() {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereLUTSystem requires raiiDevice");
        return false;
    }
    if (!ImageBuilder(allocator)
            .setExtent(TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(*raiiDevice_, transmittanceLUT, transmittanceLUTView)) {
        SDL_Log("Failed to create transmittance LUT");
        return false;
    }
    return true;
}

bool AtmosphereLUTSystem::createMultiScatterLUT() {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereLUTSystem requires raiiDevice");
        return false;
    }
    if (!ImageBuilder(allocator)
            .setExtent(MULTISCATTER_SIZE, MULTISCATTER_SIZE)
            .setFormat(VK_FORMAT_R16G16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(*raiiDevice_, multiScatterLUT, multiScatterLUTView)) {
        SDL_Log("Failed to create multi-scatter LUT");
        return false;
    }
    return true;
}

bool AtmosphereLUTSystem::createSkyViewLUT() {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereLUTSystem requires raiiDevice");
        return false;
    }
    if (!ImageBuilder(allocator)
            .setExtent(SKYVIEW_WIDTH, SKYVIEW_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(*raiiDevice_, skyViewLUT, skyViewLUTView)) {
        SDL_Log("Failed to create sky-view LUT");
        return false;
    }
    return true;
}

bool AtmosphereLUTSystem::createIrradianceLUTs() {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereLUTSystem requires raiiDevice");
        return false;
    }

    // Create Rayleigh Irradiance LUT (64×16, RGBA16F)
    if (!ImageBuilder(allocator)
            .setExtent(IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(*raiiDevice_, rayleighIrradianceLUT, rayleighIrradianceLUTView)) {
        SDL_Log("Failed to create Rayleigh irradiance LUT");
        return false;
    }

    // Create Mie Irradiance LUT (same dimensions and format)
    if (!ImageBuilder(allocator)
            .setExtent(IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(*raiiDevice_, mieIrradianceLUT, mieIrradianceLUTView)) {
        SDL_Log("Failed to create Mie irradiance LUT");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createCloudMapLUT() {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereLUTSystem requires raiiDevice");
        return false;
    }
    // Cloud Map LUT (256×256, RGBA16F) - Paraboloid projection
    if (!ImageBuilder(allocator)
            .setExtent(CLOUDMAP_SIZE, CLOUDMAP_SIZE)
            .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            .build(*raiiDevice_, cloudMapLUT, cloudMapLUTView)) {
        SDL_Log("Failed to create cloud map LUT");
        return false;
    }
    return true;
}

bool AtmosphereLUTSystem::createLUTSampler() {
    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereLUTSystem requires raiiDevice");
        return false;
    }

    lutSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!lutSampler_) {
        SDL_Log("Failed to create LUT sampler");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createUniformBuffer() {
    // Create static uniform buffer for one-time LUT computations
    if (!VmaBufferFactory::createUniformBuffer(allocator, sizeof(AtmosphereUniforms), staticUniformBuffer_)) {
        SDL_Log("Failed to create static atmosphere uniform buffer");
        return false;
    }
    staticUniformMapped_ = staticUniformBuffer_.map();
    if (!staticUniformMapped_) {
        SDL_Log("Failed to create static atmosphere uniform buffer");
        return false;
    }

    // Create per-frame uniform buffers for sky view LUT updates (double-buffered)
    skyViewUniformBuffers_.resize(framesInFlight);
    skyViewUniformMapped_.resize(framesInFlight, nullptr);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        if (!VmaBufferFactory::createUniformBuffer(allocator, sizeof(AtmosphereUniforms), skyViewUniformBuffers_[i])) {
            SDL_Log("Failed to create sky view per-frame uniform buffers");
            return false;
        }
        skyViewUniformMapped_[i] = skyViewUniformBuffers_[i].map();
        if (!skyViewUniformMapped_[i]) {
            SDL_Log("Failed to create sky view per-frame uniform buffers");
            return false;
        }
    }

    // Create per-frame uniform buffers for cloud map LUT updates (double-buffered)
    cloudMapUniformBuffers_.resize(framesInFlight);
    cloudMapUniformMapped_.resize(framesInFlight, nullptr);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        if (!VmaBufferFactory::createUniformBuffer(allocator, sizeof(CloudMapUniforms), cloudMapUniformBuffers_[i])) {
            SDL_Log("Failed to create cloud map per-frame uniform buffers");
            return false;
        }
        cloudMapUniformMapped_[i] = cloudMapUniformBuffers_[i].map();
        if (!cloudMapUniformMapped_[i]) {
            SDL_Log("Failed to create cloud map per-frame uniform buffers");
            return false;
        }
    }

    return true;
}
