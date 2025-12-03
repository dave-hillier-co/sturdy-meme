#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_meshlet_shadow.vert - Meshlet-based terrain shadow pass
 * Simplified version for shadow map rendering using instanced meshlets
 * Height convention: see terrain_height_common.glsl
 */

#define CBT_BUFFER_BINDING 0
#include "cbt.glsl"
#include "leb.glsl"
#include "../terrain_height_common.glsl"

// Meshlet vertex input: position in unit triangle space
layout(location = 0) in vec2 inMeshletPos;

// Height map
layout(binding = 3) uniform sampler2D heightMap;

// Push constants for per-cascade matrix
layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    float terrainSize;
    float heightScale;
    int cascadeIndex;
    int padding;
};

void main() {
    // Get the CBT leaf index from instance
    uint cbtLeafIndex = uint(gl_InstanceIndex);

    // Map leaf index to heap index
    cbt_Node node = cbt_DecodeNode(cbtLeafIndex);

    // Decode parent triangle vertices in UV space
    vec2 parentV0, parentV1, parentV2;
    leb_DecodeTriangleVertices(node, parentV0, parentV1, parentV2);

    // Transform meshlet local position to parent triangle UV space
    float s = inMeshletPos.x;
    float t = inMeshletPos.y;
    vec2 uv = parentV0 * (1.0 - s - t) + parentV1 * s + parentV2 * t;

    // Sample height using shared function (terrain_height_common.glsl)
    float height = sampleTerrainHeight(heightMap, uv, heightScale);

    // Compute world position
    vec3 worldPos = vec3(
        (uv.x - 0.5) * terrainSize,
        height,
        (uv.y - 0.5) * terrainSize
    );

    // Transform to light space
    gl_Position = lightViewProj * vec4(worldPos, 1.0);
}
