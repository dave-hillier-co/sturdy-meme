#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "shadow_common.glsl"
#include "color_common.glsl"
#include "octahedral_mapping.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in float fragBlendFactor;
layout(location = 3) flat in int fragCellIndex;
layout(location = 4) in mat3 fragImpostorToWorld;
layout(location = 7) flat in uint fragArchetypeIndex;

// Octahedral mode inputs
layout(location = 8) in vec2 fragOctaUV;
layout(location = 9) in vec3 fragViewDir;
layout(location = 10) flat in int fragUseOctahedral;

layout(location = 0) out vec4 outColor;

// Impostor atlas texture arrays (one layer per archetype)
layout(binding = BINDING_TREE_IMPOSTOR_ALBEDO) uniform sampler2DArray albedoAlphaAtlas;
layout(binding = BINDING_TREE_IMPOSTOR_NORMAL) uniform sampler2DArray normalDepthAOAtlas;

// Shadow map
layout(binding = BINDING_TREE_IMPOSTOR_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;

layout(push_constant) uniform PushConstants {
    vec4 cameraPos;     // xyz = camera position, w = autumnHueShift
    vec4 lodParams;     // x = useOctahedral, y = brightness, z = normal strength, w = debug elevation
    vec4 atlasParams;   // x = enableFrameBlending, y = unused, z = unused, w = debugShowCellIndex
} push;

// Debug colors for angles
const vec3 debugColors[8] = vec3[8](
    vec3(1.0, 0.0, 0.0),   // Red
    vec3(1.0, 0.5, 0.0),   // Orange
    vec3(1.0, 1.0, 0.0),   // Yellow
    vec3(0.0, 1.0, 0.0),   // Green
    vec3(0.0, 1.0, 1.0),   // Cyan
    vec3(0.0, 0.0, 1.0),   // Blue
    vec3(0.5, 0.0, 1.0),   // Purple
    vec3(1.0, 0.0, 1.0)    // Magenta
);

// Octahedral normal decoding
vec3 decodeOctahedral(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        vec2 signNotZero = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }
    return normalize(n);
}

// 4x4 Bayer dither matrix for LOD transition
const float bayerMatrix[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
);

// Sample octahedral atlas at a specific frame
vec4 sampleOctahedralFrame(vec2 frameUV, uint archetype) {
    return texture(albedoAlphaAtlas, vec3(frameUV, float(archetype)));
}

vec4 sampleOctahedralNormal(vec2 frameUV, uint archetype) {
    return texture(normalDepthAOAtlas, vec3(frameUV, float(archetype)));
}

void main() {
    vec4 albedoAlpha;
    vec4 normalDepthAO;

    if (fragUseOctahedral != 0) {
        // Octahedral mode with optional frame blending
        bool enableBlending = push.atlasParams.x > 0.5;

        if (enableBlending) {
            // Get frame blend data for smooth interpolation
            OctaFrameData frameData = octaGetFrameBlendData(fragViewDir, OCTA_GRID_SIZE, true);

            // Sample all 3 frames
            vec4 albedo0 = sampleOctahedralFrame(frameData.frameUV0, fragArchetypeIndex);
            vec4 albedo1 = sampleOctahedralFrame(frameData.frameUV1, fragArchetypeIndex);
            vec4 albedo2 = sampleOctahedralFrame(frameData.frameUV2, fragArchetypeIndex);

            vec4 normal0 = sampleOctahedralNormal(frameData.frameUV0, fragArchetypeIndex);
            vec4 normal1 = sampleOctahedralNormal(frameData.frameUV1, fragArchetypeIndex);
            vec4 normal2 = sampleOctahedralNormal(frameData.frameUV2, fragArchetypeIndex);

            // Blend based on weights
            albedoAlpha = albedo0 * frameData.weight0 +
                          albedo1 * frameData.weight1 +
                          albedo2 * frameData.weight2;

            normalDepthAO = normal0 * frameData.weight0 +
                            normal1 * frameData.weight1 +
                            normal2 * frameData.weight2;

            // Improve alpha handling - use max alpha to avoid premature clipping
            albedoAlpha.a = max(max(albedo0.a * frameData.weight0,
                                    albedo1.a * frameData.weight1),
                                albedo2.a * frameData.weight2) +
                            min(min(albedo0.a * frameData.weight0,
                                    albedo1.a * frameData.weight1),
                                albedo2.a * frameData.weight2);
        } else {
            // Single frame lookup (no blending)
            vec3 atlasCoord = vec3(fragOctaUV, float(fragArchetypeIndex));
            albedoAlpha = texture(albedoAlphaAtlas, atlasCoord);
            normalDepthAO = texture(normalDepthAOAtlas, atlasCoord);
        }
    } else {
        // Legacy mode: direct texture lookup
        vec3 atlasCoord = vec3(fragTexCoord, float(fragArchetypeIndex));
        albedoAlpha = texture(albedoAlphaAtlas, atlasCoord);
        normalDepthAO = texture(normalDepthAOAtlas, atlasCoord);
    }

    // Alpha test
    if (albedoAlpha.a < 0.5) {
        discard;
    }

    // LOD dithered transition
    if (fragBlendFactor < 0.99) {
        ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
        int ditherIndex = (pixelCoord.x % 4) + (pixelCoord.y % 4) * 4;
        float ditherValue = bayerMatrix[ditherIndex];
        if (fragBlendFactor < ditherValue) {
            discard;
        }
    }

    // Decode normal from octahedral encoding
    vec3 impostorNormal = decodeOctahedral(normalDepthAO.rg);

    // Transform normal from impostor space to world space
    float normalStrength = push.lodParams.z;
    vec3 worldNormal = normalize(fragImpostorToWorld * impostorNormal);
    worldNormal = mix(vec3(0.0, 1.0, 0.0), worldNormal, normalStrength);

    float depth = normalDepthAO.b;
    float ao = normalDepthAO.a;

    // Get albedo and apply brightness adjustment
    vec3 albedo = albedoAlpha.rgb * push.lodParams.y;

    // Apply autumn hue shift
    float autumnFactor = push.cameraPos.w;
    albedo = applyAutumnHueShift(albedo, autumnFactor);

    // PBR parameters for tree foliage
    float metallic = 0.0;
    float roughness = 0.7;

    // Calculate lighting
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 lightDir = normalize(-ubo.sunDirection.xyz);

    // Simple PBR-like lighting
    float NdotL = max(dot(worldNormal, lightDir), 0.0);
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(worldNormal, halfVec), 0.0);
    float NdotV = max(dot(worldNormal, viewDir), 0.001);

    // Diffuse
    vec3 diffuse = albedo / 3.14159265;

    // Simple specular
    float spec = pow(NdotH, 32.0) * (1.0 - roughness);
    vec3 specular = vec3(spec) * 0.1;

    // Shadow sampling
    float shadow = calculateCascadedShadow(
        fragWorldPos, worldNormal, lightDir,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Combine lighting
    vec3 sunLight = ubo.sunColor.rgb * ubo.sunColor.a;
    vec3 ambient = ubo.ambientColor.rgb * ubo.ambientColor.a * ao;

    vec3 color = ambient * albedo;
    color += (diffuse + specular) * sunLight * NdotL * shadow;

    // Debug: show cell index as color
    if (push.atlasParams.w > 0.5) {
        if (fragUseOctahedral != 0) {
            // Octahedral mode: show grid position as color gradient
            color = vec3(fragOctaUV.x, fragOctaUV.y, 0.5);
        } else {
            // Legacy mode: discrete cell colors
            if (fragCellIndex == 8) {
                color = vec3(1.0);  // Top-down = white
            } else if (fragCellIndex < 8) {
                color = debugColors[fragCellIndex];
            } else {
                int hIndex = fragCellIndex - 9;
                color = debugColors[hIndex % 8] * 0.7;
            }
        }
    }

    outColor = vec4(color, 1.0);
}
