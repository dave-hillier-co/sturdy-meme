// Common wind Perlin noise functions
// Include this in shaders that need wind-based animation with Perlin noise
//
// The permutation table is stored as ivec4[128] for efficient std140 packing
// (512 ints packed into 128 vec4s = 2KB instead of 8KB for unpacked int[512])
//
// Usage: Define WindUniforms in your shader, then call sampleWindNoise()
//
// layout(binding = X) uniform WindUniforms {
//     vec4 windDirectionAndStrength;  // xy = direction, z = strength, w = speed
//     vec4 windParams;                // x = gustFreq, y = gustAmp, z = noiseScale, w = time
//     ivec4 permPacked[128];          // Permutation table
// } wind;
//
// float windSample = sampleWindNoise(wind.windDirectionAndStrength, wind.windParams, wind.permPacked, worldPos);

#ifndef WIND_COMMON_GLSL
#define WIND_COMMON_GLSL

// Helper to access permutation table (unpacks ivec4 storage)
int getWindPerm(ivec4 permPacked[128], int index) {
    int vecIndex = index / 4;
    int component = index % 4;
    return permPacked[vecIndex][component];
}

// Perlin noise fade function (6t^5 - 15t^4 + 10t^3)
float windFade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Perlin noise gradient function
float windGrad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

// 2D Perlin noise using UBO permutation table
float windPerlinNoise(ivec4 permPacked[128], float x, float y) {
    int X = int(floor(x)) & 255;
    int Y = int(floor(y)) & 255;

    x -= floor(x);
    y -= floor(y);

    float u = windFade(x);
    float v = windFade(y);

    int A = getWindPerm(permPacked, X) + Y;
    int AA = getWindPerm(permPacked, A);
    int AB = getWindPerm(permPacked, A + 1);
    int B = getWindPerm(permPacked, X + 1) + Y;
    int BA = getWindPerm(permPacked, B);
    int BB = getWindPerm(permPacked, B + 1);

    float res = mix(
        mix(windGrad(getWindPerm(permPacked, AA), x, y),
            windGrad(getWindPerm(permPacked, BA), x - 1.0, y), u),
        mix(windGrad(getWindPerm(permPacked, AB), x, y - 1.0),
            windGrad(getWindPerm(permPacked, BB), x - 1.0, y - 1.0), u),
        v
    );

    return (res + 1.0) * 0.5;
}

// Sample wind using scrolling Perlin noise
// Multi-octave: 0.7*noise(x/10) + 0.2*noise(x/5) + 0.1*noise(x/2.5)
float sampleWindNoise(vec4 windDirectionAndStrength, vec4 windParams, ivec4 permPacked[128], vec2 worldPos) {
    vec2 windDir = windDirectionAndStrength.xy;
    float windStrength = windDirectionAndStrength.z;
    float windSpeed = windDirectionAndStrength.w;
    float windTime = windParams.w;
    float gustFreq = windParams.x;
    float gustAmp = windParams.y;

    // Scroll position in wind direction
    vec2 scrolledPos = worldPos - windDir * windTime * windSpeed * 0.4;

    // Three octaves: ~10m, ~5m, ~2.5m wavelengths
    float baseFreq = 0.1;
    float n1 = windPerlinNoise(permPacked, scrolledPos.x * baseFreq, scrolledPos.y * baseFreq);
    float n2 = windPerlinNoise(permPacked, scrolledPos.x * baseFreq * 2.0, scrolledPos.y * baseFreq * 2.0);
    float n3 = windPerlinNoise(permPacked, scrolledPos.x * baseFreq * 4.0, scrolledPos.y * baseFreq * 4.0);

    // Weighted sum dominated by first octave
    float noise = n1 * 0.7 + n2 * 0.2 + n3 * 0.1;

    // Add time-varying gust
    float gust = (sin(windTime * gustFreq * 6.28318) * 0.5 + 0.5) * gustAmp;

    return (noise + gust) * windStrength;
}

#endif // WIND_COMMON_GLSL
