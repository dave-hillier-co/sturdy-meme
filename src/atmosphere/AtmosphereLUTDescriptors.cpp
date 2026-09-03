#include "AtmosphereLUTSystem.h"
#include "DescriptorManager.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include <SDL3/SDL_log.h>
#include <array>

bool AtmosphereLUTSystem::createDescriptorSetLayouts() {
    // Transmittance LUT descriptor set layout (just output image and uniform buffer)
    {
        vk::DescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)      // 0: Output image
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)     // 1: Uniform buffer
            .build();

        if (!rawLayout) {
            SDL_Log("Failed to create transmittance descriptor set layout");
            return false;
        }
        transmittanceDescriptorSetLayout.emplace(*raiiDevice_, rawLayout);

        if (!PipelineLayoutBuilder(*raiiDevice_)
                 .addDescriptorSetLayout(**transmittanceDescriptorSetLayout)
                 .buildInto(transmittancePipelineLayout)) {
            SDL_Log("Failed to create transmittance pipeline layout");
            return false;
        }
    }

    // Multi-scatter LUT descriptor set layout (transmittance input, output image, uniform buffer)
    {
        vk::DescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 0: Output image
            .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 1: Transmittance input
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: Uniform buffer
            .build();

        if (!rawLayout) {
            SDL_Log("Failed to create multi-scatter descriptor set layout");
            return false;
        }
        multiScatterDescriptorSetLayout.emplace(*raiiDevice_, rawLayout);

        if (!PipelineLayoutBuilder(*raiiDevice_)
                 .addDescriptorSetLayout(**multiScatterDescriptorSetLayout)
                 .buildInto(multiScatterPipelineLayout)) {
            SDL_Log("Failed to create multi-scatter pipeline layout");
            return false;
        }
    }

    // Sky-view LUT descriptor set layout (transmittance + multiscatter inputs, output image, uniform buffer)
    {
        vk::DescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 0: Output image
            .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 1: Transmittance input
            .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 2: Multi-scatter input
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 3: Uniform buffer
            .build();

        if (!rawLayout) {
            SDL_Log("Failed to create sky-view descriptor set layout");
            return false;
        }
        skyViewDescriptorSetLayout.emplace(*raiiDevice_, rawLayout);

        if (!PipelineLayoutBuilder(*raiiDevice_)
                 .addDescriptorSetLayout(**skyViewDescriptorSetLayout)
                 .buildInto(skyViewPipelineLayout)) {
            SDL_Log("Failed to create sky-view pipeline layout");
            return false;
        }
    }

    // Irradiance LUT descriptor set layout (Phase 4.1.9)
    // Two output images (Rayleigh and Mie), transmittance input, uniform buffer
    {
        vk::DescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 0: Rayleigh output
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 1: Mie output
            .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 2: Transmittance input
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 3: Uniform buffer
            .build();

        if (!rawLayout) {
            SDL_Log("Failed to create irradiance descriptor set layout");
            return false;
        }
        irradianceDescriptorSetLayout.emplace(*raiiDevice_, rawLayout);

        if (!PipelineLayoutBuilder(*raiiDevice_)
                 .addDescriptorSetLayout(**irradianceDescriptorSetLayout)
                 .buildInto(irradiancePipelineLayout)) {
            SDL_Log("Failed to create irradiance pipeline layout");
            return false;
        }
    }

    // Cloud Map LUT descriptor set layout (output image, uniform buffer)
    {
        vk::DescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)      // 0: Output image
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)     // 1: Uniform buffer
            .build();

        if (!rawLayout) {
            SDL_Log("Failed to create cloud map descriptor set layout");
            return false;
        }
        cloudMapDescriptorSetLayout.emplace(*raiiDevice_, rawLayout);

        if (!PipelineLayoutBuilder(*raiiDevice_)
                 .addDescriptorSetLayout(**cloudMapDescriptorSetLayout)
                 .buildInto(cloudMapPipelineLayout)) {
            SDL_Log("Failed to create cloud map pipeline layout");
            return false;
        }
    }

    return true;
}

bool AtmosphereLUTSystem::createDescriptorSets() {
    // Allocate transmittance descriptor set using managed pool
    {
        transmittanceDescriptorSet = descriptorPool->allocateSingle(**transmittanceDescriptorSetLayout);
        if (!transmittanceDescriptorSet) {
            SDL_Log("Failed to allocate transmittance descriptor set");
            return false;
        }

        DescriptorManager::SetWriter(device, transmittanceDescriptorSet)
            .writeStorageImage(0, **transmittanceLUTView)
            .writeBuffer(1, staticUniformBuffer_.get(), 0, sizeof(AtmosphereUniforms))
            .update();
    }

    // Allocate multi-scatter descriptor set using managed pool
    {
        multiScatterDescriptorSet = descriptorPool->allocateSingle(**multiScatterDescriptorSetLayout);
        if (!multiScatterDescriptorSet) {
            SDL_Log("Failed to allocate multi-scatter descriptor set");
            return false;
        }

        DescriptorManager::SetWriter(device, multiScatterDescriptorSet)
            .writeStorageImage(0, **multiScatterLUTView)
            .writeImage(1, **transmittanceLUTView, **lutSampler_)
            .writeBuffer(2, staticUniformBuffer_.get(), 0, sizeof(AtmosphereUniforms))
            .update();
    }

    // Allocate per-frame sky-view descriptor sets (double-buffered) using managed pool
    {
        skyViewDescriptorSets = descriptorPool->allocate(**skyViewDescriptorSetLayout, framesInFlight);
        if (skyViewDescriptorSets.empty()) {
            SDL_Log("Failed to allocate sky-view descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            DescriptorManager::SetWriter(device, skyViewDescriptorSets[i])
                .writeStorageImage(0, **skyViewLUTView)
                .writeImage(1, **transmittanceLUTView, **lutSampler_)
                .writeImage(2, **multiScatterLUTView, **lutSampler_)
                .writeBuffer(3, skyViewUniformBuffers_[i].get(), 0, sizeof(AtmosphereUniforms))
                .update();
        }
    }

    // Allocate irradiance descriptor set using managed pool
    {
        irradianceDescriptorSet = descriptorPool->allocateSingle(**irradianceDescriptorSetLayout);
        if (!irradianceDescriptorSet) {
            SDL_Log("Failed to allocate irradiance descriptor set");
            return false;
        }

        DescriptorManager::SetWriter(device, irradianceDescriptorSet)
            .writeStorageImage(0, **rayleighIrradianceLUTView)
            .writeStorageImage(1, **mieIrradianceLUTView)
            .writeImage(2, **transmittanceLUTView, **lutSampler_)
            .writeBuffer(3, staticUniformBuffer_.get(), 0, sizeof(AtmosphereUniforms))
            .update();
    }

    // Allocate per-frame cloud map descriptor sets (double-buffered) using managed pool
    {
        cloudMapDescriptorSets = descriptorPool->allocate(**cloudMapDescriptorSetLayout, framesInFlight);
        if (cloudMapDescriptorSets.empty()) {
            SDL_Log("Failed to allocate cloud map descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            DescriptorManager::SetWriter(device, cloudMapDescriptorSets[i])
                .writeStorageImage(0, **cloudMapLUTView)
                .writeBuffer(1, cloudMapUniformBuffers_[i].get(), 0, sizeof(CloudMapUniforms))
                .update();
        }
    }

    return true;
}
