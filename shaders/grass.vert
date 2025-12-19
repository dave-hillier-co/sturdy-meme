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
layout(binding = BINDING_GRASS_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
    ivec4 permPacked[128];          // Permutation table: 512 ints packed as 128 ivec4s
} wind;

layout(push_constant) uniform PushConstants {
    float time;
} push;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragHeight;
layout(location = 3) out float fragClumpId;
layout(location = 4) out vec3 fragWorldPos;

// Quadratic Bezier evaluation
vec3 bezier(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return u * u * p0 + 2.0 * u * t * p1 + t * t * p2;
}

// Quadratic Bezier derivative
vec3 bezierDerivative(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return 2.0 * u * (p1 - p0) + 2.0 * t * (p2 - p1);
}

// Build orthonormal basis from terrain normal
// Returns tangent (T) and bitangent (B) vectors
void buildTerrainBasis(vec3 N, float facing, out vec3 T, out vec3 B) {
    // Start with world up, but handle edge case where N is nearly vertical
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = vec3(1.0, 0.0, 0.0);

    // Choose a reference vector that's not parallel to N
    vec3 ref = abs(N.y) < 0.99 ? up : right;

    // First tangent perpendicular to N
    T = normalize(cross(N, ref));
    // Second tangent perpendicular to both
    B = normalize(cross(N, T));

    // Rotate tangent basis by facing angle around N
    float cs = cos(facing);
    float sn = sin(facing);
    vec3 T_rotated = T * cs + B * sn;
    vec3 B_rotated = -T * sn + B * cs;

    T = T_rotated;
    B = B_rotated;
}

void main() {
    // With indirect draw:
    // - gl_InstanceIndex = which blade (0 to instanceCount-1)
    // - gl_VertexIndex = which vertex within this blade (0 to vertexCount-1)

    // Get instance data using gl_InstanceIndex
    GrassInstance inst = instances[gl_InstanceIndex];
    vec3 basePos = inst.positionAndFacing.xyz;
    float facing = inst.positionAndFacing.w;
    float height = inst.heightHashTilt.x;
    float bladeHash = inst.heightHashTilt.y;
    float tilt = inst.heightHashTilt.z;
    float clumpId = inst.heightHashTilt.w;
    vec3 terrainNormal = normalize(inst.terrainNormal.xyz);

    // Build coordinate frame aligned to terrain surface
    // T = tangent (facing direction on terrain surface)
    // B = bitangent (perpendicular to T on terrain surface)
    // N = terrain normal (grass "up" direction)
    vec3 T, B;
    buildTerrainBasis(terrainNormal, facing, T, B);

    // Wind animation using noise-based wind system
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windTime = wind.windParams.w;

    // Sample wind strength at this blade's position (uses UBO permutation table)
    float windSample = sampleWindNoise(wind.windDirectionAndStrength, wind.windParams, wind.permPacked, vec2(basePos.x, basePos.z));

    // Per-blade phase offset for variation (prevents lockstep motion)
    float windPhase = bladeHash * 6.28318;
    float phaseOffset = sin(windTime * 2.5 + windPhase) * 0.3;

    // Wind offset in wind direction (converted to local X space after rotation)
    // The blade faces in the 'facing' direction, so we need to project wind onto blade space
    float windAngle = atan(windDir.y, windDir.x);
    float relativeWindAngle = windAngle - facing;

    // Wind effect is stronger when wind is perpendicular to blade facing
    // This creates more natural bending
    float windEffect = (windSample + phaseOffset) * 0.25;
    float windOffset = windEffect * cos(relativeWindAngle);

    // Blade folding for short grass
    // Shorter blades fold over more (like real lawn grass bending under weight)
    // Height range is ~0.3-0.7, normalize to 0-1 for folding calculation
    float normalizedHeight = clamp((height - 0.3) / 0.4, 0.0, 1.0);

    // Fold amount: short grass (0) folds a lot, tall grass (1) stays upright
    // foldAmount ranges from 0.6 (short) to 0.1 (tall)
    float foldAmount = mix(0.6, 0.1, normalizedHeight);

    // Add per-blade variation to fold direction using hash
    float foldDirection = (bladeHash - 0.5) * 2.0;  // -1 to 1
    float foldX = foldDirection * foldAmount * height;

    // Short grass also droops more - tip ends up lower relative to height
    float droopFactor = mix(0.3, 0.0, normalizedHeight);  // 30% droop for shortest
    float effectiveHeight = height * (1.0 - droopFactor);

    // Bezier control points (in local blade space)
    vec3 p0 = vec3(0.0, 0.0, 0.0);  // Base
    vec3 p1 = vec3(windOffset * 0.3 + tilt * 0.5 + foldX * 0.5, height * 0.6, 0.0);  // Mid control - higher for fold
    vec3 p2 = vec3(windOffset + tilt + foldX, effectiveHeight, 0.0);  // Tip - with fold and droop

    // Triangle strip blade geometry: 15 vertices = 7 segments (8 height levels)
    // Even vertices (0,2,4,6,8,10,12,14) are left side
    // Odd vertices (1,3,5,7,9,11,13) are right side
    // Vertex 14 is the tip point (width = 0)
    uint vi = gl_VertexIndex;

    // Number of segments and height levels
    const uint NUM_SEGMENTS = 7;
    const float baseWidth = 0.02;

    // Calculate which height level this vertex is at
    uint segmentIndex = vi / 2;  // 0-7
    bool isRightSide = (vi % 2) == 1;

    // Calculate t (position along blade, 0 = base, 1 = tip)
    float t = float(segmentIndex) / float(NUM_SEGMENTS);

    // Width tapers from base to tip (90% taper)
    float widthAtT = baseWidth * (1.0 - t * 0.9);

    // For the last vertex (tip), width is 0
    if (vi == 14) {
        widthAtT = 0.0;
        t = 1.0;
    }

    // Offset left or right from center
    float xOffset = isRightSide ? widthAtT : -widthAtT;

    // Get position on bezier curve and offset by width
    vec3 curvePos = bezier(p0, p1, p2, t);
    vec3 localPos = curvePos + vec3(xOffset, 0.0, 0.0);

    // Blade's right vector (B - bitangent, perpendicular to facing in terrain plane)
    vec3 bladeRight = B;

    // View-facing thickening: widen blade when viewed edge-on
    // Calculate view direction to blade base
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - basePos);

    // How much we're viewing edge-on (0 = face-on, 1 = edge-on)
    float edgeFactor = abs(dot(viewDir, bladeRight));

    // Thicken by up to 3x when viewed edge-on
    float thickenAmount = 1.0 + edgeFactor * 2.0;
    vec3 thickenedPos = vec3(localPos.x * thickenAmount, localPos.y, localPos.z);

    // Transform from local blade space to world space using terrain basis
    // Local X = blade width direction (bitangent B)
    // Local Y = blade up direction (terrain normal N)
    // Local Z = blade facing direction (tangent T)
    vec3 worldOffset = B * thickenedPos.x +
                       terrainNormal * thickenedPos.y +
                       T * thickenedPos.z;

    // Final world position
    vec3 worldPos = basePos + worldOffset;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Calculate normal (perpendicular to blade surface)
    // Bezier tangent is in local space (X=width, Y=up, Z=forward)
    vec3 localTangent = normalize(bezierDerivative(p0, p1, p2, t));

    // Transform tangent to world space using terrain basis
    vec3 worldTangent = B * localTangent.x +
                        terrainNormal * localTangent.y +
                        T * localTangent.z;

    // Normal is perpendicular to both tangent and blade width direction
    vec3 normal = normalize(cross(worldTangent, bladeRight));

    // Color gradient: darker at base, lighter at tip
    vec3 baseColor = vec3(0.08, 0.22, 0.04);
    vec3 tipColor = vec3(0.35, 0.65, 0.18);
    fragColor = mix(baseColor, tipColor, t);

    fragNormal = normal;
    fragHeight = t;
    fragClumpId = clumpId;
    fragWorldPos = worldPos;
}
