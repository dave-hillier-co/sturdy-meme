#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "wind_animation_common.glsl"

// Vertex attributes (matching tree.vert)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 6) in vec4 inColor;

// Wind uniform buffer (same binding as tree.vert, shared descriptor set)
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;
} push;

void main() {
    vec3 localPos = inPosition;
    // Vertex color layout (precomputed during mesh generation):
    // R = total sway factor (inherited + own, precomputed from hierarchy)
    // A = branch level (0-0.95 for levels 0-3)
    float totalSwayFactor = inColor.r;
    float branchLevel = inColor.a * 3.0;  // Scale back to 0-3 range

    // Extract wind parameters using common function
    WindParams windParams = windExtractParams(wind.windDirectionAndStrength, wind.windParams);

    // Get tree base position for noise sampling
    vec3 treeBaseWorld = vec3(push.model[3][0], push.model[3][1], push.model[3][2]);

    // Calculate tree oscillation using GPU Gems 3 style animation
    TreeWindOscillation osc = windCalculateTreeOscillation(treeBaseWorld, windParams);

    // Apply precomputed sway factor - this includes both inherited and own sway
    float swayAmount = totalSwayFactor * windParams.strength;
    vec3 totalSway = osc.windDir3D * osc.mainBend * swayAmount +
                     osc.windPerp3D * osc.perpBend * swayAmount * 0.5;

    // Detail motion
    vec3 detailOffset = windCalculateDetailOffset(localPos, branchLevel, windParams.strength, windParams.gustFreq, windParams.time);

    // Transform to world space FIRST, then apply world-space wind offsets
    vec4 worldPos = push.model * vec4(localPos, 1.0);
    worldPos.xyz += totalSway + detailOffset;

    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * worldPos;
}
