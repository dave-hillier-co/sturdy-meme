// PBR texture flags - indicates which optional PBR textures are bound.
// Shared between the push-constant path (push_constants_common.glsl) and the
// instanced path (scene_instance_common.glsl), which selects materials from the
// instance SSBO and so must not pull in the push-constant block.

#ifndef PBR_FLAGS_COMMON_GLSL
#define PBR_FLAGS_COMMON_GLSL

#define PBR_HAS_ROUGHNESS_MAP  (1u << 0)
#define PBR_HAS_METALLIC_MAP   (1u << 1)
#define PBR_HAS_AO_MAP         (1u << 2)
#define PBR_HAS_HEIGHT_MAP     (1u << 3)

#endif // PBR_FLAGS_COMMON_GLSL
