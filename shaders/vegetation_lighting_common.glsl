// Common lighting functions for vegetation (grass, tree leaves, etc.)
// Ensures consistent lighting between all vegetation types
#ifndef VEGETATION_LIGHTING_COMMON_GLSL
#define VEGETATION_LIGHTING_COMMON_GLSL

#include "constants_common.glsl"
#include "lighting_common.glsl"

// ============================================================================
// Material Parameters for Vegetation
// ============================================================================

// Default material parameters (can be overridden per-shader)
#ifndef VEGETATION_ROUGHNESS
#define VEGETATION_ROUGHNESS 0.7
#endif

#ifndef VEGETATION_SSS_STRENGTH
#define VEGETATION_SSS_STRENGTH 0.35
#endif

#ifndef VEGETATION_SPECULAR_STRENGTH
#define VEGETATION_SPECULAR_STRENGTH 0.15
#endif

#ifndef VEGETATION_RIM_STRENGTH
#define VEGETATION_RIM_STRENGTH 0.15
#endif

#ifndef VEGETATION_BACK_DIFFUSE_FACTOR
#define VEGETATION_BACK_DIFFUSE_FACTOR 0.6
#endif

// ============================================================================
// GPU Light Structure (must match CPU GPULight struct)
// ============================================================================

#ifndef GPU_LIGHT_STRUCT_DEFINED
#define GPU_LIGHT_STRUCT_DEFINED

struct GPULight {
    vec4 positionAndType;    // xyz = position, w = type (0=point, 1=spot)
    vec4 directionAndCone;   // xyz = direction (for spot), w = outer cone angle (cos)
    vec4 colorAndIntensity;  // rgb = color, a = intensity
    vec4 radiusAndInnerCone; // x = radius, y = inner cone angle (cos), z = shadow map index (-1 = no shadow), w = padding
};

#endif // GPU_LIGHT_STRUCT_DEFINED

// ============================================================================
// Two-Sided Diffuse for Thin Vegetation
// ============================================================================

// Calculate two-sided diffuse for thin translucent materials
// Returns combined front and back contribution
float calculateTwoSidedDiffuse(float NdotL) {
    return max(NdotL, 0.0) + max(-NdotL, 0.0) * VEGETATION_BACK_DIFFUSE_FACTOR;
}

// ============================================================================
// Dynamic Light Calculation for Vegetation
// ============================================================================

// Calculate contribution from a single dynamic light for vegetation
vec3 calculateDynamicLightVegetation(
    GPULight light,
    vec3 N,
    vec3 V,
    vec3 worldPos,
    vec3 albedo,
    float roughness,
    float sssStrength
) {
    vec3 lightPos = light.positionAndType.xyz;
    uint lightType = uint(light.positionAndType.w);
    vec3 lightColor = light.colorAndIntensity.rgb;
    float lightIntensity = light.colorAndIntensity.a;
    float lightRadius = light.radiusAndInnerCone.x;

    if (lightIntensity <= 0.0) return vec3(0.0);

    vec3 lightVec = lightPos - worldPos;
    float distance = length(lightVec);
    vec3 L = normalize(lightVec);

    // Early out if beyond radius
    if (lightRadius > 0.0 && distance > lightRadius) return vec3(0.0);

    // Calculate attenuation
    float attenuation = calculateAttenuation(distance, lightRadius);

    // For spot lights, apply cone falloff
    if (lightType == LIGHT_TYPE_SPOT) {
        vec3 spotDir = normalize(light.directionAndCone.xyz);
        float outerCone = light.directionAndCone.w;
        float innerCone = light.radiusAndInnerCone.y;
        float spotFalloff = calculateSpotFalloff(L, spotDir, innerCone, outerCone);
        attenuation *= spotFalloff;
    }

    // Two-sided diffuse for thin vegetation
    float diffuse = calculateTwoSidedDiffuse(dot(N, L));

    // Energy-conserving diffuse (divide by PI to match impostor lighting)
    vec3 diffuseContrib = (albedo / PI) * diffuse;

    // Add SSS for dynamic light
    vec3 sss = calculateSSS(L, V, N, lightColor, albedo, sssStrength);

    return (diffuseContrib + sss) * lightColor * lightIntensity * attenuation;
}

// ============================================================================
// Height-Based Ambient Occlusion for Vegetation
// ============================================================================

// Calculate AO based on height along vegetation blade/leaf
// fragHeight: 0 at base, 1 at tip
float calculateVegetationAO(float fragHeight) {
    // Darker at base, brighter at tip
    return 0.4 + fragHeight * 0.6;
}

// Calculate height-based ambient color shift
// Base is cooler/darker, tips catch more sky light
vec3 calculateHeightAmbient(vec3 ambientColor, vec3 albedo, float fragHeight) {
    vec3 ambientBase = ambientColor * vec3(0.8, 0.85, 0.9);  // Cooler at base
    vec3 ambientTip = ambientColor * vec3(1.0, 1.0, 0.95);   // Warmer at tip
    return albedo * mix(ambientBase, ambientTip, fragHeight);
}

// ============================================================================
// Fresnel Rim Lighting for Vegetation
// ============================================================================

// Calculate rim lighting for vegetation
vec3 calculateVegetationRimLight(vec3 N, vec3 V, vec3 ambientColor, vec3 sunColor, float sunIntensity) {
    float NoV = max(dot(N, V), 0.0);
    float rimFresnel = pow(1.0 - NoV, 4.0);

    // Rim color based on sky/ambient
    vec3 rimColor = ambientColor * 0.5 + sunColor * sunIntensity * 0.2;
    return rimColor * rimFresnel * VEGETATION_RIM_STRENGTH;
}

// ============================================================================
// Moon Lighting for Vegetation
// ============================================================================

// Calculate moon lighting contribution
vec3 calculateVegetationMoonLight(
    vec3 N,
    vec3 V,
    vec3 moonDir,
    vec3 moonColor,
    float moonIntensity,
    float sunAltitude,  // ubo.sunDirection.y
    vec3 albedo,
    float sssStrength
) {
    float moonNdotL = dot(N, moonDir);
    float moonDiffuse = calculateTwoSidedDiffuse(moonNdotL);

    // Energy-conserving diffuse (divide by PI to match impostor lighting)
    vec3 diffuseContrib = (albedo / PI) * moonDiffuse;

    // Smooth transition: starts at sun altitude 10°, full effect at -6°
    float twilightFactor = smoothstep(0.17, -0.1, sunAltitude);
    float moonVisibility = smoothstep(-0.09, 0.1, moonDir.y);
    float moonSssFactor = twilightFactor * moonVisibility;

    vec3 moonSss = calculateSSS(moonDir, V, N, moonColor, albedo, sssStrength) * 0.5 * moonSssFactor;

    return (diffuseContrib + moonSss) * moonColor * moonIntensity;
}

// ============================================================================
// Sun Lighting for Vegetation (with Specular)
// ============================================================================

// Calculate sun lighting with PBR specular for vegetation
vec3 calculateVegetationSunLight(
    vec3 N,
    vec3 V,
    vec3 sunDir,
    vec3 sunColor,
    float sunIntensity,
    vec3 albedo,
    float roughness,
    float sssStrength,
    float shadow
) {
    // Two-sided diffuse
    float sunNdotL = dot(N, sunDir);
    float sunDiffuse = calculateTwoSidedDiffuse(sunNdotL);

    // Energy-conserving diffuse (divide by PI to match impostor lighting)
    vec3 diffuseContrib = (albedo / PI) * sunDiffuse;

    // Specular highlight (subtle for vegetation)
    vec3 H = normalize(V + sunDir);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float D = D_GGX(NoH, roughness);
    vec3 F0 = vec3(F0_DIELECTRIC);  // Dielectric vegetation
    vec3 F = F_Schlick(VoH, F0);
    vec3 specular = D * F * VEGETATION_SPECULAR_STRENGTH;

    // Subsurface scattering - light through vegetation when backlit
    vec3 sss = calculateSSS(sunDir, V, N, sunColor, albedo, sssStrength);

    return (diffuseContrib + specular + sss) * sunColor * sunIntensity * shadow;
}

// ============================================================================
// Complete Vegetation Lighting (All Components)
// ============================================================================

// Full vegetation lighting calculation
// Returns final lit color before aerial perspective
struct VegetationLightingParams {
    vec3 N;                  // Surface normal
    vec3 V;                  // View direction (toward camera)
    vec3 worldPos;           // World position
    vec3 albedo;             // Surface albedo
    float fragHeight;        // Height along vegetation (0=base, 1=tip)

    // Sun
    vec3 sunDir;             // Sun direction (toward sun)
    vec3 sunColor;
    float sunIntensity;
    float shadow;            // Combined terrain + cloud shadow

    // Moon
    vec3 moonDir;            // Moon direction (toward moon)
    vec3 moonColor;
    float moonIntensity;
    float sunAltitude;       // For twilight calculation

    // Ambient
    vec3 ambientColor;

    // Material
    float roughness;
    float sssStrength;
};

vec3 calculateVegetationLighting(VegetationLightingParams params) {
    // Sun lighting
    vec3 sunLight = calculateVegetationSunLight(
        params.N, params.V, params.sunDir,
        params.sunColor, params.sunIntensity,
        params.albedo, params.roughness, params.sssStrength,
        params.shadow
    );

    // Moon lighting
    vec3 moonLight = calculateVegetationMoonLight(
        params.N, params.V, params.moonDir,
        params.moonColor, params.moonIntensity,
        params.sunAltitude, params.albedo, params.sssStrength
    );

    // Rim lighting
    vec3 rimLight = calculateVegetationRimLight(
        params.N, params.V,
        params.ambientColor, params.sunColor, params.sunIntensity
    );

    // Height-based ambient
    vec3 ambient = calculateHeightAmbient(params.ambientColor, params.albedo, params.fragHeight);

    // Height-based AO
    float ao = calculateVegetationAO(params.fragHeight);

    // Combine all lighting
    return (ambient + sunLight + moonLight + rimLight) * ao;
}

// Simplified lighting without moon (for shadows or simple shaders)
vec3 calculateVegetationLightingSimple(
    vec3 N,
    vec3 V,
    vec3 sunDir,
    vec3 albedo,
    float shadow,
    vec3 sunColor,
    float sunIntensity,
    vec3 ambientColor
) {
    // Two-sided diffuse
    float NdotL = dot(N, sunDir);
    float diffuse = calculateTwoSidedDiffuse(NdotL);

    // Simple SSS
    vec3 sss = calculateSSS(sunDir, V, N, sunColor, albedo, VEGETATION_SSS_STRENGTH);

    // Energy-conserving diffuse
    vec3 diffuseLight = albedo / PI;

    // Direct lighting with SSS
    vec3 directLight = (diffuseLight * diffuse + sss) * sunColor * sunIntensity * shadow;

    // Ambient lighting
    vec3 ambient = ambientColor * albedo;

    return directLight + ambient;
}

#endif // VEGETATION_LIGHTING_COMMON_GLSL
