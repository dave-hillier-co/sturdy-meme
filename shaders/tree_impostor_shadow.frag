#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in uint fragArchetypeIndex;

// Impostor atlas texture array for alpha testing
layout(binding = BINDING_TREE_IMPOSTOR_ALBEDO) uniform sampler2DArray albedoAlphaAtlas;

void main() {
    // Sample alpha from impostor atlas array using archetype as layer
    float alpha = texture(albedoAlphaAtlas, vec3(fragTexCoord, float(fragArchetypeIndex))).a;

    // Alpha test - discard transparent pixels
    if (alpha < 0.5) {
        discard;
    }

    // No color output needed - just depth
}
