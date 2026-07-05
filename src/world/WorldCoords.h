#pragma once

#include <glm/glm.hpp>

// Coordinate conventions shared by all world-content systems.
//
// Content space: the space used by the offline generators (biome_preprocess,
// road_generator, watershed) - X/Z in [0, kTerrainSize].
// World space: the runtime rendering/physics space - X/Z centered on the
// terrain, in [-kTerrainSize/2, +kTerrainSize/2].
namespace WorldCoords {

inline constexpr float kTerrainSize = 16384.0f;
inline constexpr float kHalfTerrain = kTerrainSize * 0.5f;

inline glm::vec2 contentToWorld(glm::vec2 content) {
    return content - kHalfTerrain;
}

inline glm::vec2 worldToContent(glm::vec2 world) {
    return world + kHalfTerrain;
}

} // namespace WorldCoords
