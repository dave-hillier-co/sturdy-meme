#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"

// Vertex attributes (matching tree.vert)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 6) in vec4 inColor;

// Instance data from SSBO
struct BranchShadowInstance {
    mat4 model;
};

layout(std430, binding = BINDING_TREE_GFX_BRANCH_SHADOW_INSTANCES) readonly buffer InstanceBuffer {
    BranchShadowInstance instances[];
};

layout(push_constant) uniform PushConstants {
    uint cascadeIndex;
    uint instanceOffset; // Base offset for this mesh group in SSBO
} push;

void main() {
    uint instanceIdx = push.instanceOffset + gl_InstanceIndex;
    mat4 model = instances[instanceIdx].model;
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * model * vec4(inPosition, 1.0);
}
