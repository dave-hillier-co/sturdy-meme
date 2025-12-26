// World-space tree leaf instance data for GPU-driven single-draw rendering
// Shared between compute culling shader and vertex shader
//
// Usage in shaders: #include "tree_leaf_world.glsl"

#ifndef TREE_LEAF_WORLD_GLSL
#define TREE_LEAF_WORLD_GLSL

#include "quaternion.glsl"

// Per-leaf instance data in WORLD space - 48 bytes per leaf (std430)
// After compute culling, leaves are output in world-space with tree index
struct WorldLeafInstance {
    vec4 worldPosition;      // xyz = world position, w = size
    vec4 worldOrientation;   // world-space quaternion (x, y, z, w)
    uint treeIndex;          // Index into tree data SSBO
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

// Per-tree render data - stored in SSBO, indexed by treeIndex
// Contains everything needed for rendering that was previously in push constants
struct TreeRenderData {
    mat4 model;              // Tree model matrix (for normal transform, wind pivot)
    vec4 tintAndParams;      // rgb = leaf tint, a = autumn hue shift
    vec4 windPhaseAndLOD;    // x = wind phase offset, y = LOD blend factor, zw = reserved
};

#endif // TREE_LEAF_WORLD_GLSL
