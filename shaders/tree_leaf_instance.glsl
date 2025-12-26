// Tree leaf instance data for GPU instancing
// Shared between vertex shaders and C++ (via generated UBOs.h pattern)
//
// Usage in shaders: #include "tree_leaf_instance.glsl"

#ifndef TREE_LEAF_INSTANCE_GLSL
#define TREE_LEAF_INSTANCE_GLSL

#include "quaternion.glsl"

// Per-leaf instance data - 32 bytes per leaf
// std430 layout for SSBO
struct LeafInstance {
    vec4 positionAndSize;   // xyz = world position, w = size
    vec4 orientation;       // quaternion (x, y, z, w)
};

// Quad vertices for a single leaf billboard (generated in vertex shader)
// Index 0-3 for first quad, 4-7 for second quad (double billboard)
const vec3 LEAF_QUAD_OFFSETS[4] = vec3[4](
    vec3(-0.5, 1.0, 0.0),   // Top-left
    vec3(-0.5, 0.0, 0.0),   // Bottom-left
    vec3( 0.5, 0.0, 0.0),   // Bottom-right
    vec3( 0.5, 1.0, 0.0)    // Top-right
);

const vec2 LEAF_QUAD_UVS[4] = vec2[4](
    vec2(0.0, 0.0),  // Top-left gets bottom of texture
    vec2(0.0, 1.0),  // Bottom-left gets top of texture
    vec2(1.0, 1.0),  // Bottom-right
    vec2(1.0, 0.0)   // Top-right
);

#endif // TREE_LEAF_INSTANCE_GLSL
