#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "ubo_common.glsl"
#include "push_constants_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
// Locations 4, 5 are bone indices/weights (unused in non-skinned path)
layout(location = 6) in vec4 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec4 fragColor;

void main() {
    vec4 worldPos = material.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragNormal = mat3(material.model) * inNormal;
    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    // Transform tangent direction by model matrix, preserve handedness in w
    fragTangent = vec4(mat3(material.model) * inTangent.xyz, inTangent.w);
    fragColor = inColor;
}
