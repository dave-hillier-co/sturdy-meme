#include "SceneObjectsDrawable.h"
#include "UBOs.h"  // For PushConstants (generated from shaders)
#include "../GPUSceneBuffer.h"

#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "npc/NPCSimulation.h"
#include "GrassSystem.h"
#include "ScatterSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "WindSystem.h"
#include "Mesh.h"

// ECS includes
#include "ecs/World.h"
#include "ecs/Components.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <numeric>

SceneObjectsDrawable::SceneObjectsDrawable(const Resources& resources)
    : resources_(resources)
{
}

void SceneObjectsDrawable::recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                          float time, const HDRDrawParams& params) {
    vk::CommandBuffer vkCmd(cmd);

    if (params.sceneObjectsPipeline) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.sceneObjectsPipeline);
    }
    recordSceneObjects(cmd, frameIndex, params);
}

void SceneObjectsDrawable::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex,
                                               const HDRDrawParams& params) {
    if (!params.pipelineLayout) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneObjectsDrawable: pipelineLayout not set");
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Get MaterialRegistry for descriptor set lookup
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();

    // The main scene objects (those mirrored into GPUSceneBuffer) draw via the GPU-driven
    // indirect path when enabled, otherwise the CPU path below. Either way, rocks, detritus
    // and trees still render afterward, so they must not be skipped here.
    const bool useIndirect = params.useIndirectDraw && params.gpuSceneBuffer
                          && params.gpuSceneBuffer->getObjectCount() > 0;

    // Helper lambda to render an entity with RenderData
    auto renderWithRenderData = [&](const ecs::RenderData& data, VkDescriptorSet descSet) {
        if (!data.mesh) return;

        PushConstants push{};
        push.model = data.transform;
        push.roughness = data.roughness;
        push.metallic = data.metallic;
        push.emissiveIntensity = data.emissiveIntensity;
        push.opacity = data.opacity;
        push.emissiveColor = glm::vec4(data.emissiveColor, 1.0f);
        push.pbrFlags = data.pbrFlags;
        push.alphaTestThreshold = data.alphaTestThreshold;

        vkCmd.pushConstants<PushConstants>(
            *params.pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 *params.pipelineLayout, 0, vk::DescriptorSet(descSet), {});

        vk::Buffer vertexBuffers[] = {data.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(data.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        vkCmd.drawIndexed(data.mesh->getIndexCount(), 1, 0, 0, 0);
    };

    if (useIndirect) {
        // Main scene objects via GPU-driven instanced/indirect draw.
        recordSceneObjectsIndirect(cmd, frameIndex, params);
        // Rebind the CPU scene pipeline so the rocks/detritus draws below are valid.
        if (params.sceneObjectsPipeline) {
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.sceneObjectsPipeline);
        }
    } else {
    // CPU scene-object path (ECS is always present on this path).
    {
        ecs::World& world = *resources_.ecsWorld;

        // Collect entities to render (those with MeshRef and MaterialRef, excluding special entities)
        std::vector<ecs::RenderData> renderList;
        renderList.reserve(256);  // Preallocate for typical scene size

        // Query all entities with MeshRef and MaterialRef (required for rendering)
        for (auto [entity, meshRef, materialRef] : world.view<ecs::MeshRef, ecs::MaterialRef>().each()) {
            // Skip entities rendered by specialized systems
            if (world.has<ecs::PlayerTag>(entity)) continue;   // Skinned mesh renderer
            if (world.has<ecs::NPCTag>(entity)) continue;      // NPC renderer
            if (world.has<ecs::TreeData>(entity)) continue;    // Tree renderer

            // Extract render data from entity's components
            ecs::RenderData data = ecs::extractRenderData(world, entity);
            if (data.mesh && data.materialId != ecs::InvalidMaterialId) {
                renderList.push_back(data);
            }
        }

        // Sort by materialId to minimize descriptor set switches
        std::sort(renderList.begin(), renderList.end(), [](const ecs::RenderData& a, const ecs::RenderData& b) {
            return a.materialId < b.materialId;
        });

        // Render sorted entities
        ecs::MaterialId lastMaterialId = ecs::InvalidMaterialId;
        VkDescriptorSet currentDescSet = VK_NULL_HANDLE;

        for (const auto& data : renderList) {
            if (data.materialId != lastMaterialId) {
                currentDescSet = materialRegistry.getDescriptorSet(data.materialId, frameIndex);
                if (currentDescSet == VK_NULL_HANDLE) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Skipping entity with invalid materialId %u", data.materialId);
                    continue;
                }
                lastMaterialId = data.materialId;
            }
            renderWithRenderData(data, currentDescSet);
        }
    }
    }  // end CPU main-objects path (else of useIndirect)

    // Rocks and detritus. When the indirect path is active they are folded into the
    // GPUSceneBuffer (see FrameUpdater::populateGPUSceneBuffer) and drawn there with GPU
    // frustum culling, so the CPU loops below must be skipped to avoid double-drawing.
    if (!useIndirect) {
        // Render procedural rocks (ScatterSystem owns its own descriptor sets)
        if (resources_.rocks && resources_.rocks->hasDescriptorSets()) {
            VkDescriptorSet rockDescSet = resources_.rocks->getDescriptorSet(frameIndex);
            for (const auto& rock : resources_.rocks->getSceneObjects()) {
                renderWithRenderData(rock, rockDescSet);
            }
        }

        // Render woodland detritus (ScatterSystem owns its own descriptor sets)
        if (resources_.detritus && resources_.detritus->hasDescriptorSets()) {
            VkDescriptorSet detritusDescSet = resources_.detritus->getDescriptorSet(frameIndex);
            for (const auto& detritus : resources_.detritus->getSceneObjects()) {
                renderWithRenderData(detritus, detritusDescSet);
            }
        }
    }

    // Render procedural trees using dedicated TreeRenderer with wind animation
    if (resources_.tree && resources_.treeRenderer) {
        resources_.treeRenderer->render(vk::CommandBuffer(cmd), frameIndex,
                                        resources_.wind->getTime(),
                                        *resources_.tree, resources_.treeLOD);
    }

    // Render tree impostors for distant trees
    if (resources_.treeLOD) {
        if (resources_.impostorCull && resources_.impostorCull->getTreeCount() > 0) {
            // Use GPU-culled indirect rendering
            resources_.treeLOD->renderImpostorsGPUCulled(
                cmd, frameIndex,
                resources_.globalBuffers->uniformBuffers.buffers[frameIndex],
                resources_.shadow->getShadowImageView(),
                resources_.shadow->getShadowSampler(),
                resources_.impostorCull->getVisibleImpostorBuffer(),
                resources_.impostorCull->getIndirectDrawBuffer()
            );
        } else {
            // Fall back to CPU-culled rendering
            resources_.treeLOD->renderImpostors(
                cmd, frameIndex,
                resources_.globalBuffers->uniformBuffers.buffers[frameIndex],
                resources_.shadow->getShadowImageView(),
                resources_.shadow->getShadowSampler()
            );
        }
    }
}

void SceneObjectsDrawable::recordSceneObjectsIndirect(VkCommandBuffer cmd, uint32_t frameIndex,
                                                       const HDRDrawParams& params) {
    if (!params.gpuSceneBuffer || !params.instancedPipeline || !params.instancedPipelineLayout) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneObjectsDrawable: indirect draw requires gpuSceneBuffer, instancedPipeline and layout");
        return;
    }

    GPUSceneBuffer* sceneBuffer = params.gpuSceneBuffer;
    const auto& batches = sceneBuffer->getBatches();
    if (batches.empty()) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();

    // Instanced pipeline: set 0 = material (bound per batch), set 1 = instance SSBO (bound once).
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.instancedPipeline);

    if (params.instanceDescriptorSet != VK_NULL_HANDLE) {
        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 *params.instancedPipelineLayout, 1,
                                 vk::DescriptorSet(params.instanceDescriptorSet), {});
    }

    // One draw per (mesh, material) batch. The GPU cull pass writes one indirect command
    // per object into a stable slot (command k -> instance k); each batch owns the
    // contiguous range [firstObject, firstObject + objectCount). firstInstance carries the
    // slot so gl_InstanceIndex selects the right instance, and instanceCount (0/1) from the
    // cull pass skips culled objects. Falls back to a direct instanced draw where
    // multiDrawIndirect/drawIndirectFirstInstance are unavailable.
    const VkBuffer indirectBuffer = sceneBuffer->getIndirectBuffer(frameIndex);
    constexpr uint32_t kCmdStride = sizeof(GPUDrawIndexedIndirectCommand);

    VkDescriptorSet lastSet = VK_NULL_HANDLE;
    for (const auto& batch : batches) {
        if (!batch.mesh || batch.objectCount == 0) {
            continue;
        }

        // Resolve set 0: scatter objects carry an explicit descriptor override; normal
        // objects resolve it from MaterialRegistry by materialId.
        VkDescriptorSet set0 = batch.overrideDescriptorSet != VK_NULL_HANDLE
            ? batch.overrideDescriptorSet
            : materialRegistry.getDescriptorSet(batch.materialId, frameIndex);
        if (set0 == VK_NULL_HANDLE) {
            continue;  // skip a batch whose descriptor set is unavailable
        }
        if (set0 != lastSet) {
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     *params.instancedPipelineLayout, 0,
                                     vk::DescriptorSet(set0), {});
            lastSet = set0;
        }

        vk::Buffer vertexBuffers[] = {batch.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(batch.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        if (params.canMultiDrawIndirect && indirectBuffer != VK_NULL_HANDLE) {
            vkCmd.drawIndexedIndirect(
                vk::Buffer(indirectBuffer),
                static_cast<vk::DeviceSize>(batch.firstObject) * kCmdStride,
                batch.objectCount,
                kCmdStride);
        } else {
            // Direct fallback: instanceCount = objectCount, firstInstance = firstObject.
            // (No GPU culling in this path; all objects in the batch are drawn.)
            vkCmd.drawIndexed(batch.mesh->getIndexCount(), batch.objectCount, 0, 0, batch.firstObject);
        }
    }

    // Note: Trees, rocks, and other subsystems still use their own rendering paths
    // Full GPU-driven rendering would consolidate these into the scene buffer
}
