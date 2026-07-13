#include "ShadowPassRecorder.h"
#include "../RendererSystems.h"
#include "../PerformanceToggles.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "GrassSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "ScatterSystem.h"
#include "SkinnedMeshRenderer.h"
#include "GlobalBufferManager.h"
#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "Profiler.h"
#include "AnimatedCharacter.h"
#include "SkinnedMesh.h"
#include "CullCommon.h"  // For extractFrustumPlanes
#include "culling/ShadowCullPass.h"
#include "GPUSceneBuffer.h"

// ECS includes for Phase 6 rendering
#include "ecs/World.h"
#include "ecs/Components.h"

ShadowPassRecorder::ShadowPassRecorder(const ShadowPassResources& resources)
    : resources_(resources)
{
}

ShadowPassRecorder::ShadowPassRecorder(RendererSystems& systems)
    : resources_(ShadowPassResources::collect(systems))
{
}

void ShadowPassRecorder::record(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                const glm::vec3& cameraPosition, const Params& params) {
    // Setup phase: build callbacks and collect shadow-casting objects
    resources_.profiler->beginCpuZone("Shadow:Setup");

    // Delegate to the shadow system with callbacks for terrain and grass
    auto terrainCallback = [this, &params, frameIndex](vk::CommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (params.terrainEnabled && params.terrainShadows) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Terrain");
            resources_.terrain->recordShadowDraw(cb, frameIndex, lightMatrix, static_cast<int>(cascade));
            resources_.profiler->endGpuZone(cb, "Shadow:Terrain");
        }
    };

    auto grassCallback = [this, &params, frameIndex, time](vk::CommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;  // Grass uses cascade index only
        if (params.grassShadows) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Grass");
            resources_.vegetation.grass().recordShadowDraw(cb, frameIndex, time, static_cast<int>(cascade));
            resources_.profiler->endGpuZone(cb, "Shadow:Grass");
        }
    };

    auto treeCallback = [this, frameIndex](vk::CommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;
        if (resources_.vegetation.hasTree() && resources_.vegetation.hasTreeRenderer()) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Trees");
            resources_.vegetation.treeRenderer()->renderShadows(cb, frameIndex, *resources_.vegetation.tree(), static_cast<int>(cascade), resources_.vegetation.treeLOD());
            resources_.profiler->endGpuZone(cb, "Shadow:Trees");
        }
        // Render impostor shadows
        if (resources_.vegetation.hasTreeLOD()) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Impostors");
            vk::Buffer uniformBuffer = resources_.globalBuffers->uniformBuffers.buffers[frameIndex];
            auto* impostorCull = resources_.vegetation.impostorCull();
            if (impostorCull && impostorCull->getTreeCount() > 0) {
                // Use GPU-culled indirect rendering
                resources_.vegetation.treeLOD()->renderImpostorShadowsGPUCulled(
                    cb, frameIndex, static_cast<int>(cascade), uniformBuffer,
                    impostorCull->getVisibleImpostorBuffer(),
                    impostorCull->getIndirectDrawBuffer()
                );
            } else {
                // Fall back to CPU-culled rendering
                resources_.vegetation.treeLOD()->renderImpostorShadows(cb, frameIndex, static_cast<int>(cascade), uniformBuffer);
            }
            resources_.profiler->endGpuZone(cb, "Shadow:Impostors");
        }
    };

    // When the GPU-driven indirect shadow path is active it draws everything mirrored into
    // GPUSceneBuffer (legacy renderables + scatter rocks/detritus). Those must NOT also be
    // collected into the CPU/instanced list below or they would be drawn twice. ECS scene
    // entities are NOT in GPUSceneBuffer, so they are still collected and drawn via the
    // instanced path alongside the indirect draw (the same split the color pass uses).
    const bool indirectActive = params.indirectShadowDraw
                             && resources_.shadowCullPass && resources_.gpuSceneBuffer
                             && resources_.shadow->hasIndirectShadowPath()
                             && resources_.gpuSceneBuffer->getObjectCount() > 0;

    // Collect shadow-casting scene objects for the CPU/instanced shadow path.
    // Skip player character - it's rendered separately with skinned shadow pipeline.
    std::vector<ecs::RenderData> allObjects;
    bool hasCharacter = resources_.scene->getSceneBuilder().hasCharacter();

    size_t detritusCount = resources_.vegetation.hasDetritus() ? resources_.vegetation.detritus()->getSceneObjects().size() : 0;
    size_t rockCount = resources_.vegetation.rocks().getSceneObjects().size();

    if (resources_.ecsWorld) {
        ecs::World& world = *resources_.ecsWorld;

        // Collect shadow-casting entities straight from the ECS as RenderData (no
        // Renderable round-trip). On the indirect path these are also drawn from
        // GPUSceneBuffer; the instanced draw of this list is the same set (depth is
        // idempotent), matching the pre-existing behavior of the mirrored buffer.
        allObjects.reserve(256 + rockCount + detritusCount);

        for (auto [entity, meshRef, materialRef] : world.view<ecs::MeshRef, ecs::MaterialRef>().each()) {
            // Skip entities rendered by specialized systems
            if (world.has<ecs::PlayerTag>(entity)) continue;   // Skinned mesh renderer
            if (world.has<ecs::NPCTag>(entity)) continue;      // NPC renderer (handled separately)
            if (world.has<ecs::TreeData>(entity)) continue;    // Tree renderer

            // Only include shadow-casting entities
            if (!world.has<ecs::CastsShadow>(entity)) continue;

            ecs::RenderData data = ecs::extractRenderData(world, entity);
            if (data.mesh && data.materialId != ecs::InvalidMaterialId) {
                allObjects.push_back(data);
            }
        }
    }

    // Add rocks and detritus (scatter RenderData). When indirect is active these ride the
    // GPUSceneBuffer indirect draw (with the cull-extension below keeping off-slice casters),
    // so don't also collect them on the CPU/instanced path.
    if (!indirectActive) {
        for (const auto& r : resources_.vegetation.rocks().getSceneObjects()) {
            allObjects.push_back(r);
        }
        if (resources_.vegetation.hasDetritus()) {
            for (const auto& r : resources_.vegetation.detritus()->getSceneObjects()) {
                allObjects.push_back(r);
            }
        }
    }

    // Skinned character shadow callback (renders with GPU skinning)
    ShadowSystem::DrawCallback skinnedCallback = nullptr;
    if (hasCharacter) {
        skinnedCallback = [this, frameIndex](vk::CommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
            (void)lightMatrix;  // Not used, cascade matrices are in UBO
            SceneBuilder& sceneBuilder = resources_.scene->getSceneBuilder();
            ecs::Entity playerEntity = sceneBuilder.getPlayerEntity();
            if (!resources_.ecsWorld || !resources_.ecsWorld->valid(playerEntity) ||
                !resources_.ecsWorld->has<ecs::Transform>(playerEntity)) return;
            glm::mat4 playerTransform = resources_.ecsWorld->get<ecs::Transform>(playerEntity).matrix;

            resources_.profiler->beginGpuZone(cb, "Shadow:Skinned");
            AnimatedCharacter& character = sceneBuilder.getAnimatedCharacter();
            SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

            // Bind skinned shadow pipeline with descriptor set that has bone matrices
            resources_.shadow->bindSkinnedShadowPipeline(cb, resources_.skinnedMesh->getDescriptorSet(frameIndex));

            // Record the skinned mesh shadow
            resources_.shadow->recordSkinnedMeshShadow(cb, cascade, playerTransform, skinnedMesh);
            resources_.profiler->endGpuZone(cb, "Shadow:Skinned");
        };
    }

    // Pre-cascade compute callback for GPU culling (runs before each cascade's render pass)
    ShadowSystem::ComputeCallback preCascadeComputeCallback = [this, cameraPosition](
        vk::CommandBuffer cb, uint32_t frame, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (resources_.vegetation.hasTreeRenderer() && resources_.vegetation.hasTree() && resources_.vegetation.hasTreeLOD()) {
            // Extract frustum planes from the light view-projection matrix
            glm::vec4 cascadeFrustumPlanes[6];
            extractFrustumPlanes(lightMatrix, cascadeFrustumPlanes);

            // Record GPU culling pass for branch shadows
            resources_.vegetation.treeRenderer()->recordBranchShadowCulling(
                cb, frame, cascade, cascadeFrustumPlanes, cameraPosition, resources_.vegetation.treeLOD());
        }
    };

    // Use any MaterialRegistry descriptor set for shadow pass (only needs common bindings/UBO)
    // MaterialId 0 is the first registered material (crate)
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();
    vk::DescriptorSet shadowDescriptorSet = materialRegistry.getDescriptorSet(0, frameIndex);

    // GPU-driven indirect scene-object shadow path. When enabled and available, the shared
    // scene objects (those mirrored into GPUSceneBuffer) are culled per-cascade on the GPU
    // and drawn indirectly; the CPU allObjects path above is skipped for them. Rocks/detritus
    // ride along since they are part of GPUSceneBuffer too.
    ShadowSystem::IndirectShadowParams indirect{};
    if (indirectActive) {
        indirect.enabled = true;
        indirect.cullPass = resources_.shadowCullPass;
        indirect.sceneBuffer = resources_.gpuSceneBuffer;
        indirect.canMultiDrawIndirect = params.canMultiDrawIndirect;
    }

    resources_.profiler->endCpuZone("Shadow:Setup");

    // Record all shadow cascades
    resources_.profiler->beginCpuZone("Shadow:Cascades");
    resources_.shadow->recordShadowPass(cmd, frameIndex, shadowDescriptorSet,
                                       allObjects,
                                       terrainCallback, grassCallback, treeCallback, skinnedCallback,
                                       preCascadeComputeCallback, indirect);
    resources_.profiler->endCpuZone("Shadow:Cascades");
}

// Legacy API implementation (deprecated)
void ShadowPassRecorder::record(vk::CommandBuffer cmd, uint32_t frameIndex, float time, const glm::vec3& cameraPosition) {
    // Convert legacy config to new params
    Params params;
    params.terrainEnabled = legacyConfig_.terrainEnabled;
    if (legacyConfig_.perfToggles) {
        params.terrainShadows = legacyConfig_.perfToggles->terrainShadows;
        params.grassShadows = legacyConfig_.perfToggles->grassShadows;
    }

    // Call the stateless version
    record(cmd, frameIndex, time, cameraPosition, params);
}
