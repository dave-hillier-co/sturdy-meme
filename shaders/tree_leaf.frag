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

layout(binding = 2) uniform sampler2DArray shadowMap;

// Light buffer for multiple lights
struct Light {
    vec4 positionAndType;    // xyz = position, w = type
    vec4 directionAndRange;  // xyz = direction, w = range
    vec4 colorAndIntensity;  // xyz = color, w = intensity
    vec4 params;             // spotlight params
};

layout(std430, binding = 4) buffer LightBuffer {
    uint lightCount;
    Light lights[];
};

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragColor;
layout(location = 4) in float fragDepth;

layout(location = 0) out vec4 outColor;

// PCF shadow sampling
float sampleShadowPCF(int cascade, vec3 shadowCoord) {
    float shadow = 0.0;
    float texelSize = 1.0 / ubo.shadowMapSize;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            float depth = texture(shadowMap, vec3(shadowCoord.xy + offset, float(cascade))).r;
            shadow += shadowCoord.z - 0.002 > depth ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

float calculateShadow() {
    // Determine cascade
    int cascade = 3;
    for (int i = 0; i < NUM_CASCADES; i++) {
        if (fragDepth < ubo.cascadeSplits[i]) {
            cascade = i;
            break;
        }
    }

    // Transform to shadow space
    vec4 shadowPos = ubo.cascadeViewProj[cascade] * vec4(fragWorldPos, 1.0);
    vec3 shadowCoord = shadowPos.xyz / shadowPos.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;
    }

    return sampleShadowPCF(cascade, shadowCoord);
}

void main() {
    // Create a simple leaf shape using UV
    vec2 centered = fragUV * 2.0 - 1.0;

    // Ellipse shape for leaf
    float leafShape = 1.0 - length(centered * vec2(1.0, 0.7));
    leafShape = smoothstep(0.0, 0.3, leafShape);

    // Add a bit of alpha variation for softer edges
    if (leafShape < 0.1) {
        discard;
    }

    // Base leaf color from vertex shader
    vec3 baseColor = fragColor;

    // Sun direction
    vec3 lightDir = normalize(ubo.sunDirection.xyz);
    vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Simple lighting for billboard
    // Normal faces camera, so use a fake normal for lighting that's slightly up
    vec3 fakeNormal = normalize(fragNormal + vec3(0.0, 0.5, 0.0));

    float NdotL = dot(fakeNormal, lightDir);
    float frontLight = max(0.0, NdotL);

    // Translucency - light from behind passes through leaf
    float backLight = max(0.0, -NdotL) * 0.6;

    // Warm the transmitted light (like sunlight through a leaf)
    vec3 translucentColor = baseColor * vec3(1.3, 1.1, 0.6);

    // Shadow
    float shadow = calculateShadow();

    // Combine lighting
    vec3 sunColor = ubo.sunColor.rgb;
    vec3 ambient = ubo.ambientColor.rgb * 0.4;

    vec3 directLight = baseColor * frontLight * sunColor * shadow;
    vec3 backLitLight = translucentColor * backLight * sunColor * shadow * 0.5;

    vec3 finalColor = ambient * baseColor + directLight + backLitLight;

    // Slightly boost saturation
    float luminance = dot(finalColor, vec3(0.299, 0.587, 0.114));
    finalColor = mix(vec3(luminance), finalColor, 1.2);

    // Apply leaf shape alpha
    float alpha = leafShape;

    outColor = vec4(finalColor, alpha);
}
