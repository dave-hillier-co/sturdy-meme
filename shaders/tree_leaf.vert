#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "tree_leaf_instance.glsl"

// Vertex attributes (matching shared leaf quad mesh)
layout(location = 0) in vec3 inPosition;  // Local quad position [-0.5, 0.5] x [0, 1] x [0, 0]
layout(location = 1) in vec3 inNormal;    // Default normal (not used - computed from orientation)
layout(location = 2) in vec2 inTexCoord;  // Quad UVs

// Leaf instance SSBO
layout(std430, binding = BINDING_TREE_GFX_LEAF_INSTANCES) readonly buffer LeafInstanceBuffer {
    LeafInstance leafInstances[];
};

// Wind uniform buffer
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    vec3 leafTint;
    float alphaTest;
    int firstInstance;  // Offset into leafInstances[] for this tree
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out float fragLeafSize;

void main() {
    // Get leaf instance data from SSBO
    int instanceIndex = push.firstInstance + gl_InstanceIndex;
    LeafInstance leaf = leafInstances[instanceIndex];

    vec3 leafPosition = leaf.positionAndSize.xyz;
    float leafSize = leaf.positionAndSize.w;
    vec4 orientation = leaf.orientation;

    // Scale the local quad position by leaf size
    vec3 scaledPos = inPosition * leafSize;

    // Rotate by leaf orientation quaternion
    vec3 rotatedPos = rotateByQuat(scaledPos, orientation);

    // Transform to world space: leaf position is in tree-local space
    vec3 treeLocalPos = leafPosition + rotatedPos;
    vec4 worldPos = push.model * vec4(treeLocalPos, 1.0);

    // Compute world-space pivot for wind animation
    vec4 worldPivot = push.model * vec4(leafPosition, 1.0);

    // Wind animation for leaves (matches ez-tree behavior)
    float windStrength = wind.windDirectionAndStrength.z;
    float windScale = wind.windParams.z;
    float windTime = wind.windParams.w;
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float gustFreq = wind.windParams.x;

    // Sample wind noise using pivot position so all vertices of this leaf get same wind
    float windOffset = 2.0 * 3.14159265 * simplex3(worldPivot.xyz / windScale);

    // Leaves sway more at tips (UV.y=0 = top of leaf) than at branch attachment (UV.y=1 = bottom)
    float swayFactor = 1.0 - inTexCoord.y;

    // Multi-frequency wind sway (matching ez-tree formula)
    vec3 windSway = swayFactor * windStrength * vec3(windDir.x, 0.0, windDir.y) * (
        0.5 * sin(windTime * gustFreq + windOffset) +
        0.3 * sin(2.0 * windTime * gustFreq + 1.3 * windOffset) +
        0.2 * sin(5.0 * windTime * gustFreq + 1.5 * windOffset)
    );

    worldPos.xyz += windSway;

    gl_Position = ubo.proj * ubo.view * worldPos;

    // Compute normal from orientation
    vec3 localNormal = vec3(0.0, 0.0, 1.0);
    vec3 rotatedNormal = rotateByQuat(localNormal, orientation);
    mat3 normalMatrix = mat3(push.model);
    fragNormal = normalize(normalMatrix * rotatedNormal);

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    fragLeafSize = leafSize;
}
