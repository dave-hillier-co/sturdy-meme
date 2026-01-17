#pragma once

#include "FrameData.h"
#include <glm/glm.hpp>
#include <cstdint>

class RendererSystems;
class GlobalBufferManager;
struct VkExtent2D;

/**
 * FrameUpdater - Handles per-frame subsystem updates
 *
 * Extracted from Renderer::render() to reduce complexity and improve testability.
 * Consolidates all the subsystem update calls that happen each frame.
 *
 * This class is stateless - all state comes from the systems it updates.
 */
class FrameUpdater {
public:
    /**
     * Configuration for snow accumulation behavior
     */
    struct SnowConfig {
        float maxSnowHeight = 0.3f;
        bool useVolumetricSnow = true;
    };

    /**
     * Update all subsystems for the current frame
     *
     * @param systems Reference to all renderer subsystems
     * @param frame The current frame's data (camera, timing, player state, etc.)
     * @param extent The current viewport extent
     * @param snowConfig Snow behavior configuration
     */
    static void updateAllSystems(
        RendererSystems& systems,
        const FrameData& frame,
        VkExtent2D extent,
        const SnowConfig& snowConfig
    );

private:
    // Individual system update helpers
    static void updateWind(RendererSystems& systems, const FrameData& frame);
    static void updateTreeDescriptors(RendererSystems& systems, const FrameData& frame);
    static void updateGrass(RendererSystems& systems, const FrameData& frame);
    static void updateWeather(RendererSystems& systems, const FrameData& frame);
    static void updateTerrain(RendererSystems& systems, const FrameData& frame, const SnowConfig& snowConfig);
    static void updateSnow(RendererSystems& systems, const FrameData& frame, const SnowConfig& snowConfig);
    static void updateLeaf(RendererSystems& systems, const FrameData& frame);
    static void updateTreeLOD(RendererSystems& systems, const FrameData& frame, VkExtent2D extent);
    static void updateWater(RendererSystems& systems, const FrameData& frame);
};
