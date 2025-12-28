#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "wind_animation_common.glsl"
#include "tree_leaf_world.glsl"

// Vertex attributes (matching shared leaf quad mesh)
layout(location = 0) in vec3 inPosition;  // Local quad position [-0.5, 0.5] x [0, 1] x [0, 0]
layout(location = 1) in vec3 inNormal;    // Default normal (not used - computed from orientation)
layout(location = 2) in vec2 inTexCoord;  // Quad UVs

// World-space leaf instance SSBO (from compute culling)
layout(std430, binding = BINDING_TREE_GFX_LEAF_INSTANCES) readonly buffer LeafInstanceBuffer {
    WorldLeafInstance leafInstances[];
};

// Tree render data SSBO (transforms, tints, etc.)
layout(std430, binding = BINDING_TREE_GFX_TREE_DATA) readonly buffer TreeDataBuffer {
    TreeRenderData treeData[];
};

// Wind uniform buffer
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

// Simplified push constants - no more per-tree data
layout(push_constant) uniform PushConstants {
    float time;
    float alphaTest;
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out float fragLeafSize;
layout(location = 4) out vec3 fragLeafTint;
layout(location = 5) out float fragAutumnHueShift;
layout(location = 6) out float fragLodBlendFactor;

void main() {
    // Get world-space leaf instance data from SSBO
    WorldLeafInstance leaf = leafInstances[gl_InstanceIndex];

    vec3 worldPosition = leaf.worldPosition.xyz;
    float leafSize = leaf.worldPosition.w;
    vec4 worldOrientation = leaf.worldOrientation;
    uint treeIndex = leaf.treeIndex;

    // Get tree render data
    TreeRenderData tree = treeData[treeIndex];
    vec3 leafTint = tree.tintAndParams.rgb;
    float autumnHueShift = tree.tintAndParams.a;
    float windPhaseOffset = tree.windPhaseAndLOD.x;

    // Extract wind parameters using common function
    WindParams windParams = windExtractParams(wind.windDirectionAndStrength, wind.windParams);

    // Get tree base position from model matrix
    vec3 treeBaseWorld = vec3(tree.model[3][0], tree.model[3][1], tree.model[3][2]);

    // Calculate tree oscillation (same as tree.vert for consistency)
    TreeWindOscillation osc = windCalculateTreeOscillation(treeBaseWorld, windParams);

    // Leaf height relative to tree base
    float leafHeight = worldPosition.y - treeBaseWorld.y;

    // Hierarchical offset from tree sway (leaves move with branches)
    vec3 branchSway = windCalculateBranchSwayInfluence(osc, leafHeight, windParams.strength);

    // Detail leaf motion using common oscillation calculation
    LeafWindOscillation leafOsc = windCalculateLeafOscillation(worldPosition, windParams, windPhaseOffset);
    float oscillation = leafOsc.combined;

    // Leaves sway more at tips (UV.y=0 = top of leaf) than at branch attachment (UV.y=1 = bottom)
    float swayFactor = 1.0 - inTexCoord.y;

    // === Leaf Rotation/Tilt from Wind Pressure (GPU Gems 3) ===
    // Wind pushes leaf to rotate around its attachment point
    // Tilt axis is perpendicular to wind direction (leaves fold over)
    vec3 tiltAxis = normalize(cross(osc.windDir3D, vec3(0.0, 1.0, 0.0)));
    float tiltAngle = oscillation * windParams.strength * 0.4;  // Up to ~23 degrees

    // Also add some twist around leaf's up axis for flutter
    float twistAngle = (leafOsc.osc2 * 0.3 + leafOsc.osc3 * 0.2) * windParams.strength * 0.3;

    // Create rotation quaternions
    vec4 tiltQuat = quatFromAxisAngle(tiltAxis, tiltAngle);

    // Twist around leaf's local up (approximated as world Y rotated by orientation)
    vec3 leafUp = rotateByQuat(vec3(0.0, 1.0, 0.0), worldOrientation);
    vec4 twistQuat = quatFromAxisAngle(leafUp, twistAngle);

    // Combine rotations: first tilt, then twist
    vec4 windRotation = quatMul(twistQuat, tiltQuat);

    // Apply wind rotation to leaf orientation
    vec4 animatedOrientation = quatMul(windRotation, worldOrientation);

    // Scale the local quad position by leaf size
    vec3 scaledPos = inPosition * leafSize;

    // Rotate by animated orientation quaternion
    vec3 rotatedPos = rotateByQuat(scaledPos, animatedOrientation);

    // Apply position sway (reduced since rotation now handles much of the motion)
    vec3 leafSway = swayFactor * windParams.strength * osc.windDir3D * oscillation * 0.3;

    // Combine all motion: base position + branch sway + leaf sway + rotated offset
    vec3 worldPos = worldPosition + branchSway + leafSway + rotatedPos;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Compute normal from animated orientation
    vec3 localNormal = vec3(0.0, 0.0, 1.0);
    vec3 worldNormal = rotateByQuat(localNormal, animatedOrientation);
    fragNormal = normalize(worldNormal);

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos;
    fragLeafSize = leafSize;
    fragLeafTint = leafTint;
    fragAutumnHueShift = autumnHueShift;
    fragLodBlendFactor = tree.windPhaseAndLOD.y;
}
