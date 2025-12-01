#version 450

#extension GL_GOOGLE_include_directive : require

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "atmosphere_common.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float padding[2];
} material;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;

layout(location = 0) out vec4 outColor;

// Calculate PBR lighting for a single light
vec3 calculatePBR(vec3 N, vec3 V, vec3 L, vec3 lightColor, float lightIntensity, vec3 albedo, float shadow) {
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    // Dielectric F0 (0.04 is typical for non-metals)
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);

    // Specular BRDF
    float D = D_GGX(NoH, material.roughness);
    float Vis = V_SmithGGX(NoV, NoL, material.roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = D * Vis * F;

    // Energy-conserving diffuse
    vec3 kD = (1.0 - F) * (1.0 - material.metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * lightIntensity * NoL * shadow;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 albedo = texColor.rgb;

    // Calculate shadow for sun
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float shadow = calculateCascadedShadow(
        fragWorldPos, N, sunL,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Sun lighting with shadow
    float sunIntensity = ubo.sunDirection.w;
    vec3 sunLight = calculatePBR(N, V, sunL, ubo.sunColor.rgb, sunIntensity, albedo, shadow);

    // Moon lighting (soft fill light)
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonIntensity = ubo.moonDirection.w;
    vec3 moonLight = calculatePBR(N, V, moonL, ubo.moonColor.rgb, moonIntensity, albedo, 1.0);

    // Ambient lighting
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);
    vec3 ambientDiffuse = ubo.ambientColor.rgb * albedo * (1.0 - material.metallic);
    float envReflection = mix(0.3, 1.0, material.roughness);
    vec3 ambientSpecular = ubo.ambientColor.rgb * F0 * material.metallic * envReflection;
    vec3 ambient = ambientDiffuse + ambientSpecular;

    vec3 finalColor = ambient + sunLight + moonLight;

    // Apply aerial perspective
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 atmosphericColor = applyAerialPerspective(finalColor, ubo.cameraPosition.xyz, normalize(cameraToFrag), length(cameraToFrag), sunDir, sunColor);

    // Debug cascade visualization
    if (ubo.debugCascades > 0.5) {
        int cascade = getCascadeForDebug(fragWorldPos, ubo.view, ubo.cascadeSplits);
        vec3 cascadeColor = getCascadeDebugColor(cascade);
        atmosphericColor = mix(atmosphericColor, cascadeColor, 0.3);
    }

    outColor = vec4(atmosphericColor, texColor.a);
}
