#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "tree_leaf_instance.glsl"

// Vertex attributes (matching shared leaf quad mesh)
layout(location = 0) in vec3 inPosition;  // Local quad position
layout(location = 2) in vec2 inTexCoord;  // Quad UVs

// Leaf instance SSBO
layout(std430, binding = BINDING_TREE_GFX_LEAF_INSTANCES) readonly buffer LeafInstanceBuffer {
    LeafInstance leafInstances[];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;
    float alphaTest;
    int firstInstance;  // Offset into leafInstances[] for this tree
} push;

layout(location = 0) out vec2 fragTexCoord;

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

    // Transform to world space
    vec3 treeLocalPos = leafPosition + rotatedPos;
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * vec4(treeLocalPos, 1.0);

    fragTexCoord = inTexCoord;
}
