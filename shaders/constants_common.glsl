// Common constants shared across all shaders
// Prevent multiple inclusion
#ifndef CONSTANTS_COMMON_GLSL
#define CONSTANTS_COMMON_GLSL

const float PI = 3.14159265359;
#define NUM_CASCADES 4

// Planet geometry (fixed physical constants)
const float PLANET_RADIUS = 6371.0;
const float ATMOSPHERE_RADIUS = 6471.0;

// Light types
const uint LIGHT_TYPE_POINT = 0;
const uint LIGHT_TYPE_SPOT = 1;

// Maximum lights (must match CPU side)
const uint MAX_LIGHTS = 16;

#endif // CONSTANTS_COMMON_GLSL
