#version 450

/*
 * terrain.frag - Terrain fragment shader
 * Simple PBR-based terrain rendering with shadow support
 */

const float PI = 3.14159265359;
const int NUM_CASCADES = 4;

// Atmospheric parameters (matched with grass/main shaders)
const float PLANET_RADIUS = 6371.0;
const float ATMOSPHERE_RADIUS = 6471.0;
const vec3 RAYLEIGH_SCATTERING_BASE = vec3(5.802e-3, 13.558e-3, 33.1e-3);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;
const float MIE_SCATTERING_BASE = 3.996e-3;
const float MIE_ABSORPTION_BASE = 4.4e-3;
const float MIE_SCALE_HEIGHT = 1.2;
const float MIE_ANISOTROPY = 0.8;
const vec3 OZONE_ABSORPTION = vec3(0.65e-3, 1.881e-3, 0.085e-3);
const float OZONE_LAYER_CENTER = 25.0;
const float OZONE_LAYER_WIDTH = 15.0;

// Height fog parameters (Phase 4.3 - Volumetric Haze)
const float FOG_BASE_HEIGHT = 0.0;        // Ground level
const float FOG_SCALE_HEIGHT = 50.0;      // Exponential falloff height in scene units
const float FOG_DENSITY = 0.015;          // Base fog density
const float FOG_LAYER_THICKNESS = 10.0;   // Low-lying fog layer thickness
const float FOG_LAYER_DENSITY = 0.03;     // Low-lying fog density

// Use the same UBO as main scene for consistent lighting
layout(binding = 5) uniform UniformBufferObject {
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
    vec4 windDirectionAndSpeed;  // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;             // Julian day for sidereal rotation
} ubo;

// Terrain-specific uniforms
layout(std140, binding = 4) uniform TerrainUniforms {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 viewProjMatrix;
    vec4 frustumPlanes[6];
    vec4 terrainCameraPosition;
    vec4 terrainParams;
    vec4 lodParams;
    vec2 screenSize;
    float lodFactor;
    float terrainPadding;
};

// Textures
layout(binding = 3) uniform sampler2D heightMap;
layout(binding = 6) uniform sampler2D terrainAlbedo;
layout(binding = 7) uniform sampler2DArrayShadow shadowMapArray;

// Inputs from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragDepth;

layout(location = 0) out vec4 outColor;

// Shadow sampling
float sampleShadow(int cascade, vec3 worldPos) {
    vec4 lightSpacePos = ubo.cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform to [0, 1] range for sampling
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check bounds
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    // PCF shadow sampling
    float shadow = 0.0;
    float texelSize = 1.0 / ubo.shadowMapSize;
    float bias = 0.002;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec4 sampleCoord = vec4(
                projCoords.xy + vec2(x, y) * texelSize,
                float(cascade),
                projCoords.z - bias
            );
            shadow += texture(shadowMapArray, sampleCoord);
        }
    }

    return shadow / 9.0;
}

float getShadowFactor(vec3 worldPos) {
    // Calculate view-space depth
    vec4 viewPos = ubo.view * vec4(worldPos, 1.0);
    float depth = -viewPos.z;

    // Determine cascade
    int cascade = 3;
    if (depth < ubo.cascadeSplits.x) cascade = 0;
    else if (depth < ubo.cascadeSplits.y) cascade = 1;
    else if (depth < ubo.cascadeSplits.z) cascade = 2;

    return sampleShadow(cascade, worldPos);
}

// GGX/Trowbridge-Reitz normal distribution
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// Schlick-GGX geometry function
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// Atmospheric Scattering Functions (matched with grass.frag)
// ============================================================================

struct ScatteringResult {
    vec3 inscatter;
    vec3 transmittance;
};

float rayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

float cornetteShanksPhase(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

vec2 raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return vec2(1e9, -1e9);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

float ozoneDensity(float altitude) {
    float z = (altitude - OZONE_LAYER_CENTER) / OZONE_LAYER_WIDTH;
    return exp(-0.5 * z * z);
}

ScatteringResult integrateAtmosphere(vec3 origin, vec3 dir, float maxDistance, int sampleCount) {
    vec2 atmo = raySphereIntersect(origin, dir, ATMOSPHERE_RADIUS);
    float start = max(atmo.x, 0.0);
    float end = min(atmo.y, maxDistance);

    if (end <= 0.0) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    vec2 planet = raySphereIntersect(origin, dir, PLANET_RADIUS);
    if (planet.x > 0.0) {
        end = min(end, planet.x);
    }

    if (end <= start) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    float stepSize = (end - start) / float(sampleCount);
    vec3 transmittance = vec3(1.0);
    vec3 inscatter = vec3(0.0);

    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    float cosViewSun = dot(dir, sunDir);
    float rayleighP = rayleighPhase(cosViewSun);
    float mieP = cornetteShanksPhase(cosViewSun, MIE_ANISOTROPY);

    for (int i = 0; i < sampleCount; i++) {
        float t = start + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;
        float altitude = max(length(pos) - PLANET_RADIUS, 0.0);

        float rayleighDensity = exp(-altitude / RAYLEIGH_SCALE_HEIGHT);
        float mieDensity = exp(-altitude / MIE_SCALE_HEIGHT);
        float ozone = ozoneDensity(altitude);

        vec3 rayleighScatter = rayleighDensity * RAYLEIGH_SCATTERING_BASE;
        vec3 mieScatter = mieDensity * vec3(MIE_SCATTERING_BASE);

        vec3 extinction = rayleighScatter + mieScatter +
                          mieDensity * vec3(MIE_ABSORPTION_BASE) +
                          ozone * OZONE_ABSORPTION;

        vec3 segmentScatter = rayleighScatter * rayleighP + mieScatter * mieP;

        vec3 attenuation = exp(-extinction * stepSize);
        inscatter += transmittance * segmentScatter * stepSize;
        transmittance *= attenuation;
    }

    return ScatteringResult(inscatter, transmittance);
}

// ============================================================================
// Height Fog Functions (Phase 4.3 - Volumetric Haze)
// ============================================================================

// Exponential height falloff - good for general atmospheric haze
float exponentialHeightDensity(float height) {
    float relativeHeight = height - FOG_BASE_HEIGHT;
    return FOG_DENSITY * exp(-max(relativeHeight, 0.0) / FOG_SCALE_HEIGHT);
}

// Sigmoidal layer density - good for low-lying ground fog
float sigmoidalLayerDensity(float height) {
    float t = (height - FOG_BASE_HEIGHT) / FOG_LAYER_THICKNESS;
    // Smooth transition from full density below to zero above
    return FOG_LAYER_DENSITY / (1.0 + exp(t * 2.0));
}

// Combined fog density at a given height
float getHeightFogDensity(float height) {
    return exponentialHeightDensity(height) + sigmoidalLayerDensity(height);
}

// Analytically integrate exponential fog density along a ray
// Returns optical depth (for transmittance calculation)
float integrateExponentialFog(vec3 startPos, vec3 endPos) {
    float h0 = startPos.y;
    float h1 = endPos.y;
    float distance = length(endPos - startPos);

    if (distance < 0.001) return 0.0;

    float deltaH = h1 - h0;

    // For nearly horizontal rays, use simple density * distance
    if (abs(deltaH) < 0.01) {
        float avgHeight = (h0 + h1) * 0.5;
        return getHeightFogDensity(avgHeight) * distance;
    }

    // Analytical integration of exponential density along ray
    float invScaleHeight = 1.0 / FOG_SCALE_HEIGHT;

    // Exponential fog component
    float expIntegral = FOG_DENSITY * FOG_SCALE_HEIGHT *
        abs(exp(-(max(h0 - FOG_BASE_HEIGHT, 0.0)) * invScaleHeight) -
            exp(-(max(h1 - FOG_BASE_HEIGHT, 0.0)) * invScaleHeight)) /
        max(abs(deltaH / distance), 0.001);

    // Sigmoidal component (approximate with average)
    float avgSigmoidal = (sigmoidalLayerDensity(h0) + sigmoidalLayerDensity(h1)) * 0.5;
    float sigIntegral = avgSigmoidal * distance;

    return expIntegral + sigIntegral;
}

// Apply height fog with in-scattering
vec3 applyHeightFog(vec3 color, vec3 cameraPos, vec3 fragPos, vec3 sunDir, vec3 sunColor) {
    vec3 viewDir = fragPos - cameraPos;
    float viewDistance = length(viewDir);
    viewDir = normalize(viewDir);

    // Calculate fog optical depth along the view ray
    float opticalDepth = integrateExponentialFog(cameraPos, fragPos);

    // Beer-Lambert transmittance
    float transmittance = exp(-opticalDepth);

    // In-scattering from sun (with Mie-like phase function for forward scattering)
    float cosTheta = dot(viewDir, sunDir);
    float phase = cornetteShanksPhase(cosTheta, 0.6);  // Slightly lower g for fog

    // Sun visibility (above horizon and not blocked by terrain)
    float sunVisibility = smoothstep(-0.1, 0.1, sunDir.y);

    // Fog color: blend between sun-lit and ambient based on sun angle
    vec3 fogSunColor = sunColor * phase * sunVisibility;

    // Ambient sky light contribution (approximate hemisphere irradiance)
    float night = 1.0 - smoothstep(-0.05, 0.08, sunDir.y);
    vec3 ambientFog = mix(vec3(0.4, 0.5, 0.6), vec3(0.02, 0.03, 0.05), night);

    // Combined in-scatter (energy conserving)
    vec3 inScatter = (fogSunColor + ambientFog * 0.3) * (1.0 - transmittance);

    return color * transmittance + inScatter;
}

// Apply aerial perspective (combined height fog + atmospheric scattering)
vec3 applyAerialPerspective(vec3 color, vec3 viewDir, float viewDistance) {
    vec3 cameraPos = ubo.cameraPosition.xyz;
    vec3 fragPos = cameraPos + viewDir;

    // Apply local height fog first (scene scale)
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 fogged = applyHeightFog(color, cameraPos, fragPos, sunDir, sunColor);

    // Then apply large-scale atmospheric scattering (km scale)
    vec3 origin = vec3(0.0, PLANET_RADIUS + max(cameraPos.y, 0.0), 0.0);
    ScatteringResult result = integrateAtmosphere(origin, normalize(viewDir), viewDistance, 8);

    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 scatterLight = result.inscatter * (sunLight + vec3(0.02));

    float night = 1.0 - smoothstep(-0.05, 0.08, ubo.sunDirection.y);
    scatterLight += night * vec3(0.01, 0.015, 0.03) * (1.0 - result.transmittance);

    // Combine: atmospheric scattering adds to fogged scene
    // Use reduced atmospheric effect since we're at scene scale
    float atmoBlend = clamp(viewDistance * 0.001, 0.0, 0.3);
    vec3 finalColor = mix(fogged, fogged * result.transmittance + scatterLight, atmoBlend);

    return finalColor;
}

void main() {
    // Sample terrain albedo with tiling
    vec3 albedo = texture(terrainAlbedo, fragTexCoord * 50.0).rgb;

    // Add some variation based on slope
    vec3 normal = normalize(fragNormal);
    float slope = 1.0 - normal.y;

    // Blend in rock color on steep slopes
    vec3 rockColor = vec3(0.4, 0.35, 0.3);
    vec3 grassColor = albedo;
    albedo = mix(grassColor, rockColor, smoothstep(0.3, 0.6, slope));

    // Material properties
    float roughness = mix(0.8, 0.95, slope);  // Rougher on slopes
    float metallic = 0.0;  // Terrain is non-metallic

    // View direction
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Calculate sun contribution
    vec3 L = normalize(ubo.sunDirection.xyz);
    vec3 H = normalize(V + L);

    // PBR calculations
    vec3 F0 = vec3(0.04);  // Non-metallic
    F0 = mix(F0, albedo, metallic);

    float NDF = distributionGGX(normal, H, roughness);
    float G = geometrySmith(normal, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(normal, L), 0.0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(normal, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 radiance = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 sunLight = (kD * albedo / PI + specular) * radiance * NdotL;

    // Shadow
    float shadow = getShadowFactor(fragWorldPos);

    // === MOON LIGHTING ===
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    vec3 moonH = normalize(V + moonL);
    float moonNdotL = max(dot(normal, moonL), 0.0);

    // Moon PBR (simplified - less specular contribution)
    float moonNDF = distributionGGX(normal, moonH, roughness);
    float moonG = geometrySmith(normal, V, moonL, roughness);
    vec3 moonF = fresnelSchlick(max(dot(moonH, V), 0.0), F0);

    vec3 moonKS = moonF;
    vec3 moonKD = (vec3(1.0) - moonKS) * (1.0 - metallic);

    vec3 moonNumerator = moonNDF * moonG * moonF;
    float moonDenominator = 4.0 * max(dot(normal, V), 0.0) * moonNdotL + 0.0001;
    vec3 moonSpecular = moonNumerator / moonDenominator;

    vec3 moonRadiance = ubo.moonColor.rgb * ubo.moonDirection.w;
    vec3 moonLight = (moonKD * albedo / PI + moonSpecular * 0.5) * moonRadiance * moonNdotL;

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb * albedo;

    // Combine lighting (sun with shadows, moon without for soft night light)
    vec3 color = ambient + sunLight * shadow + moonLight;

    // Debug: visualize LOD depth
    #if 0
    float maxDepth = lodParams.w;
    float t = fragDepth / maxDepth;
    color = mix(color, vec3(t, 1.0 - t, 0.0), 0.3);
    #endif

    // === AERIAL PERSPECTIVE ===
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDistance = length(cameraToFrag);
    vec3 atmosphericColor = applyAerialPerspective(color, cameraToFrag, viewDistance);

    // Output
    outColor = vec4(atmosphericColor, 1.0);
}
