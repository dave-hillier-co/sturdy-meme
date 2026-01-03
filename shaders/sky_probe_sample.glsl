/*
 * sky_probe_sample.glsl - Sky visibility probe sampling
 *
 * Include this file in any shader that needs ambient sky lighting.
 * Provides cascaded probe lookup with smooth blending between cascades.
 *
 * Usage:
 *   #include "sky_probe_sample.glsl"
 *
 *   // In fragment shader:
 *   vec3 skyVis = sampleSkyProbe(worldPos, worldNormal);
 *   vec3 ambient = skyVis * skyIrradiance;
 *
 * Required bindings (define before including):
 *   SKY_PROBE_BINDING_TEXTURE - 3D probe texture (sampler3D)
 *   SKY_PROBE_BINDING_CASCADE_INFO - Cascade info UBO
 */

#ifndef SKY_PROBE_SAMPLE_GLSL
#define SKY_PROBE_SAMPLE_GLSL

#ifndef SKY_PROBE_BINDING_TEXTURE
#define SKY_PROBE_BINDING_TEXTURE 0
#endif

#ifndef SKY_PROBE_BINDING_CASCADE_INFO
#define SKY_PROBE_BINDING_CASCADE_INFO 1
#endif

#ifndef SKY_PROBE_SET
#define SKY_PROBE_SET 0
#endif

// Cascade info from CPU
struct SkyProbeCascadeInfo {
    vec4 origin;       // xyz = world origin, w = spacing
    vec4 invSize;      // xyz = 1/(gridSize * spacing), w = blend range
    vec4 params;       // x = gridSize, y = layer offset (normalized), z = range, w = unused
};

layout(set = SKY_PROBE_SET, binding = SKY_PROBE_BINDING_TEXTURE) uniform sampler3D skyProbeTexture;

layout(set = SKY_PROBE_SET, binding = SKY_PROBE_BINDING_CASCADE_INFO) uniform SkyProbeCascades {
    SkyProbeCascadeInfo cascades[4];
} skyProbeCascades;

// Sample a single cascade at world position
// Returns vec4: xyz = sky color/visibility, w = valid weight
vec4 sampleCascade(int cascadeIndex, vec3 worldPos) {
    SkyProbeCascadeInfo info = skyProbeCascades.cascades[cascadeIndex];

    // Transform to local cascade space [0, 1]
    vec3 localPos = (worldPos - info.origin.xyz) * info.invSize.xyz;

    // Check if position is within cascade bounds
    if (any(lessThan(localPos, vec3(0.0))) || any(greaterThan(localPos, vec3(1.0)))) {
        return vec4(0.0);
    }

    // Calculate blend weight (fade out near edges for smooth cascade transitions)
    float blendRange = info.invSize.w;
    vec3 edgeDist = min(localPos, 1.0 - localPos);
    float edgeFade = smoothstep(0.0, blendRange, min(edgeDist.x, min(edgeDist.y, edgeDist.z)));

    // Prefer inner cascades (higher weight for finer detail)
    float cascadeWeight = 1.0 / float(cascadeIndex + 1);
    float weight = edgeFade * cascadeWeight;

    // Offset Z by cascade layer
    float layerOffset = info.params.y;
    float gridSize = info.params.x;

    // Map local position to atlas UV
    // localPos is in [0,1] for this cascade's grid
    // We need to offset into the correct Z region of the atlas
    vec3 atlasUV = vec3(
        localPos.xy,
        localPos.z * (gridSize / 256.0) + layerOffset  // Assuming max 256 total layers
    );

    // Sample probe (RGBA16F: bent normal or SH data)
    vec4 probeData = texture(skyProbeTexture, atlasUV);

    return vec4(probeData.rgb, weight * probeData.a);
}

// Sample sky visibility from cascaded probes
// Returns visibility-weighted sky color (typically multiplied by irradiance LUT)
vec3 sampleSkyProbe(vec3 worldPos, vec3 worldNormal) {
    vec3 totalColor = vec3(0.0);
    float totalWeight = 0.0;

    // Sample all cascades, blend by weight
    for (int c = 0; c < 4; c++) {
        vec4 cascadeResult = sampleCascade(c, worldPos);
        totalColor += cascadeResult.rgb * cascadeResult.a;
        totalWeight += cascadeResult.a;
    }

    // Normalize by total weight
    if (totalWeight > 0.001) {
        totalColor /= totalWeight;
    } else {
        // Fallback: full sky visibility
        totalColor = vec3(1.0);
    }

    return totalColor;
}

// Sample with normal-weighted visibility (for bent normal format)
// bentNormal.xyz = dominant unoccluded direction
// bentNormal.w = visibility [0,1]
vec3 sampleSkyProbeWeighted(vec3 worldPos, vec3 worldNormal) {
    vec3 totalColor = vec3(0.0);
    float totalWeight = 0.0;

    for (int c = 0; c < 4; c++) {
        vec4 cascadeResult = sampleCascade(c, worldPos);

        if (cascadeResult.a > 0.001) {
            // Interpret RGB as bent normal, apply normal weighting
            vec3 bentNormal = normalize(cascadeResult.rgb * 2.0 - 1.0);
            float visibility = cascadeResult.a;

            // Weight by dot product with surface normal
            float normalWeight = max(0.0, dot(worldNormal, bentNormal));

            totalColor += vec3(visibility * normalWeight);
            totalWeight += cascadeResult.a;
        }
    }

    if (totalWeight > 0.001) {
        totalColor /= totalWeight;
    } else {
        totalColor = vec3(1.0);
    }

    return totalColor;
}

// Get sky visibility scalar (simple version)
float getSkyVisibility(vec3 worldPos) {
    float totalVis = 0.0;
    float totalWeight = 0.0;

    for (int c = 0; c < 4; c++) {
        vec4 cascadeResult = sampleCascade(c, worldPos);
        // Use alpha as visibility, weight by cascade preference
        totalVis += cascadeResult.a * (1.0 / float(c + 1));
        totalWeight += cascadeResult.a > 0.0 ? (1.0 / float(c + 1)) : 0.0;
    }

    return totalWeight > 0.001 ? (totalVis / totalWeight) : 1.0;
}

#endif // SKY_PROBE_SAMPLE_GLSL
