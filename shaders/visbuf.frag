#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

// Visibility buffer fragment shader
// Writes packed (instanceID, triangleID) into a uint32 render target
//
// Packing format (32-bit):
//   Bits [31:23] = instanceId (9 bits, up to 512 instances)
//   Bits [22:0]  = triangleId (23 bits, up to ~8M triangles)
//
// triangleId = gl_PrimitiveID + triangleOffset

layout(location = 0) flat in uint inInstanceId;
layout(location = 1) in vec2 inTexCoord;

layout(push_constant) uniform VisBufPushConstants {
    mat4 model;
    uint instanceId;
    uint triangleOffset;
    float alphaTestThreshold;
    float _pad;
} visbuf;

// Optional: diffuse texture for alpha testing (transparent objects)
layout(binding = BINDING_DIFFUSE_TEX) uniform sampler2D diffuseTexture;

layout(location = 0) out uint outVisibility;

void main() {
    // Alpha test (for foliage/transparent objects)
    if (visbuf.alphaTestThreshold > 0.0) {
        float alpha = texture(diffuseTexture, inTexCoord).a;
        if (alpha < visbuf.alphaTestThreshold) {
            discard;
        }
    }

    uint triangleId = uint(gl_PrimitiveID) + visbuf.triangleOffset;

    // Pack: 9 bits instance (512 max), 23 bits triangle (~8M max)
    outVisibility = (inInstanceId << 23u) | (triangleId & 0x7FFFFFu);
}
