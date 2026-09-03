#pragma once

#include <vulkan/vulkan.hpp>
#include "DescriptorManager.h"
#include "Texture.h"

/**
 * MaterialDescriptorFactory - Encapsulates common descriptor bindings for materials
 *
 * Reduces duplication when creating descriptor sets for different materials.
 * All materials share the same "common" bindings (UBO, shadow maps, lights, etc.)
 * but differ in their texture bindings.
 */
class MaterialDescriptorFactory {
public:
    // Common resources shared across all material descriptor sets
    struct CommonBindings {
        vk::Buffer uniformBuffer;
        vk::DeviceSize uniformBufferSize;

        vk::ImageView shadowMapView;
        vk::Sampler shadowMapSampler;

        vk::Buffer lightBuffer;
        vk::DeviceSize lightBufferSize;

        vk::ImageView emissiveMapView;
        vk::Sampler emissiveMapSampler;

        vk::ImageView pointShadowView;
        vk::Sampler pointShadowSampler;

        vk::ImageView spotShadowView;
        vk::Sampler spotShadowSampler;

        vk::ImageView snowMaskView;
        vk::Sampler snowMaskSampler;

        // Optional: cloud shadow (may be added after initial creation)
        vk::ImageView cloudShadowView{};
        vk::Sampler cloudShadowSampler{};

        // Snow and cloud shadow UBOs (binding 10 and 11)
        vk::Buffer snowUboBuffer{};
        vk::DeviceSize snowUboBufferSize = 0;
        vk::Buffer cloudShadowUboBuffer{};
        vk::DeviceSize cloudShadowUboBufferSize = 0;

        // Optional: bone matrices for skinned meshes
        vk::Buffer boneMatricesBuffer{};
        vk::DeviceSize boneMatricesBufferSize = 0;

        // Placeholder texture for unused PBR bindings (bindings 13-16 must always be written)
        vk::ImageView placeholderTextureView{};
        vk::Sampler placeholderTextureSampler{};

        // Wind UBO for vegetation animation (binding 17)
        vk::Buffer windBuffer{};
        vk::DeviceSize windBufferSize = 0;

        // Screen-space shadow buffer (binding 21, optional)
        vk::ImageView screenShadowView{};
        vk::Sampler screenShadowSampler{};
    };

    // Per-material texture bindings
    struct MaterialTextures {
        vk::ImageView diffuseView;
        vk::Sampler diffuseSampler;
        vk::ImageView normalView;
        vk::Sampler normalSampler;

        // Optional PBR textures (for Substance/PBR materials)
        // Leave default (null) if not used - shader will use push constant values
        vk::ImageView roughnessView{};
        vk::Sampler roughnessSampler{};
        vk::ImageView metallicView{};
        vk::Sampler metallicSampler{};
        vk::ImageView aoView{};
        vk::Sampler aoSampler{};
        vk::ImageView heightView{};
        vk::Sampler heightSampler{};
    };

    explicit MaterialDescriptorFactory(vk::Device device);

    // Write a complete material descriptor set using common + material-specific bindings
    void writeDescriptorSet(
        vk::DescriptorSet set,
        const CommonBindings& common,
        const MaterialTextures& material);

    // Write a skinned material descriptor set (includes bone matrices at binding 10)
    void writeSkinnedDescriptorSet(
        vk::DescriptorSet set,
        const CommonBindings& common,
        const MaterialTextures& material);

    // Update only the cloud shadow binding (for late initialization)
    void updateCloudShadowBinding(
        vk::DescriptorSet set,
        vk::ImageView cloudShadowView,
        vk::Sampler cloudShadowSampler);

private:
    vk::Device device;

    void writeCommonBindings(
        DescriptorManager::SetWriter& writer,
        const CommonBindings& common);
};
