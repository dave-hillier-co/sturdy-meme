#version 450

const int NUM_CASCADES = 4;

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

// Leaf instance data - matches compute shader output
struct LeafInstance {
    vec4 position;      // xyz = world position, w = size
    vec4 facing;        // xy = facing direction, z = hash, w = flutter
    vec4 color;         // rgb = color tint, a = alpha
    uvec4 metadata;     // x = tree index, y = flags, z = unused, w = unused
};

layout(std430, binding = 1) readonly buffer LeafBuffer {
    LeafInstance leaves[];
};

// Wind uniform buffer
layout(binding = 3) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    int cascadeIndex;
    float padding[2];
} push;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragColor;
layout(location = 4) out float fragDepth;

// Simple hash for wind variation
float hash(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(dot(i, vec2(127.1, 311.7)));
    float b = hash(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7)));
    float c = hash(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7)));
    float d = hash(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7)));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sampleWind(vec2 worldPos) {
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windTime = wind.windParams.w;

    vec2 scrolledPos = worldPos - windDir * windTime * 1.0;
    float n = noise(scrolledPos * 0.1);
    return n * windStrength;
}

void main() {
    uint leafIndex = gl_InstanceIndex;
    uint vertIndex = gl_VertexIndex;

    LeafInstance leaf = leaves[leafIndex];

    vec3 leafPos = leaf.position.xyz;
    float leafSize = leaf.position.w;
    vec2 facingDir = leaf.facing.xy;
    float leafHash = leaf.facing.z;
    float flutter = leaf.facing.w;
    vec3 leafColor = leaf.color.rgb;

    // Get camera direction for billboarding
    vec3 toCamera = normalize(ubo.cameraPosition.xyz - leafPos);

    // Create billboard axes
    // For a more natural look, we'll use a constrained billboard that keeps Y up
    vec3 up = vec3(0.0, 1.0, 0.0);

    // Add some tilt based on facing direction
    up = normalize(up + vec3(facingDir.x, 0.0, facingDir.y) * 0.3);

    vec3 right = normalize(cross(up, toCamera));
    vec3 forward = cross(right, up);

    // Quad vertex positions (CCW winding)
    // vertIndex: 0=BL, 1=BR, 2=TR, 3=BL, 4=TR, 5=TL
    vec2 quadPos;
    vec2 uv;

    if (vertIndex == 0u) {
        quadPos = vec2(-0.5, -0.5);
        uv = vec2(0.0, 1.0);
    } else if (vertIndex == 1u) {
        quadPos = vec2(0.5, -0.5);
        uv = vec2(1.0, 1.0);
    } else if (vertIndex == 2u) {
        quadPos = vec2(0.5, 0.5);
        uv = vec2(1.0, 0.0);
    } else if (vertIndex == 3u) {
        quadPos = vec2(-0.5, -0.5);
        uv = vec2(0.0, 1.0);
    } else if (vertIndex == 4u) {
        quadPos = vec2(0.5, 0.5);
        uv = vec2(1.0, 0.0);
    } else {  // vertIndex == 5
        quadPos = vec2(-0.5, 0.5);
        uv = vec2(0.0, 0.0);
    }

    // Apply leaf size
    vec3 offset = right * quadPos.x * leafSize + up * quadPos.y * leafSize;
    vec3 worldPos = leafPos + offset;

    // Apply wind animation
    float windSample = sampleWind(leafPos.xz);
    vec2 windDir2D = wind.windDirectionAndStrength.xy;

    // Base wind sway
    float sway = windSample * 0.3;

    // Add flutter (high frequency oscillation)
    float flutterPhase = push.time * 8.0 + leafHash * 6.28318;
    float flutterAmount = sin(flutterPhase) * flutter * windSample;

    vec3 windOffset = vec3(windDir2D.x, 0.0, windDir2D.y) * sway;

    // Add perpendicular flutter
    vec3 perpDir = vec3(-windDir2D.y, 0.2, windDir2D.x);
    windOffset += perpDir * flutterAmount;

    worldPos += windOffset;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Output
    fragWorldPos = worldPos;
    fragNormal = toCamera;  // Normal faces camera for billboard
    fragUV = uv;
    fragColor = leafColor;
    fragDepth = -(ubo.view * vec4(worldPos, 1.0)).z;
}
