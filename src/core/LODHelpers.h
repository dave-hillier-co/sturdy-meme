#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>

/**
 * LOD (Level of Detail) Helper Utilities
 *
 * Provides functions for distance-based LOD selection with hysteresis
 * to prevent flickering when objects are near LOD transition boundaries.
 *
 * Hysteresis works by using different thresholds for increasing vs
 * decreasing LOD levels. When moving closer (higher detail), we switch
 * at the normal threshold. When moving farther (lower detail), we require
 * distance to exceed threshold + hysteresis margin before switching.
 *
 * Example usage:
 *   LODSelector selector({100.0f, 500.0f, 1000.0f, 2000.0f}, 0.1f);
 *   uint32_t lod = selector.selectLOD(distance, currentLOD);
 */

/**
 * Configuration for LOD distance thresholds.
 * Each threshold defines the maximum distance for that LOD level.
 * LOD 0 is highest detail (closest), higher LOD = lower detail (farther).
 */
struct LODConfig {
    std::vector<float> thresholds;  // Max distance for each LOD level
    float hysteresisRatio = 0.1f;   // Hysteresis as ratio of threshold (0.1 = 10%)

    LODConfig() = default;

    LODConfig(std::initializer_list<float> dists, float hysteresis = 0.1f)
        : thresholds(dists), hysteresisRatio(hysteresis) {}

    LODConfig(std::vector<float> dists, float hysteresis = 0.1f)
        : thresholds(std::move(dists)), hysteresisRatio(hysteresis) {}

    uint32_t numLODLevels() const {
        return static_cast<uint32_t>(thresholds.size());
    }
};

// ============================================================================
// Free Functions - Stateless LOD utilities
// ============================================================================

/**
 * Select LOD level based on distance without hysteresis.
 * Simple threshold-based selection - use when hysteresis is not needed.
 *
 * @param distance Distance from camera/viewer
 * @param thresholds Max distance for each LOD level (ascending order)
 * @return LOD level (0 = highest detail, size = beyond all thresholds)
 */
inline uint32_t selectLODByDistance(float distance, const std::vector<float>& thresholds) {
    for (uint32_t i = 0; i < thresholds.size(); ++i) {
        if (distance < thresholds[i]) {
            return i;
        }
    }
    return static_cast<uint32_t>(thresholds.size());
}

/**
 * Select LOD level with hysteresis to prevent flickering.
 *
 * When transitioning to lower detail (higher LOD number), requires
 * distance to exceed threshold + hysteresis margin.
 * When transitioning to higher detail (lower LOD number), uses
 * the normal threshold.
 *
 * @param distance Current distance from camera/viewer
 * @param currentLOD Current LOD level
 * @param config LOD configuration with thresholds and hysteresis ratio
 * @return New LOD level
 */
inline uint32_t selectLODWithHysteresis(float distance, uint32_t currentLOD,
                                         const LODConfig& config) {
    if (config.thresholds.empty()) {
        return 0;
    }

    const uint32_t numLevels = config.numLODLevels();
    const float hysteresisRatio = config.hysteresisRatio;

    // Calculate target LOD without hysteresis
    uint32_t targetLOD = selectLODByDistance(distance, config.thresholds);

    // Apply hysteresis
    if (targetLOD > currentLOD) {
        // Moving to lower detail (farther away)
        // Require distance to exceed threshold + hysteresis
        uint32_t checkLOD = currentLOD;
        while (checkLOD < targetLOD && checkLOD < numLevels) {
            float threshold = config.thresholds[checkLOD];
            float hysteresisMargin = threshold * hysteresisRatio;
            if (distance < threshold + hysteresisMargin) {
                // Not far enough past threshold, stay at current
                return checkLOD;
            }
            ++checkLOD;
        }
        return targetLOD;
    } else if (targetLOD < currentLOD) {
        // Moving to higher detail (closer)
        // Use threshold - hysteresis for snap to higher detail
        uint32_t checkLOD = currentLOD;
        while (checkLOD > targetLOD && checkLOD > 0) {
            float threshold = config.thresholds[checkLOD - 1];
            float hysteresisMargin = threshold * hysteresisRatio;
            if (distance >= threshold - hysteresisMargin) {
                // Not close enough past threshold, stay at current
                return checkLOD;
            }
            --checkLOD;
        }
        return targetLOD;
    }

    return currentLOD;
}

/**
 * Calculate blend factor for smooth LOD transitions.
 * Returns 0.0 at fadeStart, 1.0 at fadeEnd, clamped to [0,1].
 *
 * @param distance Current distance
 * @param fadeStart Distance where fade begins
 * @param fadeEnd Distance where fade completes
 * @return Blend factor [0, 1]
 */
inline float calculateLODBlend(float distance, float fadeStart, float fadeEnd) {
    if (fadeEnd <= fadeStart) {
        return distance >= fadeStart ? 1.0f : 0.0f;
    }
    float t = (distance - fadeStart) / (fadeEnd - fadeStart);
    return std::clamp(t, 0.0f, 1.0f);
}

/**
 * Calculate smooth hermite blend factor for LOD transitions.
 * Uses smoothstep for more gradual transitions.
 *
 * @param distance Current distance
 * @param fadeStart Distance where fade begins
 * @param fadeEnd Distance where fade completes
 * @return Smooth blend factor [0, 1]
 */
inline float calculateLODBlendSmooth(float distance, float fadeStart, float fadeEnd) {
    float t = calculateLODBlend(distance, fadeStart, fadeEnd);
    // Smoothstep: 3t^2 - 2t^3
    return t * t * (3.0f - 2.0f * t);
}

/**
 * Get the distance range for a specific LOD level.
 *
 * @param lod LOD level to query
 * @param thresholds Distance thresholds
 * @param outMinDist Output minimum distance for this LOD
 * @param outMaxDist Output maximum distance for this LOD
 */
inline void getLODDistanceRange(uint32_t lod, const std::vector<float>& thresholds,
                                 float& outMinDist, float& outMaxDist) {
    outMinDist = (lod == 0) ? 0.0f : thresholds[lod - 1];
    outMaxDist = (lod < thresholds.size()) ? thresholds[lod]
                                            : std::numeric_limits<float>::max();
}

/**
 * Check if a distance falls within a specific LOD level's range.
 *
 * @param distance Distance to check
 * @param lod LOD level to check against
 * @param thresholds Distance thresholds
 * @return true if distance is within this LOD's range
 */
inline bool isWithinLODRange(float distance, uint32_t lod,
                             const std::vector<float>& thresholds) {
    float minDist, maxDist;
    getLODDistanceRange(lod, thresholds, minDist, maxDist);
    return distance >= minDist && distance < maxDist;
}

/**
 * Common LOD threshold presets for different use cases.
 */
namespace LODPresets {
    // Terrain LOD - large scale distances (matching TerrainTileCache)
    inline LODConfig terrain() {
        return LODConfig({1000.0f, 2000.0f, 4000.0f, 8000.0f}, 0.1f);
    }

    // Vegetation LOD - medium range
    inline LODConfig vegetation() {
        return LODConfig({50.0f, 150.0f, 400.0f, 1000.0f}, 0.15f);
    }

    // Props/Objects LOD - short to medium range
    inline LODConfig props() {
        return LODConfig({25.0f, 75.0f, 200.0f, 500.0f}, 0.1f);
    }

    // Characters LOD - detailed at close range
    inline LODConfig characters() {
        return LODConfig({10.0f, 30.0f, 80.0f, 200.0f}, 0.12f);
    }

    // UI/Effects - very close range
    inline LODConfig effects() {
        return LODConfig({5.0f, 15.0f, 40.0f, 100.0f}, 0.1f);
    }
}

// ============================================================================
// Screen-Space Error LOD Selection
// ============================================================================

/**
 * Configuration for screen-space error LOD selection.
 * Thresholds are minimum screen pixels for each LOD level (descending order).
 * LOD 0 requires the most pixels, higher LODs require fewer.
 */
struct ScreenSpaceLODConfig {
    std::vector<float> pixelThresholds;  // Min screen pixels for each LOD (descending)
    float hysteresisRatio = 0.1f;        // Hysteresis as ratio of threshold

    ScreenSpaceLODConfig() = default;

    ScreenSpaceLODConfig(std::initializer_list<float> pixels, float hysteresis = 0.1f)
        : pixelThresholds(pixels), hysteresisRatio(hysteresis) {}

    ScreenSpaceLODConfig(std::vector<float> pixels, float hysteresis = 0.1f)
        : pixelThresholds(std::move(pixels)), hysteresisRatio(hysteresis) {}

    uint32_t numLODLevels() const {
        return static_cast<uint32_t>(pixelThresholds.size());
    }
};

/**
 * Calculate projected screen size in pixels for an object.
 *
 * @param objectSize World-space size of the object (e.g., height, diameter)
 * @param distance Distance from camera to object
 * @param fovY Vertical field of view in radians
 * @param screenHeight Screen height in pixels
 * @return Projected size in screen pixels
 */
inline float calculateScreenSize(float objectSize, float distance,
                                  float fovY, float screenHeight) {
    if (distance <= 0.0f) {
        return std::numeric_limits<float>::max();
    }
    // projection factor = screenHeight / (2 * tan(fovY/2))
    float projectionFactor = screenHeight / (2.0f * std::tan(fovY * 0.5f));
    return (objectSize / distance) * projectionFactor;
}

/**
 * Calculate projected screen size using pre-computed projection factor.
 * Use this when calling repeatedly with the same FOV and screen height.
 *
 * @param objectSize World-space size of the object
 * @param distance Distance from camera to object
 * @param projectionFactor Pre-computed: screenHeight / (2 * tan(fovY/2))
 * @return Projected size in screen pixels
 */
inline float calculateScreenSizeFast(float objectSize, float distance,
                                      float projectionFactor) {
    if (distance <= 0.0f) {
        return std::numeric_limits<float>::max();
    }
    return (objectSize / distance) * projectionFactor;
}

/**
 * Compute the projection factor for screen-space calculations.
 * Cache this value when FOV and screen height don't change.
 *
 * @param fovY Vertical field of view in radians
 * @param screenHeight Screen height in pixels
 * @return Projection factor for use with calculateScreenSizeFast
 */
inline float computeProjectionFactor(float fovY, float screenHeight) {
    return screenHeight / (2.0f * std::tan(fovY * 0.5f));
}

/**
 * Select LOD level based on screen-space pixel size.
 * Thresholds should be in descending order (LOD0 needs most pixels).
 *
 * @param screenPixels Projected screen size in pixels
 * @param pixelThresholds Min pixels for each LOD level (descending order)
 * @return LOD level (0 = highest detail when object is large on screen)
 */
inline uint32_t selectLODByScreenSize(float screenPixels,
                                       const std::vector<float>& pixelThresholds) {
    for (uint32_t i = 0; i < pixelThresholds.size(); ++i) {
        if (screenPixels >= pixelThresholds[i]) {
            return i;
        }
    }
    return static_cast<uint32_t>(pixelThresholds.size());
}

/**
 * Select LOD level by screen size with hysteresis.
 *
 * When transitioning to lower detail (object getting smaller), requires
 * screen size to drop below threshold - hysteresis margin.
 * When transitioning to higher detail (object getting larger), uses
 * the normal threshold.
 *
 * @param screenPixels Projected screen size in pixels
 * @param currentLOD Current LOD level
 * @param config Screen-space LOD configuration
 * @return New LOD level
 */
inline uint32_t selectLODByScreenSizeWithHysteresis(float screenPixels,
                                                     uint32_t currentLOD,
                                                     const ScreenSpaceLODConfig& config) {
    if (config.pixelThresholds.empty()) {
        return 0;
    }

    const uint32_t numLevels = config.numLODLevels();
    const float hysteresisRatio = config.hysteresisRatio;

    // Calculate target LOD without hysteresis
    uint32_t targetLOD = selectLODByScreenSize(screenPixels, config.pixelThresholds);

    // Apply hysteresis
    if (targetLOD > currentLOD) {
        // Moving to lower detail (object getting smaller on screen)
        // Require pixels to drop below threshold - hysteresis
        uint32_t checkLOD = currentLOD;
        while (checkLOD < targetLOD && checkLOD < numLevels) {
            float threshold = config.pixelThresholds[checkLOD];
            float hysteresisMargin = threshold * hysteresisRatio;
            if (screenPixels > threshold - hysteresisMargin) {
                // Not small enough yet, stay at current
                return checkLOD;
            }
            ++checkLOD;
        }
        return targetLOD;
    } else if (targetLOD < currentLOD) {
        // Moving to higher detail (object getting larger on screen)
        // Require pixels to exceed threshold + hysteresis
        uint32_t checkLOD = currentLOD;
        while (checkLOD > targetLOD && checkLOD > 0) {
            float threshold = config.pixelThresholds[checkLOD - 1];
            float hysteresisMargin = threshold * hysteresisRatio;
            if (screenPixels < threshold + hysteresisMargin) {
                // Not large enough yet, stay at current
                return checkLOD;
            }
            --checkLOD;
        }
        return targetLOD;
    }

    return currentLOD;
}

/**
 * Common screen-space LOD presets.
 * Thresholds are minimum screen pixels (descending order).
 */
namespace ScreenSpaceLODPresets {
    // Buildings/large structures - need detail when prominent
    inline ScreenSpaceLODConfig buildings() {
        return ScreenSpaceLODConfig({200.0f, 100.0f, 50.0f, 20.0f}, 0.1f);
    }

    // Trees/vegetation - medium detail requirements
    inline ScreenSpaceLODConfig trees() {
        return ScreenSpaceLODConfig({80.0f, 40.0f, 20.0f, 8.0f}, 0.15f);
    }

    // Small props - lower detail thresholds
    inline ScreenSpaceLODConfig smallProps() {
        return ScreenSpaceLODConfig({40.0f, 20.0f, 10.0f, 4.0f}, 0.1f);
    }

    // Characters - high detail when visible
    inline ScreenSpaceLODConfig characters() {
        return ScreenSpaceLODConfig({150.0f, 80.0f, 40.0f, 15.0f}, 0.12f);
    }
}

/**
 * Screen-space LOD Selector with hysteresis support.
 * Maintains state and caches projection factor for efficient per-frame updates.
 */
class ScreenSpaceLODSelector {
public:
    ScreenSpaceLODSelector() = default;

    explicit ScreenSpaceLODSelector(const ScreenSpaceLODConfig& cfg)
        : config(cfg), currentLOD(0), projectionFactor(0.0f) {}

    ScreenSpaceLODSelector(std::initializer_list<float> thresholds, float hysteresisRatio = 0.1f)
        : config(thresholds, hysteresisRatio), currentLOD(0), projectionFactor(0.0f) {}

    /**
     * Update projection factor when FOV or screen size changes.
     * Call this once per frame or when camera parameters change.
     */
    void updateProjection(float fovY, float screenHeight) {
        projectionFactor = computeProjectionFactor(fovY, screenHeight);
    }

    /**
     * Select LOD level for an object based on its size and distance.
     *
     * @param objectSize World-space size of the object
     * @param distance Distance from camera to object
     * @return Selected LOD level (0 = highest detail)
     */
    uint32_t selectLOD(float objectSize, float distance) {
        float screenPixels = calculateScreenSizeFast(objectSize, distance, projectionFactor);
        currentLOD = selectLODByScreenSizeWithHysteresis(screenPixels, currentLOD, config);
        return currentLOD;
    }

    /**
     * Select LOD using pre-computed screen pixels.
     */
    uint32_t selectLODFromScreenSize(float screenPixels) {
        currentLOD = selectLODByScreenSizeWithHysteresis(screenPixels, currentLOD, config);
        return currentLOD;
    }

    uint32_t getCurrentLOD() const { return currentLOD; }
    void setCurrentLOD(uint32_t lod) { currentLOD = std::min(lod, config.numLODLevels()); }
    void reset() { currentLOD = 0; }

    const ScreenSpaceLODConfig& getConfig() const { return config; }
    ScreenSpaceLODConfig& getConfig() { return config; }
    float getProjectionFactor() const { return projectionFactor; }

private:
    ScreenSpaceLODConfig config;
    uint32_t currentLOD = 0;
    float projectionFactor = 0.0f;
};

// ============================================================================
// LODSelector Class - Stateful LOD selection with hysteresis
// ============================================================================

/**
 * LOD Selector with hysteresis support.
 * Maintains state to apply hysteresis when transitioning between LOD levels.
 */
class LODSelector {
public:
    LODSelector() = default;

    explicit LODSelector(const LODConfig& cfg)
        : config(cfg), currentLOD(0) {}

    LODSelector(std::initializer_list<float> thresholds, float hysteresisRatio = 0.1f)
        : config(thresholds, hysteresisRatio), currentLOD(0) {}

    /**
     * Select LOD level for a given distance with hysteresis.
     * Uses internal state to track current LOD for hysteresis calculation.
     *
     * @param distance Distance from camera/viewer
     * @return Selected LOD level (0 = highest detail)
     */
    uint32_t selectLOD(float distance) {
        currentLOD = selectLODWithHysteresis(distance, currentLOD, config);
        return currentLOD;
    }

    /**
     * Get the current LOD level without updating.
     */
    uint32_t getCurrentLOD() const { return currentLOD; }

    /**
     * Force set the current LOD (e.g., when object first becomes visible).
     */
    void setCurrentLOD(uint32_t lod) {
        currentLOD = std::min(lod, config.numLODLevels());
    }

    /**
     * Reset to highest detail LOD.
     */
    void reset() { currentLOD = 0; }

    /**
     * Get the config for inspection/modification.
     */
    const LODConfig& getConfig() const { return config; }
    LODConfig& getConfig() { return config; }

private:
    LODConfig config;
    uint32_t currentLOD = 0;
};
