// Common push constants definition - include this in all shaders using material push constants
// This ensures the struct stays in sync between vertex and fragment shaders

#ifndef PUSH_CONSTANTS_COMMON_GLSL
#define PUSH_CONSTANTS_COMMON_GLSL

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float opacity;  // For camera occlusion fading (1.0 = fully visible)
    vec4 emissiveColor;
} material;

#endif // PUSH_CONSTANTS_COMMON_GLSL
