#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"

// Vertex attributes (matching Mesh::getAttributeDescriptions)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
// Locations 4, 5 are bone indices/weights (unused for leaves)
layout(location = 6) in vec4 inColor;  // RGB = leaf attachment point, A = leaf indicator (0.98)

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
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out float fragLeafSize;

void main() {
    vec3 localPos = inPosition;
    vec3 localNormal = inNormal;
    vec2 texCoord = inTexCoord;

    // Transform to world space
    vec4 worldPos = push.model * vec4(localPos, 1.0);

    // Get leaf pivot point (attachment point) from vertex color
    // Both quads of a double-billboard leaf share the same pivot
    vec3 leafPivot = inColor.rgb;
    vec4 worldPivot = push.model * vec4(leafPivot, 1.0);

    // Wind animation for leaves (matches ez-tree behavior)
    float windStrength = wind.windDirectionAndStrength.z;
    float windScale = wind.windParams.z;
    float windTime = wind.windParams.w;
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float gustFreq = wind.windParams.x;

    // Sample wind noise using pivot position so both quads get same wind
    float windOffset = 2.0 * 3.14159265 * simplex3(worldPivot.xyz / windScale);

    // Leaves sway more at tips (UV.y=0) than at branch attachment (UV.y=1)
    // Invert so tips move most, branch stays relatively fixed
    float swayFactor = 1.0 - texCoord.y;

    // Multi-frequency wind sway (matching ez-tree formula)
    vec3 windSway = swayFactor * windStrength * vec3(windDir.x, 0.0, windDir.y) * (
        0.5 * sin(windTime * gustFreq + windOffset) +
        0.3 * sin(2.0 * windTime * gustFreq + 1.3 * windOffset) +
        0.2 * sin(5.0 * windTime * gustFreq + 1.5 * windOffset)
    );

    worldPos.xyz += windSway;

    gl_Position = ubo.proj * ubo.view * worldPos;

    // Transform normal
    mat3 normalMatrix = mat3(push.model);
    fragNormal = normalize(normalMatrix * localNormal);
    fragTexCoord = texCoord;
    fragWorldPos = worldPos.xyz;
    fragLeafSize = 1.0;  // Could be derived from vertex data if needed
}
