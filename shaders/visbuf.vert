#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"

// Visibility buffer vertex shader
// Minimal pass: transform vertices, pass through instance/triangle IDs

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;     // unused but must match vertex layout
layout(location = 2) in vec2 inTexCoord;   // passed for alpha testing
layout(location = 3) in vec4 inTangent;    // unused
layout(location = 6) in vec4 inColor;      // unused

// Push constants: just need model matrix and instance ID
layout(push_constant) uniform VisBufPushConstants {
    mat4 model;
    uint instanceId;
    uint triangleOffset;   // base triangle index for this draw call
    float alphaTestThreshold;
    float _pad;
} visbuf;

layout(location = 0) flat out uint outInstanceId;
layout(location = 1) out vec2 outTexCoord;

void main() {
    vec4 worldPos = visbuf.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    outInstanceId = visbuf.instanceId;
    outTexCoord = inTexCoord;
}
