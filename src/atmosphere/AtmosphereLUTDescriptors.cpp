#include "AtmosphereLUTSystem.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
#include <array>

bool AtmosphereLUTSystem::createDescriptorSetLayouts() {
    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return b;
    };

    // Transmittance LUT descriptor set layout (just output image and uniform buffer)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            makeComputeBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &transmittanceDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &transmittanceDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &transmittancePipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance pipeline layout");
            return false;
        }
    }

    // Multi-scatter LUT descriptor set layout (transmittance input, output image, uniform buffer)
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
            makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            makeComputeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeComputeBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &multiScatterDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &multiScatterDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &multiScatterPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter pipeline layout");
            return false;
        }
    }

    // Sky-view LUT descriptor set layout (transmittance + multiscatter inputs, output image, uniform buffer)
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
            makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            makeComputeBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeComputeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeComputeBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &skyViewDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &skyViewDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skyViewPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view pipeline layout");
            return false;
        }
    }

    // Irradiance LUT descriptor set layout (Phase 4.1.9)
    // Two output images (Rayleigh and Mie), transmittance input, uniform buffer
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
            makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            makeComputeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            makeComputeBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            makeComputeBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &irradianceDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &irradianceDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &irradiancePipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance pipeline layout");
            return false;
        }
    }

    // Cloud Map LUT descriptor set layout (output image, uniform buffer)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            makeComputeBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cloudMapDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &cloudMapDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &cloudMapPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map pipeline layout");
            return false;
        }
    }

    return true;
}

bool AtmosphereLUTSystem::createDescriptorSets() {
    // Allocate transmittance descriptor set using managed pool
    {
        transmittanceDescriptorSet = descriptorPool->allocateSingle(transmittanceDescriptorSetLayout);
        if (transmittanceDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate transmittance descriptor set");
            return false;
        }

        DescriptorManager::SetWriter(device, transmittanceDescriptorSet)
            .writeStorageImage(0, transmittanceLUTView)
            .writeBuffer(1, staticUniformBuffers.buffers[0], 0, sizeof(AtmosphereUniforms))
            .update();
    }

    // Allocate multi-scatter descriptor set using managed pool
    {
        multiScatterDescriptorSet = descriptorPool->allocateSingle(multiScatterDescriptorSetLayout);
        if (multiScatterDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate multi-scatter descriptor set");
            return false;
        }

        DescriptorManager::SetWriter(device, multiScatterDescriptorSet)
            .writeStorageImage(0, multiScatterLUTView)
            .writeImage(1, transmittanceLUTView, lutSampler)
            .writeBuffer(2, staticUniformBuffers.buffers[0], 0, sizeof(AtmosphereUniforms))
            .update();
    }

    // Allocate per-frame sky-view descriptor sets (double-buffered) using managed pool
    {
        skyViewDescriptorSets = descriptorPool->allocate(skyViewDescriptorSetLayout, framesInFlight);
        if (skyViewDescriptorSets.empty()) {
            SDL_Log("Failed to allocate sky-view descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            DescriptorManager::SetWriter(device, skyViewDescriptorSets[i])
                .writeStorageImage(0, skyViewLUTView)
                .writeImage(1, transmittanceLUTView, lutSampler)
                .writeImage(2, multiScatterLUTView, lutSampler)
                .writeBuffer(3, skyViewUniformBuffers.buffers[i], 0, sizeof(AtmosphereUniforms))
                .update();
        }
    }

    // Allocate irradiance descriptor set using managed pool
    {
        irradianceDescriptorSet = descriptorPool->allocateSingle(irradianceDescriptorSetLayout);
        if (irradianceDescriptorSet == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate irradiance descriptor set");
            return false;
        }

        DescriptorManager::SetWriter(device, irradianceDescriptorSet)
            .writeStorageImage(0, rayleighIrradianceLUTView)
            .writeStorageImage(1, mieIrradianceLUTView)
            .writeImage(2, transmittanceLUTView, lutSampler)
            .writeBuffer(3, staticUniformBuffers.buffers[0], 0, sizeof(AtmosphereUniforms))
            .update();
    }

    // Allocate per-frame cloud map descriptor sets (double-buffered) using managed pool
    {
        cloudMapDescriptorSets = descriptorPool->allocate(cloudMapDescriptorSetLayout, framesInFlight);
        if (cloudMapDescriptorSets.empty()) {
            SDL_Log("Failed to allocate cloud map descriptor sets");
            return false;
        }

        // Update each per-frame descriptor set with its corresponding uniform buffer
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            DescriptorManager::SetWriter(device, cloudMapDescriptorSets[i])
                .writeStorageImage(0, cloudMapLUTView)
                .writeBuffer(1, cloudMapUniformBuffers.buffers[i], 0, sizeof(CloudMapUniforms))
                .update();
        }
    }

    return true;
}
