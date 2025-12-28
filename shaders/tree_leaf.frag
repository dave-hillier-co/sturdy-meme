#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

// Override UBO bindings for tree leaf descriptor set BEFORE including UBO files
#define SNOW_UBO_BINDING BINDING_TREE_GFX_SNOW_UBO
#define CLOUD_SHADOW_UBO_BINDING BINDING_TREE_GFX_CLOUD_SHADOW_UBO

#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "ubo_snow.glsl"
#include "ubo_cloud_shadow.glsl"
#include "atmosphere_common.glsl"
#include "color_common.glsl"
#include "dither_common.glsl"
#include "vegetation_lighting_common.glsl"
#include "snow_common.glsl"
#include "cloud_shadow_common.glsl"

layout(binding = BINDING_TREE_GFX_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TREE_GFX_LEAF_ALBEDO) uniform sampler2D leafAlbedo;

// Snow mask texture (world-space coverage)
layout(binding = BINDING_TREE_GFX_SNOW_MASK) uniform sampler2D snowMaskTexture;

// Cloud shadow map (R16F: 0=shadow, 1=no shadow)
layout(binding = BINDING_TREE_GFX_CLOUD_SHADOW) uniform sampler2D cloudShadowMap;

// Light buffer SSBO
layout(std430, binding = BINDING_TREE_GFX_LIGHT_BUFFER) readonly buffer LightBuffer {
    uvec4 lightCount;        // x = active light count
    GPULight lights[MAX_LIGHTS];
} lightBuffer;

// Simplified push constants - no more per-tree data
layout(push_constant) uniform PushConstants {
    float time;
    float alphaTest;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragLeafSize;
layout(location = 4) in vec3 fragLeafTint;
layout(location = 5) in float fragAutumnHueShift;
layout(location = 6) in float fragLodBlendFactor;

layout(location = 0) out vec4 outColor;

// Calculate contribution from all dynamic lights for tree leaves
vec3 calculateAllDynamicLightsLeaf(vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(lightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        totalLight += calculateDynamicLightVegetation(
            lightBuffer.lights[i], N, V, worldPos, albedo,
            VEGETATION_ROUGHNESS, VEGETATION_SSS_STRENGTH
        );
    }

    return totalLight;
}

void main() {
    // LOD dithered fade-out using staggered crossfade
    // Leaves fade out in sync with impostor fade-in for true leaf crossfade
    // This prevents the impostor disappearing while leaves are still dithering in
    if (shouldDiscardForLODLeaves(fragLodBlendFactor)) {
        discard;
    }

    // Sample leaf texture
    vec4 albedoSample = texture(leafAlbedo, fragTexCoord);

    // Alpha test for leaf transparency
    if (albedoSample.a < push.alphaTest) {
        discard;
    }

    // Apply tint and autumn hue shift (from vertex shader / tree data SSBO)
    vec3 baseColor = albedoSample.rgb * fragLeafTint;
    baseColor = applyAutumnHueShift(baseColor, fragAutumnHueShift);

    // Use geometry normal (leaves are flat)
    vec3 N = normalize(fragNormal);

    // Make leaves double-sided
    if (!gl_FrontFacing) {
        N = -N;
    }

    // View and light directions
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 sunL = normalize(ubo.sunDirection.xyz);  // sunDirection points toward sun
    vec3 moonL = normalize(ubo.moonDirection.xyz);

    // === SNOW LAYER ===
    vec3 albedo = baseColor;
    float snowMaskCoverage = sampleSnowMask(snowMaskTexture, fragWorldPos,
                                             snow.snowMaskParams.xy, snow.snowMaskParams.z);

    // Calculate vegetation snow coverage - tips/leaves catch more snow
    // Use 1.0 as affinity since leaves are tips
    float snowCoverage = calculateVegetationSnowCoverage(snow.snowAmount, snowMaskCoverage, N, 1.0);

    // Apply snow to leaf albedo
    if (snowCoverage > 0.01) {
        albedo = snowyVegetationColor(albedo, snow.snowColor.rgb, snowCoverage);
    }

    // === SHADOW CALCULATION ===
    float terrainShadow = calculateCascadedShadow(
        fragWorldPos, N, sunL,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Cloud shadows
    float cloudShadowFactor = 1.0;
    if (cloudShadow.cloudShadowEnabled > 0.5) {
        cloudShadowFactor = sampleCloudShadowSoft(cloudShadowMap, fragWorldPos, cloudShadow.cloudShadowMatrix);
    }

    // Combine terrain and cloud shadows
    float shadow = combineShadows(terrainShadow, cloudShadowFactor);

    // === SUN LIGHTING ===
    vec3 sunLight = calculateVegetationSunLight(
        N, V, sunL,
        ubo.sunColor.rgb, ubo.sunDirection.w,
        albedo, VEGETATION_ROUGHNESS, VEGETATION_SSS_STRENGTH,
        shadow
    );

    // === MOON LIGHTING ===
    vec3 moonLight = calculateVegetationMoonLight(
        N, V, moonL,
        ubo.moonColor.rgb, ubo.moonDirection.w,
        ubo.sunDirection.y,  // Sun altitude for twilight calculation
        albedo, VEGETATION_SSS_STRENGTH
    );

    // === DYNAMIC LIGHTS ===
    vec3 dynamicLights = calculateAllDynamicLightsLeaf(N, V, fragWorldPos, albedo);

    // === RIM LIGHTING ===
    vec3 rimLight = calculateVegetationRimLight(
        N, V,
        ubo.ambientColor.rgb, ubo.sunColor.rgb, ubo.sunDirection.w
    );

    // === AMBIENT LIGHTING ===
    // For leaves, use a fixed height value (they're all "tips" of the tree)
    float leafHeight = 0.8;  // Leaves are near the top
    vec3 ambient = calculateHeightAmbient(ubo.ambientColor.rgb, albedo, leafHeight);
    float ao = calculateVegetationAO(leafHeight);

    // === COMBINE LIGHTING ===
    vec3 color = (ambient + sunLight + moonLight + dynamicLights + rimLight) * ao;

    // Apply aerial perspective for distant leaves
    color = applyAerialPerspectiveSimple(color, fragWorldPos);

    // Output with alpha=1.0 since we use alpha-test (discard) for transparency
    outColor = vec4(color, 1.0);
}
