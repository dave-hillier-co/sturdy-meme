#include "FrameUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"
#include "SceneManager.h"
#include "GPUSceneBuffer.h"
#include "vegetation/ScatterSystem.h"
#include "DebugLineSystem.h"
#include "controls/DebugControlSubsystem.h"

#include "updaters/VegetationUpdater.h"
#include "updaters/AtmosphereUpdater.h"
#include "updaters/EnvironmentUpdater.h"

void FrameUpdater::updateAllSystems(
    RendererSystems& systems,
    const FrameData& frame,
    VkExtent2D extent,
    const SnowConfig& snowConfig)
{
    systems.profiler().beginCpuZone("SystemUpdates");

    // Atmosphere updates first (wind affects vegetation)
    AtmosphereUpdater::SnowConfig atmosSnowConfig;
    atmosSnowConfig.maxSnowHeight = snowConfig.maxSnowHeight;
    atmosSnowConfig.useVolumetricSnow = snowConfig.useVolumetricSnow;
    AtmosphereUpdater::update(systems, frame, atmosSnowConfig);

    // Environment updates (terrain/water)
    EnvironmentUpdater::Config envConfig;
    envConfig.maxSnowHeight = snowConfig.maxSnowHeight;
    envConfig.useVolumetricSnow = snowConfig.useVolumetricSnow;
    EnvironmentUpdater::update(systems, frame, envConfig);

    // Vegetation updates last (depends on wind)
    VegetationUpdater::update(systems, frame, extent);

    systems.profiler().endCpuZone("SystemUpdates");
}

void FrameUpdater::populateGPUSceneBuffer(RendererSystems& systems, const FrameData& frame) {
    if (!systems.hasGPUSceneBuffer()) return;

    systems.profiler().beginCpuZone("GPUSceneBuffer");
    GPUSceneBuffer& sceneBuffer = systems.gpuSceneBuffer();
    sceneBuffer.beginFrame(frame.frameIndex);

    // Scene objects are sourced from the ECS: each scene entity's render data is
    // extracted from its components (Transform/MeshRef/MaterialRef/PBRProperties/...).
    // GPU-skinned characters (player + NPCs) draw via a separate pipeline and are
    // skipped via the GPUSkinned tag.
    ecs::World* world = systems.ecsWorld();
    if (world) {
        const auto& sceneEntities = systems.scene().getSceneBuilder().getSceneEntities();
        for (ecs::Entity entity : sceneEntities) {
            if (!world->valid(entity)) continue;
            if (world->has<ecs::GPUSkinned>(entity)) continue;
            sceneBuffer.addObject(ecs::extractRenderData(*world, entity));
        }

#ifndef NDEBUG
        // One-shot equivalence check vs the legacy Renderable path: the ECS query
        // must select exactly the non-gpuSkinned scene objects.
        static bool verified = false;
        if (!verified && !sceneEntities.empty()) {
            verified = true;
            const auto& objs = systems.scene().getRenderables();
            size_t legacyCount = 0;
            for (const auto& o : objs) if (!o.gpuSkinned) ++legacyCount;
            size_t ecsCount = 0;
            for (ecs::Entity e : sceneEntities)
                if (world->valid(e) && !world->has<ecs::GPUSkinned>(e)) ++ecsCount;
            if (ecsCount != legacyCount) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "GPUSceneBuffer: ECS scene-object count %zu != legacy non-skinned count %zu",
                    ecsCount, legacyCount);
            } else {
                SDL_Log("GPUSceneBuffer: ECS scene-object query matches legacy set (%zu objects)",
                    ecsCount);
            }
        }
#endif
    }

    // Scatter content (rocks, detritus). These have no MaterialRegistry materialId but own
    // a single descriptor set per frame on the same layout, so they ride the indirect path
    // via a per-object descriptor override. When indirect draw is active, SceneObjectsDrawable
    // skips its CPU rocks/detritus loops to avoid double-drawing.
    auto addScatter = [&](const ScatterSystem& scatter) {
        if (!scatter.hasDescriptorSets()) return;
        vk::DescriptorSet set = scatter.getDescriptorSet(frame.frameIndex);
        if (!set) return;
        for (const auto& obj : scatter.getSceneObjects()) {
            if (obj.gpuSkinned) continue;
            sceneBuffer.addObject(obj, set);
        }
    };
    addScatter(systems.rocks());
    if (systems.detritus()) {
        addScatter(*systems.detritus());
    }

    sceneBuffer.finalize();
    systems.profiler().endCpuZone("GPUSceneBuffer");
}

void FrameUpdater::updateDebugLines(RendererSystems& systems, uint32_t frameIndex) {
    // Begin debug line frame if not already started by physics debug
    if (!systems.debugLine().hasLines()) {
        systems.debugLine().beginFrame(frameIndex);
    }

    // Add road/river visualization to debug lines
    systems.debugControlSubsystem().updateRoadRiverVisualization();

    // Upload debug lines if any are present
    if (systems.debugLine().hasLines()) {
        systems.debugLine().uploadLines();
    }
}
