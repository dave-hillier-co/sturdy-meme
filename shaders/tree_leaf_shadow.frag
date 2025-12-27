#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "dither_common.glsl"

layout(binding = BINDING_TREE_GFX_LEAF_ALBEDO) uniform sampler2D leafAlbedo;

// Simplified push constants - no more per-tree data
layout(push_constant) uniform PushConstants {
    int cascadeIndex;
    float alphaTest;
} push;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in float fragLodBlendFactor;

void main() {
    // LOD dithered fade-out - must match main pass to prevent shadow artifacts
    // Leaves that are dithered out in main view should not cast shadows
    if (shouldDiscardForLODLeaves(fragLodBlendFactor)) {
        discard;
    }

    // Sample leaf texture for alpha test
    vec4 albedo = texture(leafAlbedo, fragTexCoord);

    // Discard transparent pixels
    if (albedo.a < push.alphaTest) {
        discard;
    }

    // Shadow map only needs depth (written automatically)
}
