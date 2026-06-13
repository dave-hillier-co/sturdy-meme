#include "SceneComposition.h"

#include "RendererSystems.h"
#include "HDRDrawableAdapters.h"
#include "SceneObjectsDrawable.h"
#include "DebugLinesDrawable.h"
#include "WaterDrawable.h"

// Concrete system headers — RendererSystems only forward-declares these, but the
// drawable adapters upcast them to the IRecordable / IRecordableAnimated interfaces,
// which needs complete types.
#include "Profiler.h"
#include "PostProcessSystem.h"
#include "SkySystem.h"
#include "TerrainSystem.h"
#include "CatmullClarkSystem.h"
#include "GrassSystem.h"
#include "LeafSystem.h"
#include "WeatherSystem.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "DebugLineSystem.h"

namespace SceneComposition {

std::unique_ptr<HDRPassRecorder> buildHDRPassRecorder(
    RendererSystems& systems,
    RagdollDrawCallback ragdollDrawCallback) {
    auto recorder = std::make_unique<HDRPassRecorder>(
        systems.profiler(), systems.postProcess());

    // Virtual texture feedback: copy the GPU feedback buffer (written by the
    // terrain fragment shader during the HDR pass) to its CPU readback buffer
    recorder->setPostPassCallback([&systems](VkCommandBuffer cmd, uint32_t frameIndex) {
        if (systems.terrain().hasVirtualTexture() && systems.terrain().isTerrainEnabled()) {
            systems.terrain().recordVirtualTextureFeedbackCopy(cmd, frameIndex);
        }
    });

    // Draw order constants - controls rendering sequence within the HDR pass.
    // Slot assignment groups drawables for parallel secondary command buffer recording.
    // Slot 0: geometry base, Slot 1: scene meshes, Slot 2: effects/vegetation/debug

    // Slot 0: Sky + Terrain + Catmull-Clark (geometry base)
    recorder->registerDrawable(
        std::make_unique<RecordableDrawable>(systems.sky()),
        0, 0, "HDR:Sky");

    recorder->registerDrawable(
        std::make_unique<TerrainDrawable>(systems.terrain()),
        100, 0, "HDR:Terrain");

    recorder->registerDrawable(
        std::make_unique<RecordableDrawable>(systems.catmullClark()),
        200, 0, "HDR:CatmullClark");

    // Slot 1: Scene Objects + Skinned Characters (scene meshes)
    {
        SceneObjectsDrawable::Resources sceneRes;
        sceneRes.scene = &systems.scene();
        sceneRes.globalBuffers = &systems.globalBuffers();
        sceneRes.shadow = &systems.shadow();
        sceneRes.wind = &systems.wind();
        sceneRes.ecsWorld = systems.ecsWorld();
        sceneRes.rocks = &systems.rocks();
        sceneRes.detritus = systems.detritus();
        sceneRes.tree = systems.tree();
        sceneRes.treeRenderer = systems.treeRenderer();
        sceneRes.treeLOD = systems.treeLOD();
        sceneRes.impostorCull = systems.impostorCull();

        recorder->registerDrawable(
            std::make_unique<SceneObjectsDrawable>(sceneRes),
            300, 1, "HDR:SceneObjects");
    }

    {
        SkinnedCharDrawable::Resources charRes;
        charRes.scene = &systems.scene();
        charRes.skinnedMesh = &systems.skinnedMesh();
        charRes.npcRenderer = systems.npcRenderer();
        charRes.ragdollDrawCallback = std::move(ragdollDrawCallback);

        recorder->registerDrawable(
            std::make_unique<SkinnedCharDrawable>(charRes),
            400, 1, "HDR:SkinnedChar");
    }

    // Slot 2: Grass + Water + Leaves + Weather + Debug (effects/vegetation)
    recorder->registerDrawable(
        std::make_unique<AnimatedRecordableDrawable>(systems.grass()),
        500, 2, "HDR:Grass");

    recorder->registerDrawable(
        std::make_unique<WaterDrawable>(
            systems.water(),
            systems.hasWaterTileCull() ? &systems.waterTileCull() : nullptr),
        600, 2, "HDR:Water");

    recorder->registerDrawable(
        std::make_unique<AnimatedRecordableDrawable>(systems.leaf()),
        700, 2, "HDR:Leaves");

    recorder->registerDrawable(
        std::make_unique<AnimatedRecordableDrawable>(systems.weather()),
        800, 2, "HDR:Weather");

    recorder->registerDrawable(
        std::make_unique<DebugLinesDrawable>(systems.debugLine(), systems.postProcess()),
        900, 2, "HDR:DebugLines");

    return recorder;
}

} // namespace SceneComposition
