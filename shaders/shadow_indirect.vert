#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "scene_instance_common.glsl"

// Vertex attributes (standard mesh format)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
// Locations 4, 5 are bone indices/weights (unused)
layout(location = 6) in vec4 inColor;

// GPU-driven indirect shadow draw: the model matrix comes from the shared scene instance
// SSBO (set 1), selected by gl_InstanceIndex (= firstInstance from the per-cascade cull
// pass). Only the cascade index varies per draw, passed as a push constant.
layout(push_constant) uniform PushConstants {
    uint cascadeIndex;
} push;

void main() {
    SceneInstance inst = sceneInstances[gl_InstanceIndex];
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * inst.model * vec4(inPosition, 1.0);
}
