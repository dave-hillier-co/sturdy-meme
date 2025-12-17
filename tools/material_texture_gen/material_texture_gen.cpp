// Procedural material texture generator for virtual texturing
// Generates realistic textures for all biome materials using advanced procedural techniques

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <lodepng.h>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>
#include <functional>
#include <random>

namespace fs = std::filesystem;

// Texture size for generated materials
constexpr int TEXTURE_SIZE = 512;

// ============================================================================
// Advanced Noise Functions
// ============================================================================

// Hash function for reproducible randomness
float hash(glm::vec2 p) {
    return glm::fract(std::sin(glm::dot(p, glm::vec2(127.1f, 311.7f))) * 43758.5453f);
}

glm::vec2 hash2(glm::vec2 p) {
    return glm::vec2(
        glm::fract(std::sin(glm::dot(p, glm::vec2(127.1f, 311.7f))) * 43758.5453f),
        glm::fract(std::sin(glm::dot(p, glm::vec2(269.5f, 183.3f))) * 43758.5453f)
    );
}

glm::vec3 hash3(glm::vec2 p) {
    return glm::vec3(
        glm::fract(std::sin(glm::dot(p, glm::vec2(127.1f, 311.7f))) * 43758.5453f),
        glm::fract(std::sin(glm::dot(p, glm::vec2(269.5f, 183.3f))) * 43758.5453f),
        glm::fract(std::sin(glm::dot(p, glm::vec2(419.2f, 371.9f))) * 43758.5453f)
    );
}

// Fractal Brownian Motion
float fbm(glm::vec2 p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * glm::simplex(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

// Turbulence (absolute value FBM)
float turbulence(glm::vec2 p, int octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * std::abs(glm::simplex(p * frequency));
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    return value;
}

// Voronoi noise with F1 and F2 distances
struct VoronoiResult {
    float f1;      // Distance to nearest point
    float f2;      // Distance to second nearest
    glm::vec2 id;  // Cell ID of nearest point
};

VoronoiResult voronoi(glm::vec2 p, float jitter = 1.0f) {
    glm::ivec2 cell = glm::ivec2(glm::floor(p));
    glm::vec2 frac = glm::fract(p);

    float d1 = 10.0f;
    float d2 = 10.0f;
    glm::vec2 nearestId(0.0f);

    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            glm::ivec2 neighbor = cell + glm::ivec2(x, y);
            glm::vec2 point = hash2(glm::vec2(neighbor)) * jitter;
            glm::vec2 diff = point + glm::vec2(x, y) - frac;
            float dist = glm::length(diff);

            if (dist < d1) {
                d2 = d1;
                d1 = dist;
                nearestId = glm::vec2(neighbor);
            } else if (dist < d2) {
                d2 = dist;
            }
        }
    }

    return {d1, d2, nearestId};
}

// Domain warping for organic patterns
glm::vec2 domainWarp(glm::vec2 p, float strength, float scale) {
    float warpX = fbm(p * scale, 4);
    float warpY = fbm(p * scale + glm::vec2(5.2f, 1.3f), 4);
    return p + glm::vec2(warpX, warpY) * strength;
}

// Ridged noise for mountain/crack patterns
float ridgedNoise(glm::vec2 p, int octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        float n = 1.0f - std::abs(glm::simplex(p * frequency));
        value += amplitude * n * n;
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    return value;
}

// ============================================================================
// Material-Specific Pattern Generators
// ============================================================================

// Grass pattern: directional blade-like streaks with clumps
glm::vec3 generateGrass(glm::vec2 uv, glm::vec3 lightColor, glm::vec3 darkColor,
                         glm::vec3 tipColor, float bladeScale = 40.0f) {
    // Create grass blade direction variation
    float dirNoise = fbm(uv * 2.0f, 3) * 0.3f;
    glm::vec2 bladeDir = glm::normalize(glm::vec2(0.1f + dirNoise, 1.0f));

    // Stretched noise along blade direction for streaky appearance
    glm::vec2 stretchedUV = glm::vec2(
        glm::dot(uv * bladeScale, bladeDir),
        glm::dot(uv * bladeScale, glm::vec2(-bladeDir.y, bladeDir.x)) * 3.0f
    );

    // Multiple scales of blade patterns
    float blades1 = glm::simplex(stretchedUV) * 0.5f + 0.5f;
    float blades2 = glm::simplex(stretchedUV * 2.3f + glm::vec2(10.0f)) * 0.5f + 0.5f;
    float blades3 = glm::simplex(stretchedUV * 4.7f + glm::vec2(20.0f)) * 0.5f + 0.5f;

    // Combine blade layers
    float bladePattern = blades1 * 0.5f + blades2 * 0.3f + blades3 * 0.2f;

    // Add clump variation using Voronoi
    VoronoiResult clumps = voronoi(uv * 8.0f, 0.9f);
    float clumpVar = glm::smoothstep(0.0f, 0.4f, clumps.f1);

    // Color variation based on clump
    float colorVar = hash(clumps.id) * 0.3f;
    glm::vec3 clumpColor = glm::mix(darkColor, lightColor, colorVar);

    // Blade tips are lighter/yellower
    float tipAmount = std::pow(bladePattern, 2.0f) * 0.4f;

    // Final color
    glm::vec3 baseColor = glm::mix(darkColor, clumpColor, bladePattern * clumpVar);
    return glm::mix(baseColor, tipColor, tipAmount);
}

// Sand pattern: fine grains with ripples
glm::vec3 generateSand(glm::vec2 uv, glm::vec3 lightColor, glm::vec3 darkColor) {
    // Fine grain noise at high frequency
    float grainScale = 80.0f;
    float grain1 = hash(glm::floor(uv * grainScale));
    float grain2 = hash(glm::floor(uv * grainScale * 1.7f + glm::vec2(0.5f)));
    float grain = grain1 * 0.6f + grain2 * 0.4f;

    // Larger scale variation (wind ripples)
    float rippleScale = 15.0f;
    glm::vec2 rippleUV = uv * rippleScale;
    float ripple = glm::simplex(glm::vec2(rippleUV.x * 0.3f, rippleUV.y)) * 0.5f + 0.5f;
    ripple = std::pow(ripple, 1.5f);

    // Medium scale patches
    float patches = fbm(uv * 6.0f, 4) * 0.5f + 0.5f;

    // Combine scales
    float pattern = grain * 0.3f + ripple * 0.4f + patches * 0.3f;

    // Occasional darker grains (small pebbles/shells)
    float darkGrains = std::pow(hash(glm::floor(uv * 30.0f)), 8.0f);

    glm::vec3 color = glm::mix(darkColor, lightColor, pattern);
    return glm::mix(color, darkColor * 0.7f, darkGrains * 0.5f);
}

// Rock pattern: cracks, layers, and surface detail
glm::vec3 generateRock(glm::vec2 uv, glm::vec3 lightColor, glm::vec3 darkColor,
                        glm::vec3 crackColor) {
    // Domain warping for organic rock appearance
    glm::vec2 warpedUV = domainWarp(uv, 0.15f, 4.0f);

    // Layered rock strata
    float strata = std::sin(warpedUV.y * 20.0f + fbm(uv * 3.0f, 3) * 2.0f) * 0.5f + 0.5f;
    strata = glm::smoothstep(0.3f, 0.7f, strata);

    // Crack pattern using Voronoi edges
    VoronoiResult cracks = voronoi(warpedUV * 6.0f, 0.8f);
    float crackPattern = glm::smoothstep(0.0f, 0.08f, cracks.f2 - cracks.f1);

    // Surface detail
    float detail = turbulence(uv * 15.0f, 4) * 0.3f;

    // Large scale color variation
    float largeVar = fbm(uv * 2.0f, 3) * 0.5f + 0.5f;

    // Combine
    glm::vec3 rockColor = glm::mix(darkColor, lightColor, strata * 0.5f + largeVar * 0.5f);
    rockColor = glm::mix(rockColor, rockColor * (1.0f + detail), 0.5f);

    // Add cracks
    return glm::mix(crackColor, rockColor, crackPattern);
}

// Mud pattern: dried cracks and wet patches
glm::vec3 generateMud(glm::vec2 uv, glm::vec3 lightColor, glm::vec3 darkColor) {
    // Cracked mud pattern using Voronoi
    VoronoiResult cracks = voronoi(uv * 5.0f, 0.85f);
    float crackLines = glm::smoothstep(0.0f, 0.06f, cracks.f2 - cracks.f1);

    // Surface variation within each cell
    float cellVar = hash(cracks.id) * 0.4f + 0.3f;
    float surfaceDetail = fbm(uv * 20.0f, 3) * 0.15f;

    // Wet patches (darker areas)
    float wetness = fbm(uv * 3.0f + glm::vec2(10.0f), 4) * 0.5f + 0.5f;
    wetness = glm::smoothstep(0.4f, 0.6f, wetness);

    // Combine
    glm::vec3 dryColor = glm::mix(darkColor, lightColor, cellVar + surfaceDetail);
    glm::vec3 wetColor = darkColor * 0.7f;
    glm::vec3 mudColor = glm::mix(dryColor, wetColor, wetness * 0.5f);

    // Cracks are darker
    return glm::mix(darkColor * 0.5f, mudColor, crackLines);
}

// Pebble pattern: rounded stones with shadows
glm::vec3 generatePebbles(glm::vec2 uv, glm::vec3 baseColor) {
    glm::vec3 result(0.0f);
    float totalWeight = 0.0f;

    // Multiple scales of pebbles
    for (int scale = 0; scale < 3; scale++) {
        float pebbleScale = 8.0f + scale * 6.0f;
        VoronoiResult pebbles = voronoi(uv * pebbleScale, 0.9f);

        // Pebble shape (rounded)
        float pebbleShape = glm::smoothstep(0.4f, 0.0f, pebbles.f1);

        // Each pebble has unique color
        glm::vec3 pebbleColor = baseColor * (0.7f + hash(pebbles.id) * 0.6f);
        // Add some color variation
        pebbleColor.r *= 0.9f + hash(pebbles.id + glm::vec2(1.0f)) * 0.2f;
        pebbleColor.g *= 0.9f + hash(pebbles.id + glm::vec2(2.0f)) * 0.2f;

        // Shading on pebble (simple hemisphere lighting)
        glm::vec2 toCenter = glm::vec2(0.0f) - (glm::fract(uv * pebbleScale) - glm::vec2(0.5f));
        float shade = glm::dot(glm::normalize(toCenter), glm::vec2(0.5f, 0.7f)) * 0.3f + 0.7f;

        float weight = (3 - scale) * pebbleShape;
        result += pebbleColor * shade * weight;
        totalWeight += weight;
    }

    // Background between pebbles
    float bgNoise = fbm(uv * 30.0f, 3) * 0.1f;
    glm::vec3 bgColor = baseColor * (0.4f + bgNoise);

    if (totalWeight > 0.01f) {
        return glm::mix(bgColor, result / totalWeight, glm::clamp(totalWeight, 0.0f, 1.0f));
    }
    return bgColor;
}

// Chalk pattern: white with subtle gray veins
glm::vec3 generateChalk(glm::vec2 uv, glm::vec3 whiteColor, glm::vec3 grayColor) {
    // Soft layered appearance
    glm::vec2 warpedUV = domainWarp(uv, 0.1f, 3.0f);
    float layers = fbm(warpedUV * 8.0f, 5) * 0.5f + 0.5f;

    // Subtle veins
    VoronoiResult veins = voronoi(uv * 12.0f, 0.7f);
    float veinPattern = glm::smoothstep(0.02f, 0.08f, veins.f2 - veins.f1);
    veinPattern = 1.0f - (1.0f - veinPattern) * 0.3f; // Subtle veins

    // Surface texture
    float surface = turbulence(uv * 25.0f, 3) * 0.1f;

    glm::vec3 chalkColor = glm::mix(grayColor, whiteColor, layers * 0.7f + 0.3f);
    chalkColor = chalkColor * veinPattern;
    chalkColor = chalkColor * (1.0f + surface);

    return glm::clamp(chalkColor, glm::vec3(0.0f), glm::vec3(1.0f));
}

// Wildflower meadow: grass with colored flower spots
glm::vec3 generateWildflowers(glm::vec2 uv, glm::vec3 grassLight, glm::vec3 grassDark) {
    // Base grass
    glm::vec3 grass = generateGrass(uv, grassLight, grassDark,
                                     glm::vec3(0.5f, 0.55f, 0.3f), 35.0f);

    // Flower placement using multi-scale Voronoi
    std::vector<glm::vec3> flowerColors = {
        glm::vec3(0.95f, 0.9f, 0.2f),   // Yellow (buttercup)
        glm::vec3(0.9f, 0.3f, 0.8f),    // Purple (orchid)
        glm::vec3(1.0f, 1.0f, 0.95f),   // White (daisy)
        glm::vec3(0.9f, 0.2f, 0.3f),    // Red (poppy)
        glm::vec3(0.3f, 0.4f, 0.9f)     // Blue (cornflower)
    };

    // Sparse flower distribution
    VoronoiResult flowers = voronoi(uv * 25.0f, 0.95f);
    float flowerMask = glm::smoothstep(0.15f, 0.05f, flowers.f1);

    // Only some cells have flowers
    float hasFlower = std::pow(hash(flowers.id), 2.0f);
    flowerMask *= glm::step(0.7f, hasFlower);

    if (flowerMask > 0.01f) {
        // Pick flower color based on cell
        int colorIdx = int(hash(flowers.id + glm::vec2(10.0f)) * 5.0f) % 5;
        glm::vec3 flowerColor = flowerColors[colorIdx];

        // Add center to flower
        float center = glm::smoothstep(0.08f, 0.02f, flowers.f1);
        glm::vec3 centerColor = glm::vec3(0.9f, 0.8f, 0.2f); // Yellow center
        flowerColor = glm::mix(flowerColor, centerColor, center);

        return glm::mix(grass, flowerColor, flowerMask);
    }

    return grass;
}

// Forest floor: leaves, twigs, moss
glm::vec3 generateForestFloor(glm::vec2 uv, glm::vec3 dirtColor, glm::vec3 leafColor,
                               glm::vec3 mossColor) {
    // Base dirt/soil
    float soilNoise = fbm(uv * 12.0f, 4) * 0.5f + 0.5f;
    glm::vec3 soil = dirtColor * (0.8f + soilNoise * 0.4f);

    // Scattered leaves using Voronoi
    VoronoiResult leaves = voronoi(uv * 15.0f, 0.85f);
    float leafMask = glm::smoothstep(0.25f, 0.1f, leaves.f1);

    // Each leaf has slight color variation
    glm::vec3 thisLeafColor = leafColor * (0.7f + hash(leaves.id) * 0.6f);
    // Some leaves are more brown/decomposed
    float decomposed = hash(leaves.id + glm::vec2(5.0f));
    thisLeafColor = glm::mix(thisLeafColor, dirtColor * 1.2f, decomposed * 0.5f);

    // Moss patches
    float mossPattern = fbm(uv * 6.0f, 4) * 0.5f + 0.5f;
    mossPattern = glm::smoothstep(0.55f, 0.7f, mossPattern);

    // Combine
    glm::vec3 result = glm::mix(soil, thisLeafColor, leafMask * 0.8f);
    result = glm::mix(result, mossColor, mossPattern * 0.4f);

    // Add small twig details
    float twigs = ridgedNoise(uv * 30.0f, 3);
    twigs = glm::smoothstep(0.7f, 0.9f, twigs) * 0.3f;
    result = glm::mix(result, dirtColor * 0.5f, twigs);

    return result;
}

// Road/path pattern: compressed dirt or tarmac
glm::vec3 generatePath(glm::vec2 uv, glm::vec3 baseColor, bool isTarmac) {
    if (isTarmac) {
        // Tarmac: dark with aggregate specks
        float aggregate = hash(glm::floor(uv * 100.0f));
        float largeVar = fbm(uv * 3.0f, 3) * 0.1f;

        glm::vec3 color = baseColor * (0.9f + largeVar);
        // Light aggregate specks
        if (aggregate > 0.85f) {
            color = glm::mix(color, glm::vec3(0.5f), 0.3f);
        }

        // Subtle cracks
        VoronoiResult cracks = voronoi(uv * 4.0f, 0.6f);
        float crackLine = glm::smoothstep(0.02f, 0.05f, cracks.f2 - cracks.f1);
        color = glm::mix(baseColor * 0.5f, color, crackLine);

        return color;
    } else {
        // Dirt path: compressed earth with stones
        float compacted = fbm(uv * 8.0f, 4) * 0.3f;
        glm::vec3 color = baseColor * (0.85f + compacted);

        // Embedded small stones
        VoronoiResult stones = voronoi(uv * 20.0f, 0.8f);
        float stoneMask = glm::smoothstep(0.2f, 0.1f, stones.f1);
        glm::vec3 stoneColor = glm::vec3(0.55f, 0.52f, 0.48f) * (0.8f + hash(stones.id) * 0.4f);

        color = glm::mix(color, stoneColor, stoneMask * 0.6f);

        // Tire/foot track impressions
        float tracks = std::sin(uv.x * 50.0f + fbm(uv * 5.0f, 2) * 3.0f);
        tracks = glm::smoothstep(-0.2f, 0.2f, tracks) * 0.1f;
        color *= (1.0f - tracks);

        return color;
    }
}

// Water texture (for puddles/shallow water)
glm::vec3 generateWater(glm::vec2 uv, glm::vec3 shallowColor, glm::vec3 deepColor) {
    // Caustic-like pattern
    float caustic1 = voronoi(uv * 8.0f, 0.7f).f1;
    float caustic2 = voronoi(uv * 8.0f + glm::vec2(0.5f), 0.7f).f1;
    float caustics = (caustic1 + caustic2) * 0.5f;
    caustics = std::pow(caustics, 0.5f);

    // Depth variation
    float depth = fbm(uv * 3.0f, 4) * 0.5f + 0.5f;

    glm::vec3 color = glm::mix(deepColor, shallowColor, depth);
    color += glm::vec3(caustics * 0.15f);

    return color;
}

// Gorse pattern: spiky shrub with yellow flowers
glm::vec3 generateGorse(glm::vec2 uv, glm::vec3 greenColor) {
    // Spiky bush texture
    float spikes = turbulence(uv * 25.0f, 4);
    glm::vec3 bush = greenColor * (0.6f + spikes * 0.6f);

    // Yellow flowers scattered
    VoronoiResult flowers = voronoi(uv * 30.0f, 0.9f);
    float flowerMask = glm::smoothstep(0.12f, 0.05f, flowers.f1);
    flowerMask *= glm::step(0.75f, hash(flowers.id));

    glm::vec3 yellow(0.9f, 0.85f, 0.15f);
    return glm::mix(bush, yellow, flowerMask);
}

// Reeds pattern: vertical streaks
glm::vec3 generateReeds(glm::vec2 uv, glm::vec3 greenColor, glm::vec3 brownColor) {
    // Vertical reed stalks
    float reeds = std::sin(uv.x * 60.0f + fbm(uv * 3.0f, 2) * 2.0f);
    reeds = glm::smoothstep(-0.3f, 0.3f, reeds);

    // Height variation (tips are browner)
    float heightVar = fbm(glm::vec2(uv.x * 10.0f, 0.0f), 2) * 0.5f + 0.5f;

    // Base water/mud showing through
    float gapMask = glm::smoothstep(0.4f, 0.6f, reeds);

    glm::vec3 reedColor = glm::mix(brownColor, greenColor, heightVar);
    glm::vec3 mudColor = brownColor * 0.5f;

    return glm::mix(mudColor, reedColor, gapMask);
}

// ============================================================================
// Texture Generation and Output
// ============================================================================

bool generateTexture(const std::string& path,
                     std::function<glm::vec3(glm::vec2)> generator) {
    std::vector<unsigned char> pixels(TEXTURE_SIZE * TEXTURE_SIZE * 4);

    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            glm::vec2 uv = glm::vec2(float(x), float(y)) / float(TEXTURE_SIZE);

            glm::vec3 color = generator(uv);
            color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));

            int idx = (y * TEXTURE_SIZE + x) * 4;
            pixels[idx + 0] = static_cast<unsigned char>(color.r * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(color.g * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(color.b * 255.0f);
            pixels[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, pixels, TEXTURE_SIZE, TEXTURE_SIZE);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    SDL_Log("Generated: %s", path.c_str());
    return true;
}

bool generateNormalMap(const std::string& path,
                       std::function<float(glm::vec2)> heightFunc,
                       float strength = 1.0f) {
    std::vector<unsigned char> pixels(TEXTURE_SIZE * TEXTURE_SIZE * 4);

    // Generate height samples
    std::vector<float> heights(TEXTURE_SIZE * TEXTURE_SIZE);
    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            glm::vec2 uv = glm::vec2(float(x), float(y)) / float(TEXTURE_SIZE);
            heights[y * TEXTURE_SIZE + x] = heightFunc(uv);
        }
    }

    // Compute normals
    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            int x0 = (x - 1 + TEXTURE_SIZE) % TEXTURE_SIZE;
            int x1 = (x + 1) % TEXTURE_SIZE;
            int y0 = (y - 1 + TEXTURE_SIZE) % TEXTURE_SIZE;
            int y1 = (y + 1) % TEXTURE_SIZE;

            float dzdx = heights[y * TEXTURE_SIZE + x1] - heights[y * TEXTURE_SIZE + x0];
            float dzdy = heights[y1 * TEXTURE_SIZE + x] - heights[y0 * TEXTURE_SIZE + x];

            glm::vec3 normal = glm::normalize(glm::vec3(-dzdx * strength, -dzdy * strength, 1.0f));
            normal = normal * 0.5f + 0.5f;

            int idx = (y * TEXTURE_SIZE + x) * 4;
            pixels[idx + 0] = static_cast<unsigned char>(normal.x * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(normal.y * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(normal.z * 255.0f);
            pixels[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, pixels, TEXTURE_SIZE, TEXTURE_SIZE);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    SDL_Log("Generated normal: %s", path.c_str());
    return true;
}

int main(int argc, char* argv[]) {
    std::string outputDir = "assets/materials";

    if (argc > 1) {
        outputDir = argv[1];
    }

    SDL_Log("Material Texture Generator (Enhanced)");
    SDL_Log("Output directory: %s", outputDir.c_str());

    // Create all directories
    std::vector<std::string> dirs = {
        outputDir + "/terrain/beach",
        outputDir + "/terrain/cliff",
        outputDir + "/terrain/marsh",
        outputDir + "/terrain/river",
        outputDir + "/terrain/wetland",
        outputDir + "/terrain/grassland",
        outputDir + "/terrain/agricultural",
        outputDir + "/terrain/woodland",
        outputDir + "/terrain/sea",
        outputDir + "/roads",
        outputDir + "/rivers"
    };

    for (const auto& dir : dirs) {
        fs::create_directories(dir);
    }

    bool success = true;

    // ========== Beach textures ==========
    SDL_Log("Generating beach textures...");

    success &= generateTexture(outputDir + "/terrain/beach/sand_albedo.png", [](glm::vec2 uv) {
        return generateSand(uv, glm::vec3(0.93f, 0.87f, 0.7f), glm::vec3(0.8f, 0.72f, 0.55f));
    });
    success &= generateNormalMap(outputDir + "/terrain/beach/sand_normal.png", [](glm::vec2 uv) {
        return fbm(uv * 20.0f, 4) * 0.5f + voronoi(uv * 40.0f, 0.8f).f1 * 0.3f;
    }, 0.4f);

    success &= generateTexture(outputDir + "/terrain/beach/wet_sand_albedo.png", [](glm::vec2 uv) {
        return generateSand(uv, glm::vec3(0.65f, 0.58f, 0.45f), glm::vec3(0.45f, 0.4f, 0.32f));
    });

    success &= generateTexture(outputDir + "/terrain/beach/pebbles_albedo.png", [](glm::vec2 uv) {
        return generatePebbles(uv, glm::vec3(0.55f, 0.52f, 0.48f));
    });
    success &= generateNormalMap(outputDir + "/terrain/beach/pebbles_normal.png", [](glm::vec2 uv) {
        VoronoiResult v = voronoi(uv * 15.0f, 0.9f);
        return glm::smoothstep(0.3f, 0.0f, v.f1);
    }, 1.2f);

    success &= generateTexture(outputDir + "/terrain/beach/driftwood_albedo.png", [](glm::vec2 uv) {
        // Wood grain pattern
        glm::vec2 warpedUV = domainWarp(uv, 0.1f, 3.0f);
        float grain = std::sin(warpedUV.y * 30.0f + fbm(uv * 5.0f, 3) * 3.0f) * 0.5f + 0.5f;
        glm::vec3 light(0.6f, 0.52f, 0.4f);
        glm::vec3 dark(0.35f, 0.28f, 0.2f);
        return glm::mix(dark, light, grain);
    });

    success &= generateTexture(outputDir + "/terrain/beach/seaweed_albedo.png", [](glm::vec2 uv) {
        glm::vec3 green(0.2f, 0.35f, 0.15f);
        glm::vec3 brown(0.3f, 0.25f, 0.15f);
        float pattern = fbm(uv * 8.0f, 4) * 0.5f + 0.5f;
        VoronoiResult strands = voronoi(uv * 12.0f, 0.7f);
        float strandMask = glm::smoothstep(0.3f, 0.1f, strands.f1);
        glm::vec3 seaweed = glm::mix(brown, green, pattern);
        glm::vec3 sand(0.7f, 0.65f, 0.5f);
        return glm::mix(sand, seaweed, strandMask);
    });

    // ========== Cliff textures ==========
    SDL_Log("Generating cliff textures...");

    success &= generateTexture(outputDir + "/terrain/cliff/chalk_albedo.png", [](glm::vec2 uv) {
        return generateChalk(uv, glm::vec3(0.95f, 0.94f, 0.91f), glm::vec3(0.82f, 0.8f, 0.77f));
    });
    success &= generateNormalMap(outputDir + "/terrain/cliff/chalk_normal.png", [](glm::vec2 uv) {
        return fbm(uv * 15.0f, 5) * 0.5f + turbulence(uv * 8.0f, 3) * 0.3f;
    }, 0.6f);

    success &= generateTexture(outputDir + "/terrain/cliff/rock_albedo.png", [](glm::vec2 uv) {
        return generateRock(uv, glm::vec3(0.5f, 0.48f, 0.45f), glm::vec3(0.32f, 0.3f, 0.28f),
                           glm::vec3(0.2f, 0.18f, 0.16f));
    });
    success &= generateNormalMap(outputDir + "/terrain/cliff/rock_normal.png", [](glm::vec2 uv) {
        glm::vec2 warpedUV = domainWarp(uv, 0.15f, 4.0f);
        VoronoiResult cracks = voronoi(warpedUV * 6.0f, 0.8f);
        float crackDepth = 1.0f - glm::smoothstep(0.0f, 0.1f, cracks.f2 - cracks.f1);
        return fbm(uv * 12.0f, 4) * 0.4f + crackDepth * 0.6f;
    }, 1.5f);

    success &= generateTexture(outputDir + "/terrain/cliff/exposed_chalk_albedo.png", [](glm::vec2 uv) {
        return generateChalk(uv, glm::vec3(0.97f, 0.96f, 0.94f), glm::vec3(0.88f, 0.86f, 0.83f));
    });

    success &= generateTexture(outputDir + "/terrain/cliff/grass_topped_albedo.png", [](glm::vec2 uv) {
        glm::vec3 grass = generateGrass(uv, glm::vec3(0.35f, 0.5f, 0.2f), glm::vec3(0.2f, 0.35f, 0.12f),
                                        glm::vec3(0.45f, 0.52f, 0.28f));
        glm::vec3 chalk = generateChalk(uv, glm::vec3(0.92f, 0.9f, 0.87f), glm::vec3(0.8f, 0.78f, 0.75f));
        float blend = fbm(uv * 4.0f, 3) * 0.5f + 0.5f;
        blend = glm::smoothstep(0.3f, 0.7f, blend);
        return glm::mix(chalk, grass, blend);
    });

    success &= generateTexture(outputDir + "/terrain/cliff/eroded_chalk_albedo.png", [](glm::vec2 uv) {
        glm::vec3 chalk = generateChalk(uv, glm::vec3(0.9f, 0.88f, 0.85f), glm::vec3(0.75f, 0.72f, 0.68f));
        // Add erosion streaks
        float erosion = ridgedNoise(uv * glm::vec2(2.0f, 8.0f), 4);
        return chalk * (0.85f + erosion * 0.15f);
    });
    success &= generateNormalMap(outputDir + "/terrain/cliff/eroded_chalk_normal.png", [](glm::vec2 uv) {
        return ridgedNoise(uv * glm::vec2(2.0f, 8.0f), 4) + fbm(uv * 10.0f, 4) * 0.3f;
    }, 1.0f);

    success &= generateTexture(outputDir + "/terrain/cliff/flint_albedo.png", [](glm::vec2 uv) {
        return generatePebbles(uv, glm::vec3(0.25f, 0.25f, 0.28f));
    });
    success &= generateNormalMap(outputDir + "/terrain/cliff/flint_normal.png", [](glm::vec2 uv) {
        VoronoiResult v = voronoi(uv * 12.0f, 0.85f);
        return glm::smoothstep(0.35f, 0.0f, v.f1);
    }, 1.0f);

    // ========== Marsh textures ==========
    SDL_Log("Generating marsh textures...");

    success &= generateTexture(outputDir + "/terrain/marsh/muddy_grass_albedo.png", [](glm::vec2 uv) {
        glm::vec3 grass = generateGrass(uv, glm::vec3(0.35f, 0.45f, 0.2f), glm::vec3(0.2f, 0.3f, 0.12f),
                                        glm::vec3(0.4f, 0.45f, 0.25f));
        glm::vec3 mud = generateMud(uv, glm::vec3(0.4f, 0.35f, 0.25f), glm::vec3(0.25f, 0.2f, 0.15f));
        float blend = fbm(uv * 5.0f, 4) * 0.5f + 0.5f;
        return glm::mix(mud, grass, glm::smoothstep(0.3f, 0.6f, blend));
    });
    success &= generateNormalMap(outputDir + "/terrain/marsh/muddy_grass_normal.png", [](glm::vec2 uv) {
        return fbm(uv * 15.0f, 4) * 0.5f + turbulence(uv * 8.0f, 3) * 0.3f;
    }, 0.5f);

    success &= generateTexture(outputDir + "/terrain/marsh/mudflat_albedo.png", [](glm::vec2 uv) {
        return generateMud(uv, glm::vec3(0.45f, 0.38f, 0.28f), glm::vec3(0.3f, 0.25f, 0.18f));
    });

    success &= generateTexture(outputDir + "/terrain/marsh/saltpan_albedo.png", [](glm::vec2 uv) {
        // Salt crust on mud
        glm::vec3 mud = generateMud(uv, glm::vec3(0.5f, 0.45f, 0.38f), glm::vec3(0.35f, 0.3f, 0.25f));
        float salt = turbulence(uv * 20.0f, 3);
        salt = glm::smoothstep(0.3f, 0.7f, salt);
        glm::vec3 saltColor(0.9f, 0.88f, 0.85f);
        return glm::mix(mud, saltColor, salt * 0.6f);
    });

    success &= generateTexture(outputDir + "/terrain/marsh/cordgrass_albedo.png", [](glm::vec2 uv) {
        return generateReeds(uv, glm::vec3(0.4f, 0.48f, 0.25f), glm::vec3(0.45f, 0.38f, 0.25f));
    });

    success &= generateTexture(outputDir + "/terrain/marsh/creek_albedo.png", [](glm::vec2 uv) {
        glm::vec3 water = generateWater(uv, glm::vec3(0.35f, 0.4f, 0.38f), glm::vec3(0.2f, 0.25f, 0.25f));
        glm::vec3 mud = generateMud(uv, glm::vec3(0.4f, 0.35f, 0.28f), glm::vec3(0.28f, 0.24f, 0.18f));
        float edge = fbm(uv * 6.0f, 3) * 0.5f + 0.5f;
        return glm::mix(water, mud, glm::smoothstep(0.4f, 0.6f, edge));
    });

    // ========== River textures ==========
    SDL_Log("Generating river textures...");

    success &= generateTexture(outputDir + "/terrain/river/gravel_albedo.png", [](glm::vec2 uv) {
        return generatePebbles(uv, glm::vec3(0.5f, 0.48f, 0.45f));
    });
    success &= generateNormalMap(outputDir + "/terrain/river/gravel_normal.png", [](glm::vec2 uv) {
        VoronoiResult v = voronoi(uv * 18.0f, 0.85f);
        return glm::smoothstep(0.25f, 0.0f, v.f1);
    }, 1.0f);

    success &= generateTexture(outputDir + "/terrain/river/stones_albedo.png", [](glm::vec2 uv) {
        return generatePebbles(uv, glm::vec3(0.45f, 0.43f, 0.4f));
    });
    success &= generateNormalMap(outputDir + "/terrain/river/stones_normal.png", [](glm::vec2 uv) {
        VoronoiResult v = voronoi(uv * 10.0f, 0.9f);
        return glm::smoothstep(0.4f, 0.0f, v.f1);
    }, 1.2f);

    success &= generateTexture(outputDir + "/terrain/river/sand_albedo.png", [](glm::vec2 uv) {
        return generateSand(uv, glm::vec3(0.7f, 0.65f, 0.55f), glm::vec3(0.55f, 0.5f, 0.42f));
    });

    success &= generateTexture(outputDir + "/terrain/river/mud_albedo.png", [](glm::vec2 uv) {
        return generateMud(uv, glm::vec3(0.4f, 0.35f, 0.28f), glm::vec3(0.28f, 0.24f, 0.18f));
    });

    // ========== Wetland textures ==========
    SDL_Log("Generating wetland textures...");

    success &= generateTexture(outputDir + "/terrain/wetland/wet_grass_albedo.png", [](glm::vec2 uv) {
        return generateGrass(uv, glm::vec3(0.28f, 0.42f, 0.18f), glm::vec3(0.15f, 0.28f, 0.1f),
                            glm::vec3(0.35f, 0.45f, 0.22f));
    });
    success &= generateNormalMap(outputDir + "/terrain/wetland/wet_grass_normal.png", [](glm::vec2 uv) {
        return fbm(uv * 12.0f, 4) * 0.6f;
    }, 0.5f);

    success &= generateTexture(outputDir + "/terrain/wetland/marsh_grass_albedo.png", [](glm::vec2 uv) {
        return generateGrass(uv, glm::vec3(0.38f, 0.48f, 0.22f), glm::vec3(0.22f, 0.32f, 0.14f),
                            glm::vec3(0.5f, 0.52f, 0.3f));
    });

    success &= generateTexture(outputDir + "/terrain/wetland/reeds_albedo.png", [](glm::vec2 uv) {
        return generateReeds(uv, glm::vec3(0.45f, 0.52f, 0.3f), glm::vec3(0.5f, 0.42f, 0.28f));
    });

    success &= generateTexture(outputDir + "/terrain/wetland/muddy_albedo.png", [](glm::vec2 uv) {
        return generateMud(uv, glm::vec3(0.38f, 0.32f, 0.24f), glm::vec3(0.25f, 0.2f, 0.15f));
    });

    success &= generateTexture(outputDir + "/terrain/wetland/flooded_albedo.png", [](glm::vec2 uv) {
        return generateWater(uv, glm::vec3(0.3f, 0.38f, 0.35f), glm::vec3(0.18f, 0.25f, 0.22f));
    });

    // ========== Grassland textures (chalk downs) ==========
    SDL_Log("Generating grassland textures...");

    success &= generateTexture(outputDir + "/terrain/grassland/chalk_grass_albedo.png", [](glm::vec2 uv) {
        return generateGrass(uv, glm::vec3(0.4f, 0.55f, 0.25f), glm::vec3(0.25f, 0.4f, 0.15f),
                            glm::vec3(0.55f, 0.58f, 0.35f));
    });
    success &= generateNormalMap(outputDir + "/terrain/grassland/chalk_grass_normal.png", [](glm::vec2 uv) {
        return fbm(uv * 15.0f, 4) * 0.5f;
    }, 0.4f);

    success &= generateTexture(outputDir + "/terrain/grassland/open_down_albedo.png", [](glm::vec2 uv) {
        return generateGrass(uv, glm::vec3(0.45f, 0.55f, 0.28f), glm::vec3(0.3f, 0.42f, 0.18f),
                            glm::vec3(0.58f, 0.6f, 0.38f), 30.0f);
    });

    success &= generateTexture(outputDir + "/terrain/grassland/wildflower_albedo.png", [](glm::vec2 uv) {
        return generateWildflowers(uv, glm::vec3(0.38f, 0.52f, 0.22f), glm::vec3(0.22f, 0.35f, 0.12f));
    });

    success &= generateTexture(outputDir + "/terrain/grassland/gorse_albedo.png", [](glm::vec2 uv) {
        return generateGorse(uv, glm::vec3(0.28f, 0.38f, 0.18f));
    });

    success &= generateTexture(outputDir + "/terrain/grassland/chalk_scrape_albedo.png", [](glm::vec2 uv) {
        glm::vec3 chalk = generateChalk(uv, glm::vec3(0.9f, 0.88f, 0.85f), glm::vec3(0.78f, 0.75f, 0.72f));
        glm::vec3 grass = generateGrass(uv, glm::vec3(0.35f, 0.48f, 0.2f), glm::vec3(0.2f, 0.32f, 0.12f),
                                        glm::vec3(0.45f, 0.5f, 0.28f));
        float blend = fbm(uv * 3.0f, 3) * 0.5f + 0.5f;
        blend = glm::smoothstep(0.35f, 0.65f, blend);
        return glm::mix(chalk, grass, blend * 0.7f);
    });

    // ========== Agricultural textures ==========
    SDL_Log("Generating agricultural textures...");

    success &= generateTexture(outputDir + "/terrain/agricultural/ploughed_albedo.png", [](glm::vec2 uv) {
        // Ploughed field with furrows
        glm::vec3 lightBrown(0.48f, 0.4f, 0.3f);
        glm::vec3 darkBrown(0.28f, 0.22f, 0.15f);

        // Furrow pattern
        float furrows = std::sin(uv.y * 50.0f + fbm(uv * 2.0f, 2) * 1.5f);
        furrows = glm::smoothstep(-0.3f, 0.3f, furrows);

        // Soil variation
        float soilVar = fbm(uv * 8.0f, 4) * 0.3f;

        glm::vec3 color = glm::mix(darkBrown, lightBrown, furrows * 0.6f + 0.2f);
        return color * (0.9f + soilVar);
    });
    success &= generateNormalMap(outputDir + "/terrain/agricultural/ploughed_normal.png", [](glm::vec2 uv) {
        float furrows = std::sin(uv.y * 50.0f + fbm(uv * 2.0f, 2) * 1.5f) * 0.5f + 0.5f;
        return furrows + fbm(uv * 15.0f, 3) * 0.2f;
    }, 0.8f);

    success &= generateTexture(outputDir + "/terrain/agricultural/pasture_albedo.png", [](glm::vec2 uv) {
        glm::vec3 grass = generateGrass(uv, glm::vec3(0.42f, 0.55f, 0.25f), glm::vec3(0.28f, 0.42f, 0.18f),
                                        glm::vec3(0.55f, 0.58f, 0.32f), 25.0f);
        // Some bare patches
        float bare = fbm(uv * 4.0f, 3) * 0.5f + 0.5f;
        bare = glm::smoothstep(0.7f, 0.85f, bare);
        glm::vec3 dirt(0.45f, 0.4f, 0.32f);
        return glm::mix(grass, dirt, bare * 0.5f);
    });

    success &= generateTexture(outputDir + "/terrain/agricultural/crop_albedo.png", [](glm::vec2 uv) {
        // Crop rows
        glm::vec3 crop(0.4f, 0.52f, 0.22f);
        glm::vec3 soil(0.42f, 0.35f, 0.25f);

        float rows = std::sin(uv.x * 40.0f) * 0.5f + 0.5f;
        rows = glm::smoothstep(0.3f, 0.7f, rows);

        float growth = fbm(uv * 10.0f, 3) * 0.2f + 0.8f;
        crop *= growth;

        return glm::mix(soil, crop, rows);
    });

    success &= generateTexture(outputDir + "/terrain/agricultural/fallow_albedo.png", [](glm::vec2 uv) {
        glm::vec3 dirt = generateMud(uv, glm::vec3(0.48f, 0.42f, 0.32f), glm::vec3(0.35f, 0.3f, 0.22f));
        // Sparse weeds
        VoronoiResult weeds = voronoi(uv * 20.0f, 0.9f);
        float weedMask = glm::smoothstep(0.2f, 0.1f, weeds.f1);
        weedMask *= glm::step(0.8f, hash(weeds.id));
        glm::vec3 weedColor(0.35f, 0.45f, 0.2f);
        return glm::mix(dirt, weedColor, weedMask * 0.7f);
    });

    // ========== Woodland textures ==========
    SDL_Log("Generating woodland textures...");

    success &= generateTexture(outputDir + "/terrain/woodland/forest_floor_albedo.png", [](glm::vec2 uv) {
        return generateForestFloor(uv, glm::vec3(0.32f, 0.25f, 0.18f),
                                   glm::vec3(0.45f, 0.38f, 0.2f),
                                   glm::vec3(0.25f, 0.35f, 0.15f));
    });
    success &= generateNormalMap(outputDir + "/terrain/woodland/forest_floor_normal.png", [](glm::vec2 uv) {
        VoronoiResult leaves = voronoi(uv * 15.0f, 0.85f);
        float leafHeight = glm::smoothstep(0.3f, 0.1f, leaves.f1) * 0.6f;
        return leafHeight + fbm(uv * 20.0f, 3) * 0.3f;
    }, 0.6f);

    success &= generateTexture(outputDir + "/terrain/woodland/beech_floor_albedo.png", [](glm::vec2 uv) {
        return generateForestFloor(uv, glm::vec3(0.38f, 0.3f, 0.2f),
                                   glm::vec3(0.55f, 0.42f, 0.22f),
                                   glm::vec3(0.28f, 0.38f, 0.18f));
    });

    success &= generateTexture(outputDir + "/terrain/woodland/oak_fern_albedo.png", [](glm::vec2 uv) {
        glm::vec3 floor = generateForestFloor(uv, glm::vec3(0.3f, 0.24f, 0.16f),
                                              glm::vec3(0.42f, 0.35f, 0.18f),
                                              glm::vec3(0.22f, 0.32f, 0.14f));
        // Fern fronds
        VoronoiResult ferns = voronoi(uv * 8.0f, 0.8f);
        float fernMask = glm::smoothstep(0.35f, 0.15f, ferns.f1);
        glm::vec3 fernColor(0.2f, 0.4f, 0.15f);
        return glm::mix(floor, fernColor, fernMask * 0.6f);
    });

    success &= generateTexture(outputDir + "/terrain/woodland/clearing_albedo.png", [](glm::vec2 uv) {
        return generateGrass(uv, glm::vec3(0.38f, 0.5f, 0.22f), glm::vec3(0.22f, 0.35f, 0.12f),
                            glm::vec3(0.48f, 0.52f, 0.28f), 35.0f);
    });

    success &= generateTexture(outputDir + "/terrain/woodland/coppice_albedo.png", [](glm::vec2 uv) {
        return generateForestFloor(uv, glm::vec3(0.35f, 0.28f, 0.2f),
                                   glm::vec3(0.48f, 0.4f, 0.25f),
                                   glm::vec3(0.3f, 0.4f, 0.2f));
    });

    // ========== Sea texture ==========
    SDL_Log("Generating sea texture...");
    success &= generateTexture(outputDir + "/terrain/sea/albedo.png", [](glm::vec2 uv) {
        return generateWater(uv, glm::vec3(0.25f, 0.45f, 0.5f), glm::vec3(0.1f, 0.25f, 0.35f));
    });

    // ========== Road textures ==========
    SDL_Log("Generating road textures...");

    success &= generateTexture(outputDir + "/roads/footpath_albedo.png", [](glm::vec2 uv) {
        return generatePath(uv, glm::vec3(0.52f, 0.45f, 0.35f), false);
    });

    success &= generateTexture(outputDir + "/roads/bridleway_albedo.png", [](glm::vec2 uv) {
        return generatePath(uv, glm::vec3(0.48f, 0.42f, 0.35f), false);
    });
    success &= generateNormalMap(outputDir + "/roads/bridleway_normal.png", [](glm::vec2 uv) {
        VoronoiResult stones = voronoi(uv * 20.0f, 0.8f);
        return glm::smoothstep(0.2f, 0.0f, stones.f1) * 0.5f + fbm(uv * 10.0f, 3) * 0.3f;
    }, 0.7f);

    success &= generateTexture(outputDir + "/roads/lane_albedo.png", [](glm::vec2 uv) {
        return generatePath(uv, glm::vec3(0.45f, 0.4f, 0.35f), false);
    });
    success &= generateNormalMap(outputDir + "/roads/lane_normal.png", [](glm::vec2 uv) {
        return fbm(uv * 12.0f, 4) * 0.4f;
    }, 0.5f);

    success &= generateTexture(outputDir + "/roads/road_albedo.png", [](glm::vec2 uv) {
        return generatePath(uv, glm::vec3(0.25f, 0.25f, 0.28f), true);
    });
    success &= generateNormalMap(outputDir + "/roads/road_normal.png", [](glm::vec2 uv) {
        return hash(glm::floor(uv * 80.0f)) * 0.15f + fbm(uv * 5.0f, 3) * 0.1f;
    }, 0.3f);

    success &= generateTexture(outputDir + "/roads/main_road_albedo.png", [](glm::vec2 uv) {
        return generatePath(uv, glm::vec3(0.22f, 0.22f, 0.25f), true);
    });
    success &= generateNormalMap(outputDir + "/roads/main_road_normal.png", [](glm::vec2 uv) {
        return hash(glm::floor(uv * 100.0f)) * 0.1f;
    }, 0.25f);

    // ========== Riverbed textures ==========
    SDL_Log("Generating riverbed textures...");

    success &= generateTexture(outputDir + "/rivers/gravel_albedo.png", [](glm::vec2 uv) {
        return generatePebbles(uv, glm::vec3(0.48f, 0.45f, 0.42f));
    });

    success &= generateTexture(outputDir + "/rivers/mud_albedo.png", [](glm::vec2 uv) {
        return generateMud(uv, glm::vec3(0.38f, 0.32f, 0.25f), glm::vec3(0.25f, 0.2f, 0.15f));
    });

    if (success) {
        SDL_Log("All textures generated successfully!");
        return 0;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Some textures failed to generate");
        return 1;
    }
}
