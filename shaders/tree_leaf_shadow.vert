#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "wind_animation_common.glsl"
#include "tree_leaf_world.glsl"

// Vertex attributes (matching shared leaf quad mesh)
layout(location = 0) in vec3 inPosition;  // Local quad position
layout(location = 2) in vec2 inTexCoord;  // Quad UVs

// World-space leaf instance SSBO (from compute culling)
layout(std430, binding = BINDING_TREE_GFX_LEAF_INSTANCES) readonly buffer LeafInstanceBuffer {
    WorldLeafInstance leafInstances[];
};

// Tree render data SSBO (transforms, tints, etc.)
layout(std430, binding = BINDING_TREE_GFX_TREE_DATA) readonly buffer TreeDataBuffer {
    TreeRenderData treeData[];
};

// Wind uniform buffer (same binding as tree_leaf.vert, shared descriptor set)
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

// Simplified push constants - no more per-tree data
layout(push_constant) uniform PushConstants {
    int cascadeIndex;
    float alphaTest;
} push;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Get world-space leaf instance data from SSBO
    WorldLeafInstance leaf = leafInstances[gl_InstanceIndex];

    vec3 worldPosition = leaf.worldPosition.xyz;
    float leafSize = leaf.worldPosition.w;
    vec4 worldOrientation = leaf.worldOrientation;
    uint treeIndex = leaf.treeIndex;

    // Get tree render data
    TreeRenderData tree = treeData[treeIndex];
    float windPhaseOffset = tree.windPhaseAndLOD.x;

    // Extract wind parameters using common function
    WindParams windParams = windExtractParams(wind.windDirectionAndStrength, wind.windParams);

    // Get tree base position from model matrix
    vec3 treeBaseWorld = vec3(tree.model[3][0], tree.model[3][1], tree.model[3][2]);

    // Calculate tree oscillation (same as tree_leaf.vert for consistency)
    TreeWindOscillation osc = windCalculateTreeOscillation(treeBaseWorld, windParams);

    // Leaf height relative to tree base
    float leafHeight = worldPosition.y - treeBaseWorld.y;

    // Hierarchical offset from tree sway (leaves move with branches)
    vec3 branchSway = windCalculateBranchSwayInfluence(osc, leafHeight, windParams.strength);

    // Detail leaf motion using common oscillation calculation
    LeafWindOscillation leafOsc = windCalculateLeafOscillation(worldPosition, windParams, windPhaseOffset);
    float oscillation = leafOsc.combined;
    float swayFactor = 1.0 - inTexCoord.y;

    // === Leaf Rotation/Tilt ===
    vec3 tiltAxis = normalize(cross(osc.windDir3D, vec3(0.0, 1.0, 0.0)));
    float tiltAngle = oscillation * windParams.strength * 0.4;
    float twistAngle = (leafOsc.osc2 * 0.3 + leafOsc.osc3 * 0.2) * windParams.strength * 0.3;

    vec4 tiltQuat = quatFromAxisAngle(tiltAxis, tiltAngle);
    vec3 leafUp = rotateByQuat(vec3(0.0, 1.0, 0.0), worldOrientation);
    vec4 twistQuat = quatFromAxisAngle(leafUp, twistAngle);
    vec4 windRotation = quatMul(twistQuat, tiltQuat);
    vec4 animatedOrientation = quatMul(windRotation, worldOrientation);

    // Scale and rotate
    vec3 scaledPos = inPosition * leafSize;
    vec3 rotatedPos = rotateByQuat(scaledPos, animatedOrientation);

    // Position sway
    vec3 leafSway = swayFactor * windParams.strength * osc.windDir3D * oscillation * 0.3;

    // Combine all motion
    vec3 worldPos = worldPosition + branchSway + leafSway + rotatedPos;

    // Transform to shadow map space
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);

    fragTexCoord = inTexCoord;
}
