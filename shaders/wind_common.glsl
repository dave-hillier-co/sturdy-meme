// Common wind UBO and Perlin noise definitions
// Include this in shaders that need wind-based animation with Perlin noise
//
// The permutation table is stored as ivec4[128] for efficient std140 packing
// (512 ints packed into 128 vec4s = 2KB instead of 8KB for unpacked int[512])

#ifndef WIND_COMMON_GLSL
#define WIND_COMMON_GLSL

// WindUniforms UBO definition
// Note: This struct must match the C++ WindUniforms struct exactly
// The permutation table is packed as ivec4[128] for std140 alignment efficiency
struct WindUniformsData {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
    ivec4 permPacked[128];          // Permutation table: 512 ints packed as 128 ivec4s
};

// Helper to access permutation table (unpacks ivec4 storage)
int getPerm(WindUniformsData wind, int index) {
    int vecIndex = index / 4;
    int component = index % 4;
    return wind.permPacked[vecIndex][component];
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
float windPerlinNoise(WindUniformsData wind, float x, float y) {
    int X = int(floor(x)) & 255;
    int Y = int(floor(y)) & 255;

    x -= floor(x);
    y -= floor(y);

    float u = windFade(x);
    float v = windFade(y);

    int A = getPerm(wind, X) + Y;
    int AA = getPerm(wind, A);
    int AB = getPerm(wind, A + 1);
    int B = getPerm(wind, X + 1) + Y;
    int BA = getPerm(wind, B);
    int BB = getPerm(wind, B + 1);

    float res = mix(
        mix(windGrad(getPerm(wind, AA), x, y),
            windGrad(getPerm(wind, BA), x - 1.0, y), u),
        mix(windGrad(getPerm(wind, AB), x, y - 1.0),
            windGrad(getPerm(wind, BB), x - 1.0, y - 1.0), u),
        v
    );

    return (res + 1.0) * 0.5;
}

// Sample wind using scrolling Perlin noise
// Multi-octave: 0.7*noise(x/10) + 0.2*noise(x/5) + 0.1*noise(x/2.5)
float sampleWindNoise(WindUniformsData wind, vec2 worldPos) {
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windSpeed = wind.windDirectionAndStrength.w;
    float windTime = wind.windParams.w;
    float gustFreq = wind.windParams.x;
    float gustAmp = wind.windParams.y;

    // Scroll position in wind direction
    vec2 scrolledPos = worldPos - windDir * windTime * windSpeed * 0.4;

    // Three octaves: ~10m, ~5m, ~2.5m wavelengths
    float baseFreq = 0.1;
    float n1 = windPerlinNoise(wind, scrolledPos.x * baseFreq, scrolledPos.y * baseFreq);
    float n2 = windPerlinNoise(wind, scrolledPos.x * baseFreq * 2.0, scrolledPos.y * baseFreq * 2.0);
    float n3 = windPerlinNoise(wind, scrolledPos.x * baseFreq * 4.0, scrolledPos.y * baseFreq * 4.0);

    // Weighted sum dominated by first octave
    float noise = n1 * 0.7 + n2 * 0.2 + n3 * 0.1;

    // Add time-varying gust
    float gust = (sin(windTime * gustFreq * 6.28318) * 0.5 + 0.5) * gustAmp;

    return (noise + gust) * windStrength;
}

#endif // WIND_COMMON_GLSL
