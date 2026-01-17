#include "FrameUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"

// Systems
#include "WindSystem.h"
#include "GrassSystem.h"
#include "WeatherSystem.h"
#include "TerrainSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "LeafSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "WaterSystem.h"
#include "FroxelSystem.h"
#include "PostProcessSystem.h"
#include "ShadowSystem.h"
#include "GlobalBufferManager.h"
#include "EnvironmentSettings.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cmath>

void FrameUpdater::updateAllSystems(
    RendererSystems& systems,
    const FrameData& frame,
    VkExtent2D extent,
    const SnowConfig& snowConfig)
{
    systems.profiler().beginCpuZone("SystemUpdates");

    updateWind(systems, frame);
    updateTreeDescriptors(systems, frame);
    updateGrass(systems, frame);
    updateWeather(systems, frame);
    updateTerrain(systems, frame, snowConfig);
    updateSnow(systems, frame, snowConfig);
    updateLeaf(systems, frame);
    updateTreeLOD(systems, frame, extent);
    updateWater(systems, frame);

    systems.profiler().endCpuZone("SystemUpdates");
}

void FrameUpdater::updateWind(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("SystemUpdates:Wind");
    systems.wind().update(frame.deltaTime);
    systems.wind().updateUniforms(frame.frameIndex);
    systems.profiler().endCpuZone("SystemUpdates:Wind");
}

void FrameUpdater::updateTreeDescriptors(RendererSystems& systems, const FrameData& frame) {
    if (!systems.treeRenderer() || !systems.tree()) return;

    systems.profiler().beginCpuZone("SystemUpdates:TreeDesc");
    VkDescriptorBufferInfo windInfo = systems.wind().getBufferInfo(frame.frameIndex);

    // Update descriptor sets for each bark texture type
    for (const auto& barkType : systems.tree()->getBarkTextureTypes()) {
        Texture* barkTex = systems.tree()->getBarkTexture(barkType);
        Texture* barkNormal = systems.tree()->getBarkNormalMap(barkType);

        systems.treeRenderer()->updateBarkDescriptorSet(
            frame.frameIndex,
            barkType,
            systems.globalBuffers().uniformBuffers.buffers[frame.frameIndex],
            windInfo.buffer,
            systems.shadow().getShadowImageView(),
            systems.shadow().getShadowSampler(),
            barkTex->getImageView(),
            barkNormal->getImageView(),
            barkTex->getImageView(),  // roughness placeholder
            barkTex->getImageView(),  // AO placeholder
            barkTex->getSampler());
    }

    // Update descriptor sets for each leaf texture type
    for (const auto& leafType : systems.tree()->getLeafTextureTypes()) {
        Texture* leafTex = systems.tree()->getLeafTexture(leafType);

        systems.treeRenderer()->updateLeafDescriptorSet(
            frame.frameIndex,
            leafType,
            systems.globalBuffers().uniformBuffers.buffers[frame.frameIndex],
            windInfo.buffer,
            systems.shadow().getShadowImageView(),
            systems.shadow().getShadowSampler(),
            leafTex->getImageView(),
            leafTex->getSampler(),
            systems.tree()->getLeafInstanceBuffer(),
            systems.tree()->getLeafInstanceBufferSize(),
            systems.globalBuffers().snowBuffers.buffers[frame.frameIndex]);

        // Update culled leaf descriptor sets (for GPU culling path)
        systems.treeRenderer()->updateCulledLeafDescriptorSet(
            frame.frameIndex,
            leafType,
            systems.globalBuffers().uniformBuffers.buffers[frame.frameIndex],
            windInfo.buffer,
            systems.shadow().getShadowImageView(),
            systems.shadow().getShadowSampler(),
            leafTex->getImageView(),
            leafTex->getSampler(),
            systems.globalBuffers().snowBuffers.buffers[frame.frameIndex]);
    }
    systems.profiler().endCpuZone("SystemUpdates:TreeDesc");
}

void FrameUpdater::updateGrass(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("SystemUpdates:Grass");
    systems.grass().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj,
                               frame.terrainSize, frame.heightScale, frame.time);
    systems.grass().updateDisplacementSources(frame.playerPosition, frame.playerCapsuleRadius, frame.deltaTime);
    systems.profiler().endCpuZone("SystemUpdates:Grass");
}

void FrameUpdater::updateWeather(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("SystemUpdates:Weather");
    systems.weather().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj, frame.deltaTime, frame.time, systems.wind());
    systems.profiler().endCpuZone("SystemUpdates:Weather");

    // Connect weather to terrain liquid effects (composable material system)
    // Rain causes puddles and wet surfaces on terrain
    float rainIntensity = systems.weather().getIntensity();
    uint32_t weatherType = systems.weather().getWeatherType();
    if (weatherType == 0 && rainIntensity > 0.0f) {
        // Rain - enable terrain wetness
        systems.terrain().setLiquidWetness(rainIntensity);
    } else if (weatherType == 0) {
        // No rain - gradually dry out (handled by liquid system's natural state)
        systems.terrain().setLiquidWetness(0.0f);
    }
    // Note: Snow (type 1) doesn't cause wetness - it covers the ground instead
}

void FrameUpdater::updateTerrain(RendererSystems& systems, const FrameData& frame, const SnowConfig& snowConfig) {
    systems.profiler().beginCpuZone("SystemUpdates:Terrain");
    systems.terrain().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.view, frame.projection,
                                  systems.volumetricSnow().getCascadeParams(), snowConfig.useVolumetricSnow, snowConfig.maxSnowHeight);
    systems.profiler().endCpuZone("SystemUpdates:Terrain");
}

void FrameUpdater::updateSnow(RendererSystems& systems, const FrameData& frame, const SnowConfig& snowConfig) {
    systems.profiler().beginCpuZone("SystemUpdates:Snow");

    // Update snow mask system - accumulation/melting based on weather type
    bool isSnowing = (systems.weather().getWeatherType() == 1);  // 1 = snow
    float weatherIntensity = systems.weather().getIntensity();
    auto& envSettings = systems.environmentSettings();

    // Auto-adjust snow amount based on weather state
    if (isSnowing && weatherIntensity > 0.0f) {
        envSettings.snowAmount = glm::min(envSettings.snowAmount + envSettings.snowAccumulationRate * frame.deltaTime, 1.0f);
    } else if (envSettings.snowAmount > 0.0f) {
        envSettings.snowAmount = glm::max(envSettings.snowAmount - envSettings.snowMeltRate * frame.deltaTime, 0.0f);
    }
    systems.snowMask().setMaskCenter(frame.cameraPosition);
    systems.snowMask().updateUniforms(frame.frameIndex, frame.deltaTime, isSnowing, weatherIntensity, envSettings);

    // Update volumetric snow system
    systems.volumetricSnow().setCameraPosition(frame.cameraPosition);
    systems.volumetricSnow().setWindDirection(glm::vec2(systems.wind().getEnvironmentSettings().windDirection.x,
                                                     systems.wind().getEnvironmentSettings().windDirection.y));
    systems.volumetricSnow().setWindStrength(systems.wind().getEnvironmentSettings().windStrength);
    systems.volumetricSnow().updateUniforms(frame.frameIndex, frame.deltaTime, isSnowing, weatherIntensity, envSettings);

    // Add player footprint interaction with snow
    if (envSettings.snowAmount > 0.1f) {
        systems.snowMask().addInteraction(frame.playerPosition, frame.playerCapsuleRadius * 1.5f, 0.3f);
        systems.volumetricSnow().addInteraction(frame.playerPosition, frame.playerCapsuleRadius * 1.5f, 0.3f);
    }

    systems.profiler().endCpuZone("SystemUpdates:Snow");
}

void FrameUpdater::updateLeaf(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("SystemUpdates:Leaf");
    systems.leaf().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj, frame.cameraPosition, frame.playerVelocity, frame.deltaTime, frame.time,
                               frame.terrainSize, frame.heightScale);
    systems.profiler().endCpuZone("SystemUpdates:Leaf");
}

void FrameUpdater::updateTreeLOD(RendererSystems& systems, const FrameData& frame, VkExtent2D extent) {
    if (!systems.treeLOD() || !systems.tree()) return;

    systems.profiler().beginCpuZone("SystemUpdates:TreeLOD");

    // Enable GPU culling optimization when ImpostorCullSystem is available
    auto* impostorCull = systems.impostorCull();
    bool gpuCullingAvailable = impostorCull && impostorCull->getTreeCount() > 0;
    systems.treeLOD()->setGPUCullingEnabled(gpuCullingAvailable);

    // Compute screen params for screen-space error LOD
    TreeLODSystem::ScreenParams screenParams;
    screenParams.screenHeight = static_cast<float>(extent.height);
    // Extract tanHalfFOV from projection matrix: proj[1][1] = 1/tan(fov/2)
    // Note: Vulkan Y-flip makes proj[1][1] negative, so use abs()
    screenParams.tanHalfFOV = 1.0f / std::abs(frame.projection[1][1]);
    systems.treeLOD()->update(frame.deltaTime, frame.cameraPosition, *systems.tree(), screenParams);

    systems.profiler().endCpuZone("SystemUpdates:TreeLOD");
}

void FrameUpdater::updateWater(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("SystemUpdates:Water");
    systems.water().updateUniforms(frame.frameIndex);

    // Update underwater state for postprocess (Water Volume Renderer Phase 2)
    auto underwaterParams = systems.water().getUnderwaterParams(frame.cameraPosition);
    systems.postProcess().setUnderwaterState(
        underwaterParams.isUnderwater,
        underwaterParams.depth,
        underwaterParams.absorptionCoeffs,
        underwaterParams.turbidity,
        underwaterParams.waterColor,
        underwaterParams.waterLevel
    );

    // Update froxel system with underwater state for volumetric underwater fog
    systems.froxel().setWaterLevel(underwaterParams.waterLevel);
    systems.froxel().setUnderwaterEnabled(underwaterParams.isUnderwater);

    systems.profiler().endCpuZone("SystemUpdates:Water");
}
