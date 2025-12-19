#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "wind_common.glsl"

struct GrassInstance {
    vec4 positionAndFacing;  // xyz = position, w = facing angle
    vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
    vec4 terrainNormal;      // xyz = terrain normal (for tangent alignment), w = unused
};

layout(std430, binding = BINDING_GRASS_INSTANCE_BUFFER) readonly buffer InstanceBuffer {
    GrassInstance instances[];
};

// Wind uniform buffer (auto-generated via shader_reflect)
// Note: Shadow pass uses binding 2 for wind (different from main pass which uses 3)
layout(binding = BINDING_GRASS_SHADOW_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
    ivec4 permPacked[128];          // Permutation table: 512 ints packed as 128 ivec4s
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    int cascadeIndex;  // Which cascade we're rendering
} push;

layout(location = 0) out float fragHeight;
layout(location = 1) out float fragHash;

// Quadratic Bezier evaluation
vec3 bezier(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return u * u * p0 + 2.0 * u * t * p1 + t * t * p2;
}

// Build orthonormal basis from terrain normal (same as grass.vert)
void buildTerrainBasis(vec3 N, float facing, out vec3 T, out vec3 B) {
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = vec3(1.0, 0.0, 0.0);
    vec3 ref = abs(N.y) < 0.99 ? up : right;

    T = normalize(cross(N, ref));
    B = normalize(cross(N, T));

    float cs = cos(facing);
    float sn = sin(facing);
    vec3 T_rotated = T * cs + B * sn;
    vec3 B_rotated = -T * sn + B * cs;

    T = T_rotated;
    B = B_rotated;
}

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
    buildTerrainBasis(terrainNormal, facing, T, B);

    // Wind animation using noise-based wind system (must match grass.vert for consistent shadows)
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windTime = wind.windParams.w;

    // Sample wind strength at this blade's position (uses UBO permutation table)
    float windSample = sampleWindNoise(wind.windDirectionAndStrength, wind.windParams, wind.permPacked, vec2(basePos.x, basePos.z));

    // Per-blade phase offset for variation (prevents lockstep motion)
    float windPhase = bladeHash * 6.28318;
    float phaseOffset = sin(windTime * 2.5 + windPhase) * 0.3;

    // Wind offset (same calculation as grass.vert)
    float windAngle = atan(windDir.y, windDir.x);
    float relativeWindAngle = windAngle - facing;
    float windEffect = (windSample + phaseOffset) * 0.25;
    float windOffset = windEffect * cos(relativeWindAngle);

    // Bezier control points (in local blade space)
    vec3 p0 = vec3(0.0, 0.0, 0.0);  // Base
    vec3 p1 = vec3(windOffset * 0.3 + tilt * 0.5, height * 0.5, 0.0);  // Mid control
    vec3 p2 = vec3(windOffset + tilt, height, 0.0);  // Tip

    // Triangle strip blade geometry: 15 vertices = 7 segments (8 height levels)
    uint vi = gl_VertexIndex;
    const uint NUM_SEGMENTS = 7;
    const float baseWidth = 0.02;

    uint segmentIndex = vi / 2;
    bool isRightSide = (vi % 2) == 1;

    float t = float(segmentIndex) / float(NUM_SEGMENTS);
    float widthAtT = baseWidth * (1.0 - t * 0.9);

    if (vi == 14) {
        widthAtT = 0.0;
        t = 1.0;
    }

    float xOffset = isRightSide ? widthAtT : -widthAtT;

    // Get position on bezier curve
    vec3 curvePos = bezier(p0, p1, p2, t);
    vec3 localPos = curvePos + vec3(xOffset, 0.0, 0.0);

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
