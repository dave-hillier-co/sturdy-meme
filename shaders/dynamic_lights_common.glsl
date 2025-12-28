// Dynamic Lights Common - Shared dynamic lighting functions
// Provides consistent point/spot light handling across all shaders
//
// Prevents multiple inclusion
#ifndef DYNAMIC_LIGHTS_COMMON_GLSL
#define DYNAMIC_LIGHTS_COMMON_GLSL

#include "constants_common.glsl"
#include "lighting_common.glsl"

// ============================================================================
// GPU Light Structure (must match CPU GPULight struct in Light.h)
// ============================================================================
struct GPULight {
    vec4 positionAndType;    // xyz = position, w = type (0=point, 1=spot)
    vec4 directionAndCone;   // xyz = direction (for spot), w = outer cone angle (cos)
    vec4 colorAndIntensity;  // rgb = color, a = intensity
    vec4 radiusAndInnerCone; // x = radius, y = inner cone angle (cos), z = shadow map index (-1 = no shadow), w = padding
};

// ============================================================================
// Light Buffer SSBO Layout
// To use, define DYNAMIC_LIGHT_BUFFER_BINDING before including this file,
// or use the default BINDING_LIGHT_BUFFER from bindings.glsl
// ============================================================================
#ifndef DYNAMIC_LIGHT_BUFFER_BINDING
#define DYNAMIC_LIGHT_BUFFER_BINDING BINDING_LIGHT_BUFFER
#endif

layout(std430, binding = DYNAMIC_LIGHT_BUFFER_BINDING) readonly buffer DynamicLightBuffer {
    uvec4 lightCount;        // x = active light count
    GPULight lights[MAX_LIGHTS];
} dynamicLightBuffer;

// ============================================================================
// Shadow Sampling for Dynamic Lights
// Requires point/spot shadow map samplers to be bound in the shader
// ============================================================================

#ifdef DYNAMIC_LIGHTS_ENABLE_SHADOWS
// Sample point light shadow from cube map
// Requires: samplerCubeArrayShadow pointShadowMaps bound
float samplePointLightShadow(GPULight light, vec3 worldPos, samplerCubeArrayShadow pointShadowMaps) {
    int shadowIndex = int(light.radiusAndInnerCone.z);
    if (shadowIndex < 0) return 1.0;

    vec3 lightPos = light.positionAndType.xyz;
    vec3 fragToLight = worldPos - lightPos;
    float currentDepth = length(fragToLight);
    float radius = light.radiusAndInnerCone.x;

    // Normalize depth to [0,1] range based on light radius
    float normalizedDepth = currentDepth / radius;

    // Sample cube shadow map
    vec4 shadowCoord = vec4(normalize(fragToLight), float(shadowIndex));
    return texture(pointShadowMaps, shadowCoord, normalizedDepth);
}

// Sample spot light shadow from 2D shadow map
// Requires: sampler2DArrayShadow spotShadowMaps bound
float sampleSpotLightShadow(GPULight light, vec3 worldPos, sampler2DArrayShadow spotShadowMaps) {
    int shadowIndex = int(light.radiusAndInnerCone.z);
    if (shadowIndex < 0) return 1.0;

    vec3 lightPos = light.positionAndType.xyz;
    vec3 spotDir = normalize(light.directionAndCone.xyz);

    // Create light view-projection matrix
    // Note: This duplicates the lookAt/perspective helpers from shader.frag
    // For consistency, we inline the computation here
    vec3 f = spotDir;
    vec3 up = abs(f.y) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);

    mat4 lightView = mat4(1.0);
    lightView[0][0] = s.x; lightView[1][0] = s.y; lightView[2][0] = s.z;
    lightView[0][1] = u.x; lightView[1][1] = u.y; lightView[2][1] = u.z;
    lightView[0][2] = -f.x; lightView[1][2] = -f.y; lightView[2][2] = -f.z;
    lightView[3][0] = -dot(s, lightPos);
    lightView[3][1] = -dot(u, lightPos);
    lightView[3][2] = dot(f, lightPos);

    float outerCone = light.directionAndCone.w;
    float fov = acos(outerCone) * 2.0;
    float tanHalfFovy = tan(fov / 2.0);
    float near = 0.1;
    float far = light.radiusAndInnerCone.x;

    mat4 lightProj = mat4(0.0);
    lightProj[0][0] = 1.0 / tanHalfFovy;
    lightProj[1][1] = 1.0 / tanHalfFovy;
    lightProj[2][2] = -(far + near) / (far - near);
    lightProj[2][3] = -1.0;
    lightProj[3][2] = -(2.0 * far * near) / (far - near);

    // Transform world position to light clip space
    vec4 lightSpacePos = lightProj * lightView * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Sample shadow map
    vec4 shadowCoord = vec4(projCoords.xy, float(shadowIndex), projCoords.z);
    return texture(spotShadowMaps, shadowCoord);
}

// Sample shadow for any light type
float sampleDynamicLightShadow(GPULight light, vec3 worldPos,
                                samplerCubeArrayShadow pointShadowMaps,
                                sampler2DArrayShadow spotShadowMaps) {
    uint lightType = uint(light.positionAndType.w);

    if (lightType == LIGHT_TYPE_POINT) {
        return samplePointLightShadow(light, worldPos, pointShadowMaps);
    } else {
        return sampleSpotLightShadow(light, worldPos, spotShadowMaps);
    }
}
#endif // DYNAMIC_LIGHTS_ENABLE_SHADOWS

// ============================================================================
// Core Dynamic Light Calculation
// ============================================================================

// Get light vector and distance, returns false if beyond radius
bool getDynamicLightVector(GPULight light, vec3 worldPos, out vec3 L, out float distance, out float attenuation) {
    vec3 lightPos = light.positionAndType.xyz;
    float lightRadius = light.radiusAndInnerCone.x;
    float lightIntensity = light.colorAndIntensity.a;

    // Skip if light is disabled
    if (lightIntensity <= 0.0) return false;

    // Calculate light direction and distance
    vec3 lightVec = lightPos - worldPos;
    distance = length(lightVec);
    L = lightVec / distance;

    // Early out if beyond radius
    if (lightRadius > 0.0 && distance > lightRadius) return false;

    // Calculate base attenuation
    attenuation = calculateAttenuation(distance, lightRadius);

    // For spot lights, apply cone falloff
    uint lightType = uint(light.positionAndType.w);
    if (lightType == LIGHT_TYPE_SPOT) {
        vec3 spotDir = normalize(light.directionAndCone.xyz);
        float outerCone = light.directionAndCone.w;
        float innerCone = light.radiusAndInnerCone.y;
        float spotFalloff = calculateSpotFalloff(L, spotDir, innerCone, outerCone);
        attenuation *= spotFalloff;
    }

    return attenuation > 0.0001;
}

// ============================================================================
// PBR Dynamic Light (Full quality - for terrain, models, etc.)
// ============================================================================

// Calculate PBR lighting contribution from a single dynamic light
vec3 calculateDynamicLightPBR(GPULight light, vec3 N, vec3 V, vec3 worldPos,
                               vec3 albedo, float roughness, float metallic, float shadow) {
    vec3 L;
    float distance, attenuation;
    if (!getDynamicLightVector(light, worldPos, L, distance, attenuation)) {
        return vec3(0.0);
    }

    vec3 lightColor = light.colorAndIntensity.rgb;
    float lightIntensity = light.colorAndIntensity.a;

    // Apply shadow
    attenuation *= shadow;

    // PBR calculation
    vec3 H = normalize(V + L);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    // F0 for dielectrics/metals
    vec3 F0 = mix(vec3(F0_DIELECTRIC), albedo, metallic);

    // Specular BRDF
    float D = D_GGX(NoH, roughness);
    float Vis = V_SmithGGX(NoV, NoL, roughness);
    vec3 F = F_Schlick(VoH, F0);
    vec3 specular = D * Vis * F;

    // Energy-conserving diffuse
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * lightIntensity * NoL * attenuation;
}

// Calculate contribution from all dynamic lights (PBR version)
#ifdef DYNAMIC_LIGHTS_ENABLE_SHADOWS
vec3 calculateAllDynamicLightsPBR(vec3 N, vec3 V, vec3 worldPos,
                                   vec3 albedo, float roughness, float metallic,
                                   samplerCubeArrayShadow pointShadowMaps,
                                   sampler2DArrayShadow spotShadowMaps) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(dynamicLightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        GPULight light = dynamicLightBuffer.lights[i];
        float shadow = sampleDynamicLightShadow(light, worldPos, pointShadowMaps, spotShadowMaps);
        totalLight += calculateDynamicLightPBR(light, N, V, worldPos, albedo, roughness, metallic, shadow);
    }

    return totalLight;
}
#endif

// Version without shadows (for shaders that don't need shadow sampling)
vec3 calculateAllDynamicLightsPBRNoShadow(vec3 N, vec3 V, vec3 worldPos,
                                           vec3 albedo, float roughness, float metallic) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(dynamicLightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        totalLight += calculateDynamicLightPBR(dynamicLightBuffer.lights[i], N, V, worldPos,
                                                albedo, roughness, metallic, 1.0);
    }

    return totalLight;
}

// ============================================================================
// Vegetation Dynamic Light (For grass, leaves - includes SSS)
// ============================================================================

// Calculate dynamic light contribution for vegetation (two-sided diffuse + SSS)
vec3 calculateDynamicLightVegetation(GPULight light, vec3 N, vec3 V, vec3 worldPos,
                                      vec3 albedo, float roughness, float sssStrength, float shadow) {
    vec3 L;
    float distance, attenuation;
    if (!getDynamicLightVector(light, worldPos, L, distance, attenuation)) {
        return vec3(0.0);
    }

    vec3 lightColor = light.colorAndIntensity.rgb;
    float lightIntensity = light.colorAndIntensity.a;

    // Apply shadow
    attenuation *= shadow;

    // Two-sided diffuse for thin vegetation
    float NdotL = dot(N, L);
    float diffuse = max(NdotL, 0.0) + max(-NdotL, 0.0) * 0.6;

    // Subtle specular for grass
    vec3 H = normalize(V + L);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float D = D_GGX(NoH, roughness);
    vec3 F = F_Schlick(VoH, vec3(F0_DIELECTRIC));
    vec3 specular = D * F * 0.15;  // Reduced specular strength for vegetation

    // Subsurface scattering
    vec3 sss = calculateSSS(L, V, N, lightColor, albedo, sssStrength);

    return (albedo * diffuse + specular + sss) * lightColor * lightIntensity * attenuation;
}

// Calculate contribution from all dynamic lights (vegetation version)
#ifdef DYNAMIC_LIGHTS_ENABLE_SHADOWS
vec3 calculateAllDynamicLightsVegetation(vec3 N, vec3 V, vec3 worldPos,
                                          vec3 albedo, float roughness, float sssStrength,
                                          samplerCubeArrayShadow pointShadowMaps,
                                          sampler2DArrayShadow spotShadowMaps) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(dynamicLightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        GPULight light = dynamicLightBuffer.lights[i];
        float shadow = sampleDynamicLightShadow(light, worldPos, pointShadowMaps, spotShadowMaps);
        totalLight += calculateDynamicLightVegetation(light, N, V, worldPos,
                                                       albedo, roughness, sssStrength, shadow);
    }

    return totalLight;
}
#endif

// Version without shadows
vec3 calculateAllDynamicLightsVegetationNoShadow(vec3 N, vec3 V, vec3 worldPos,
                                                  vec3 albedo, float roughness, float sssStrength) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(dynamicLightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        totalLight += calculateDynamicLightVegetation(dynamicLightBuffer.lights[i], N, V, worldPos,
                                                       albedo, roughness, sssStrength, 1.0);
    }

    return totalLight;
}

// ============================================================================
// Volumetric Scattering (For froxel fog - phase function based)
// ============================================================================

// Henyey-Greenstein phase function for volumetric scattering
float henyeyGreensteinPhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// Calculate in-scattering from a single dynamic light for volumetrics
vec3 calculateDynamicLightVolumetric(GPULight light, vec3 worldPos, vec3 viewDir,
                                      float density, float phaseG) {
    vec3 L;
    float distance, attenuation;
    if (!getDynamicLightVector(light, worldPos, L, distance, attenuation)) {
        return vec3(0.0);
    }

    vec3 lightColor = light.colorAndIntensity.rgb;
    float lightIntensity = light.colorAndIntensity.a;

    // Phase function for scattering direction
    float cosTheta = dot(viewDir, L);
    float phase = henyeyGreensteinPhase(cosTheta, phaseG);

    return lightColor * lightIntensity * attenuation * phase * density;
}

// Calculate contribution from all dynamic lights for volumetrics
vec3 calculateAllDynamicLightsVolumetric(vec3 worldPos, vec3 viewDir, float density, float phaseG) {
    vec3 totalScatter = vec3(0.0);
    uint numLights = min(dynamicLightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        totalScatter += calculateDynamicLightVolumetric(dynamicLightBuffer.lights[i],
                                                         worldPos, viewDir, density, phaseG);
    }

    return totalScatter;
}

#endif // DYNAMIC_LIGHTS_COMMON_GLSL
