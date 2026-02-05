#include "FrameUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"
#include "SceneManager.h"
#include "SceneBuilder.h"
#include "GPUSceneBuffer.h"
#include "DebugLineSystem.h"
#include "npc/NPCSimulation.h"
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

    const auto& sceneObjects = systems.scene().getRenderables();
    SceneBuilder& sceneBuilder = systems.scene().getSceneBuilder();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    bool hasCharacter = sceneBuilder.hasCharacter();
    const NPCSimulation* npcSim = sceneBuilder.getNPCSimulation();

    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip player character (rendered with GPU skinning)
        if (hasCharacter && i == playerIndex) continue;

        // Skip NPC characters (rendered with GPU skinning)
        if (npcSim) {
            bool isNPC = false;
            const auto& npcData = npcSim->getData();
            for (size_t npcIdx = 0; npcIdx < npcData.count(); ++npcIdx) {
                if (i == npcData.renderableIndices[npcIdx]) {
                    isNPC = true;
                    break;
                }
            }
            if (isNPC) continue;
        }

        sceneBuffer.addObject(sceneObjects[i]);
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
