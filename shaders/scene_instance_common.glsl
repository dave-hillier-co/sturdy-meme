// Scene instance data for batched rendering
// Include this in instanced shaders to access per-instance data via gl_InstanceIndex
//
// Usage:
//   #include "scene_instance_common.glsl"
//   ...
//   SceneInstance inst = sceneInstances[gl_InstanceIndex];
//   mat4 model = inst.model;

#ifndef SCENE_INSTANCE_COMMON_GLSL
#define SCENE_INSTANCE_COMMON_GLSL

#include "bindings.glsl"
#include "pbr_flags_common.glsl"

// Per-instance data for scene objects
// Must match GPUSceneInstanceData in src/core/GPUSceneBuffer.h (std430 layout, 112 bytes)
struct SceneInstance {
    mat4 model;              // Model transform matrix (64 bytes, offset 0)
    vec4 materialParams;     // x=roughness, y=metallic, z=emissiveIntensity, w=opacity (offset 64)
    vec4 emissiveColor;      // rgb=emissive color, a=unused (offset 80)
    uint pbrFlags;           // PBR texture flags bitmask (offset 96)
    float alphaTestThreshold;// offset 100
    float hueShift;          // offset 104
    float _pad1;             // offset 108
};

// Instance buffer (readonly for vertex/fragment shaders).
// Lives in its own descriptor set (set 1) so material sets (set 0) are shared,
// unchanged, with the CPU draw path.
layout(std430, set = SET_SCENE_INSTANCE, binding = BINDING_SCENE_INSTANCE_BUFFER)
readonly buffer SceneInstanceBuffer {
    SceneInstance sceneInstances[];
};

// Helper to get material properties from instance
#define INSTANCE_ROUGHNESS(inst) (inst.materialParams.x)
#define INSTANCE_METALLIC(inst) (inst.materialParams.y)
#define INSTANCE_EMISSIVE_INTENSITY(inst) (inst.materialParams.z)
#define INSTANCE_OPACITY(inst) (inst.materialParams.w)

#endif // SCENE_INSTANCE_COMMON_GLSL
