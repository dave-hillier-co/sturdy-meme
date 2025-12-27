#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "grass_blade_common.glsl"

struct GrassInstance {
    vec4 positionAndFacing;  // xyz = position, w = facing angle
    vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
    vec4 terrainNormal;      // xyz = terrain normal (for tangent alignment), w = unused
};

layout(std430, binding = BINDING_GRASS_INSTANCE_BUFFER) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

// Wind uniform buffer (must match grass.vert for shadow consistency)
// Note: Shadow pass uses binding 2 for wind (different from main pass which uses 3)
layout(binding = BINDING_GRASS_SHADOW_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    int cascadeIndex;  // Which cascade we're rendering
} push;

layout(location = 0) out float fragHeight;
layout(location = 1) out float fragHash;

void main() {
    // Get instance data using gl_InstanceIndex
    GrassInstance inst = instances[gl_InstanceIndex];
    vec3 basePos = inst.positionAndFacing.xyz;
    float facing = inst.positionAndFacing.w;
    float height = inst.heightHashTilt.x;
    float bladeHash = inst.heightHashTilt.y;
    float tilt = inst.heightHashTilt.z;
    vec3 terrainNormal = normalize(inst.terrainNormal.xyz);

    // Build coordinate frame aligned to terrain surface
    vec3 T, B;
    grassBuildTerrainBasis(terrainNormal, facing, T, B);

    // Wind animation using noise-based wind system (must match grass.vert for consistent shadows)
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windTime = wind.windParams.w;

    // Sample wind strength at this blade's position
    float windSample = grassSampleWind(
        vec2(basePos.x, basePos.z),
        windDir,
        wind.windDirectionAndStrength.z,  // windStrength
        wind.windDirectionAndStrength.w,  // windSpeed
        windTime,
        wind.windParams.x,  // gustFreq
        wind.windParams.y   // gustAmp
    );

    // Per-blade phase offset for variation (prevents lockstep motion)
    float windPhase = bladeHash * 6.28318;
    float phaseOffset = sin(windTime * 2.5 + windPhase) * 0.3;

    // Wind offset (same calculation as grass.vert)
    float windAngle = atan(windDir.y, windDir.x);
    float relativeWindAngle = windAngle - facing;
    float windEffect = (windSample + phaseOffset) * 0.25;
    float windOffset = windEffect * cos(relativeWindAngle);

    // Calculate blade deformation (fold/droop) - SAME as main render pass
    GrassBladeControlPoints cp = grassCalculateBladeDeformation(height, bladeHash, tilt, windOffset);

    // Calculate blade vertex position
    vec3 localPos;
    float t;
    float widthAtT;
    grassCalculateBladeVertex(gl_VertexIndex, cp, localPos, t, widthAtT);

    // Transform from local blade space to world space using terrain basis
    // Local X = blade width direction (bitangent B)
    // Local Y = blade up direction (terrain normal N)
    // Local Z = blade facing direction (tangent T)
    vec3 worldOffset = B * localPos.x +
                       terrainNormal * localPos.y +
                       T * localPos.z;

    // Final world position
    vec3 worldPos = basePos + worldOffset;

    // Transform to light space for shadow map using cascade-specific matrix
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);

    // Pass height and hash for dithering in fragment shader
    fragHeight = t;
    fragHash = bladeHash;
}
