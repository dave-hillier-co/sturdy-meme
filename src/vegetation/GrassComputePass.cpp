#include "GrassComputePass.h"
#include "GrassBuffers.h"
#include "DisplacementSystem.h"
#include "ComputePipelineBuilder.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include "core/vulkan/BarrierHelpers.h"
#include "vulkan/PipelineLayoutBuilder.h"
#include <SDL3/SDL.h>
#include <cmath>

namespace {
struct DescriptorBindingInfo {
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stageFlags;
    uint32_t count = 1;
};

VkDescriptorSetLayout buildDescriptorSetLayout(
    VkDevice device,
    const DescriptorBindingInfo* bindings,
    size_t bindingCount
) {
    DescriptorManager::LayoutBuilder builder(device);
    for (size_t i = 0; i < bindingCount; ++i) {
        const auto& binding = bindings[i];
        builder.addBinding(binding.binding, binding.type, binding.stageFlags, binding.count);
    }
    return builder.build();
}

std::optional<VkPipelineLayout> buildPipelineLayoutRaw(
    const vk::raii::Device& device,
    vk::DescriptorSetLayout layout,
    vk::ShaderStageFlags pushStages,
    uint32_t pushSize
) {
    auto layoutOpt = PipelineLayoutBuilder(device)
        .addDescriptorSetLayout(layout)
        .addPushConstantRange(pushStages, pushSize)
        .build();

    if (!layoutOpt) {
        return std::nullopt;
    }

    return layoutOpt->release();
}
}  // namespace

bool GrassComputePass::createDescriptorSetLayout(vk::Device device,
                                                   SystemLifecycleHelper::PipelineHandles& handles) {
    const std::array<DescriptorBindingInfo, 9> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},          // instance buffer
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},          // indirect buffer
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},          // CullingUniforms
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},  // terrain heightmap
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},  // displacement map
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},  // tile array
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},          // tile info
        {7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},          // GrassParams
        {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT}   // hole mask
    }};

    handles.descriptorSetLayout =
        buildDescriptorSetLayout(device, bindings.data(), bindings.size());

    if (handles.descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass compute descriptor set layout");
        return false;
    }

    return true;
}

bool GrassComputePass::createPipeline(const vk::raii::Device& device, const std::string& shaderPath,
                                       SystemLifecycleHelper::PipelineHandles& handles) {
    auto layoutOpt = buildPipelineLayoutRaw(
        device,
        vk::DescriptorSetLayout(handles.descriptorSetLayout),
        vk::ShaderStageFlagBits::eCompute,
        sizeof(TiledGrassPushConstants));

    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass compute pipeline layout");
        return false;
    }

    handles.pipelineLayout = *layoutOpt;

    ComputePipelineBuilder builder(device);
    if (!builder.setShader(shaderPath + "/grass.comp.spv")
             .setPipelineLayout(handles.pipelineLayout)
             .buildRaw(handles.pipeline)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass compute pipeline");
        return false;
    }

    return true;
}

bool GrassComputePass::createTiledPipeline(const vk::raii::Device& device, const std::string& shaderPath,
                                            VkPipelineLayout pipelineLayout) {
    ComputePipelineBuilder builder(device);
    VkPipeline rawTiledPipeline = VK_NULL_HANDLE;
    if (!builder.setShader(shaderPath + "/grass_tiled.comp.spv")
             .setPipelineLayout(pipelineLayout)
             .buildRaw(rawTiledPipeline)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tiled grass compute pipeline");
        return false;
    }
    tiledPipeline_.emplace(device, rawTiledPipeline);
    SDL_Log("GrassComputePass: Created tiled grass compute pipeline");
    return true;
}

bool GrassComputePass::allocateDescriptorSets(DescriptorManager::Pool* pool,
                                               VkDescriptorSetLayout layout, uint32_t count) {
    descriptorSets_.resize(count);
    for (uint32_t set = 0; set < count; set++) {
        descriptorSets_[set] = pool->allocateSingle(layout);
        if (!descriptorSets_[set]) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate grass compute descriptor set (set %u)", set);
            return false;
        }
    }
    SDL_Log("GrassComputePass: allocated %u compute descriptor sets", count);
    return true;
}

void GrassComputePass::writeInitialDescriptorSets(vk::Device device, const GrassBuffers& buffers, uint32_t count) {
    for (uint32_t set = 0; set < count; set++) {
        DescriptorManager::SetWriter writer(device, descriptorSets_[set]);
        writer.writeBuffer(0, buffers.instanceBuffers().buffers[set], 0,
                           sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(1, buffers.indirectBuffers().buffers[set], 0,
                           sizeof(VkDrawIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, buffers.uniformBuffers().buffers[0], 0, sizeof(CullingUniforms));
        writer.writeBuffer(7, buffers.paramsBuffers().buffers[0], 0, sizeof(GrassParams));
        writer.update();
    }
}

void GrassComputePass::updateDescriptorSets(vk::Device device, uint32_t count,
                                              vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
                                              DisplacementSystem* displacementSystem,
                                              vk::ImageView tileArrayView, vk::Sampler tileSampler,
                                              const TripleBuffered<vk::Buffer>& tileInfoBuffers,
                                              vk::ImageView holeMaskView, vk::Sampler holeMaskSampler) {
    for (uint32_t set = 0; set < count; set++) {
        DescriptorManager::SetWriter computeWriter(device, descriptorSets_[set]);
        computeWriter.writeImage(3, terrainHeightMapView, terrainHeightMapSampler);
        if (displacementSystem) {
            computeWriter.writeImage(4, displacementSystem->getImageView(), displacementSystem->getSampler());
        }

        // Tile cache bindings (5 and 6) - for high-res terrain sampling
        if (tileArrayView) {
            computeWriter.writeImage(5, tileArrayView, tileSampler);
        }
        // Write initial tile info buffer (frame 0) - will be updated per-frame
        if (!tileInfoBuffers.empty() && tileInfoBuffers[0]) {
            computeWriter.writeBuffer(6, tileInfoBuffers[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Hole mask binding (8) - for terrain cutouts (caves, wells)
        if (holeMaskView) {
            computeWriter.writeImage(8, holeMaskView, holeMaskSampler);
        }

        computeWriter.update();
    }
}

void GrassComputePass::recordResetAndCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                              const GrassBuffers& buffers,
                                              const TripleBuffered<vk::Buffer>& tileInfoBuffers,
                                              const glm::vec3& cameraPos,
                                              const SystemLifecycleHelper::PipelineHandles& computeHandles) {
    uint32_t writeSet = buffers.getComputeBufferSet();

    // Ensure CPU writes to tile info buffer are visible to GPU before compute dispatch
    auto hostBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eComputeShader,
                        {}, hostBarrier, {}, {});

    // Reset indirect buffer before compute dispatch
    cmd.fillBuffer(buffers.indirectBuffers().buffers[writeSet], 0, sizeof(VkDrawIndirectCommand), 0);
    BarrierHelpers::fillBufferToCompute(cmd);

    // Bind the tiled compute pipeline if available, otherwise use the base pipeline
    vk::Pipeline computePipeline = computeHandles.pipeline;
    if (tiledPipeline_.has_value()) {
        computePipeline = **tiledPipeline_;
    }
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
    VkDescriptorSet computeSet = descriptorSets_[writeSet];
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           computeHandles.pipelineLayout, 0,
                           vk::DescriptorSet(computeSet), {});

    // Dispatch tiles around camera for coarse-grain culling
    constexpr int TILES_PER_AXIS = 5;
    float tileSize = GrassConstants::TILE_SIZE;

    int centerTileX = static_cast<int>(std::floor(cameraPos.x / tileSize));
    int centerTileZ = static_cast<int>(std::floor(cameraPos.z / tileSize));

    uint32_t tileIndex = 0;
    for (int tz = -TILES_PER_AXIS / 2; tz <= TILES_PER_AXIS / 2; ++tz) {
        for (int tx = -TILES_PER_AXIS / 2; tx <= TILES_PER_AXIS / 2; ++tx) {
            int tileX = centerTileX + tx;
            int tileZ = centerTileZ + tz;

            float tileOriginX = static_cast<float>(tileX) * tileSize;
            float tileOriginZ = static_cast<float>(tileZ) * tileSize;

            TiledGrassPushConstants grassPush{};
            grassPush.time = time;
            grassPush.tileOriginX = tileOriginX;
            grassPush.tileOriginZ = tileOriginZ;
            grassPush.tileSize = tileSize;
            grassPush.spacing = GrassConstants::SPACING;
            grassPush.tileIndex = tileIndex++;
            grassPush.unused1 = 0.0f;
            grassPush.unused2 = 0.0f;
            cmd.pushConstants<TiledGrassPushConstants>(
                computeHandles.pipelineLayout,
                vk::ShaderStageFlagBits::eCompute,
                0, grassPush);

            cmd.dispatch(GrassConstants::TILE_DISPATCH_SIZE, GrassConstants::TILE_DISPATCH_SIZE, 1);
        }
    }

    // Memory barrier: compute write -> vertex shader read and indirect read
    BarrierHelpers::computeToIndirectDrawAndShader(cmd);
}
