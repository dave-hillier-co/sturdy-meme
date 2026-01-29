#include "EnvironmentUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"

#include "TerrainSystem.h"
#include "WaterSystem.h"
#include "WeatherSystem.h"
#include "VolumetricSnowSystem.h"
#include "PostProcessSystem.h"
#include "FroxelSystem.h"

void EnvironmentUpdater::update(RendererSystems& systems, const FrameData& frame, const Config& config) {
    connectWeatherToTerrain(systems);
    updateTerrain(systems, frame, config);
    updateWater(systems, frame);
}

void EnvironmentUpdater::connectWeatherToTerrain(RendererSystems& systems) {
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

void EnvironmentUpdater::updateTerrain(RendererSystems& systems, const FrameData& frame, const Config& config) {
    systems.profiler().beginCpuZone("Update:Terrain");
    systems.terrain().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.view, frame.projection,
                                     systems.volumetricSnow().getCascadeParams(),
                                     config.useVolumetricSnow, config.maxSnowHeight);
    systems.profiler().endCpuZone("Update:Terrain");
}

void EnvironmentUpdater::updateWater(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("Update:Water");
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

    // Pass water optical properties (RGB absorption from WaterSystem)
    systems.froxel().setWaterAbsorption(underwaterParams.absorptionCoeffs);

    // Derive scattering from turbidity (higher turbidity = more scattering)
    glm::vec3 scattering = glm::vec3(underwaterParams.turbidity * 0.5f);
    systems.froxel().setWaterScattering(scattering);

    // Get max wave amplitude for surface transition zone
    float waveAmp = systems.water().getWaveAmplitude();
    systems.froxel().setMaxWaveAmplitude(waveAmp);

    systems.profiler().endCpuZone("Update:Water");
}
