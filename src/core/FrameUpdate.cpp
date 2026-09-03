#include "FrameUpdate.h"

#include "RendererSystems.h"
#include "FrameDataBuilder.h"
#include "FrameUpdater.h"
#include "updaters/UBOUpdater.h"
#include "TimeSystem.h"
#include "SkinnedMeshRenderer.h"
#include "SceneManager.h"
#include "SceneBuilder.h"
#include "AnimatedCharacter.h"
#include "vulkan/AsyncTransferManager.h"
#include "Camera.h"

FrameUpdate::Result FrameUpdate::run(RendererSystems& systems,
                                     AsyncTransferManager* transferManager,
                                     const Camera& camera,
                                     uint32_t frameIndex,
                                     VkExtent2D extent,
                                     const Config& config) {
    Result out;

    if (transferManager) transferManager->processPendingTransfers();

    // Per-frame data updates
    TimingData timing = systems.time().update();

    UBOUpdater::Config uboConfig;
    uboConfig.useVolumetricSnow = config.useVolumetricSnow;
    uboConfig.shadowsEnabled = config.shadowsEnabled;
    uboConfig.maxSnowHeight = config.maxSnowHeight;
    uboConfig.lightCullRadius = config.lightCullRadius;
    uboConfig.ecsWorld = config.ecsWorld;
    uboConfig.deltaTime = timing.deltaTime;
    auto uboResult = UBOUpdater::update(systems, frameIndex, camera, uboConfig);
    out.sunIntensity = uboResult.sunIntensity;

    SceneBuilder& sceneBuilder = systems.scene().getSceneBuilder();
    AnimatedCharacter* character = sceneBuilder.hasCharacter() ? &sceneBuilder.getAnimatedCharacter() : nullptr;
    constexpr uint32_t PLAYER_BONE_SLOT = 0;
    systems.skinnedMesh().updateBoneMatrices(frameIndex, PLAYER_BONE_SLOT, character);

    // Build per-frame shared state
    out.frame = FrameDataBuilder::buildFrameData(
        camera, systems, frameIndex, timing.deltaTime, timing.elapsedTime);

    // Subsystem updates
    FrameUpdater::updateDebugLines(systems, frameIndex);

    FrameUpdater::SnowConfig snowConfig;
    snowConfig.maxSnowHeight = config.maxSnowHeight;
    snowConfig.useVolumetricSnow = config.useVolumetricSnow;
    FrameUpdater::updateAllSystems(systems, out.frame, extent, snowConfig);

    FrameUpdater::populateGPUSceneBuffer(systems, out.frame);

    return out;
}
