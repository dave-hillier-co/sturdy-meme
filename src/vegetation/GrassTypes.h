#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// Legacy push constants for non-tiled mode (and shadow pass)
struct GrassPushConstants {
    float time;
    int cascadeIndex;  // For shadow pass: which cascade we're rendering
};

// Push constants for tiled grass with continuous stochastic culling
// Tiles provide coarse culling, continuous distance-based culling handles density
struct TiledGrassPushConstants {
    float time;
    float tileOriginX;   // World X origin of this tile
    float tileOriginZ;   // World Z origin of this tile
    float tileSize;      // Tile size in world units
    float spacing;       // Blade spacing (always base spacing, no LOD multiplier)
    uint32_t tileIndex;  // Tile index for debugging
    float unused1;       // Padding
    float unused2;       // Padding
};

struct GrassInstance {
    glm::vec4 positionAndFacing;  // xyz = position, w = facing angle
    glm::vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
    glm::vec4 terrainNormal;      // xyz = terrain normal (for tangent alignment), w = unused
};
