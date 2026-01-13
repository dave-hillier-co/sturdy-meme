#pragma once

#include <cstdint>
#include <glm/glm.hpp>

/**
 * DeterministicRandom - Hash-based pseudo-random number generation
 *
 * Provides deterministic random values based on position and seed.
 * Same inputs always produce the same outputs, making placement reproducible.
 */
class DeterministicRandom {
public:
    /**
     * Generate a pseudo-random float in [0, 1] from position and seed
     */
    static float hashPosition(float x, float z, uint32_t seed) {
        uint32_t ix = *reinterpret_cast<uint32_t*>(&x);
        uint32_t iz = *reinterpret_cast<uint32_t*>(&z);
        uint32_t n = ix ^ (iz * 1597334673U) ^ seed;
        n = (n << 13U) ^ n;
        n = n * (n * n * 15731U + 789221U) + 1376312589U;
        return float(n & 0x7fffffffU) / float(0x7fffffff);
    }

    /**
     * Generate a pseudo-random float in [minVal, maxVal]
     */
    static float hashRange(float x, float z, uint32_t seed, float minVal, float maxVal) {
        float t = hashPosition(x, z, seed);
        return minVal + t * (maxVal - minVal);
    }

    /**
     * Generate a pseudo-random integer in [0, maxVal)
     */
    static uint32_t hashInt(float x, float z, uint32_t seed, uint32_t maxVal) {
        if (maxVal == 0) return 0;
        return static_cast<uint32_t>(hashPosition(x, z, seed) * maxVal) % maxVal;
    }

    /**
     * Generate a pseudo-random 2D direction (unit vector)
     */
    static glm::vec2 hashDirection(float x, float z, uint32_t seed) {
        float angle = hashPosition(x, z, seed) * 2.0f * 3.14159265f;
        return glm::vec2(std::cos(angle), std::sin(angle));
    }

    /**
     * Generate a pseudo-random point within a radius using polar coordinates
     * Returns a point with uniform distribution over the disk
     */
    static glm::vec2 hashDiskPoint(float x, float z, uint32_t seed, float radius) {
        float angle = hashPosition(x, z, seed) * 2.0f * 3.14159265f;
        // Use sqrt for uniform disk distribution
        float r = std::sqrt(hashPosition(x, z, seed + 1000)) * radius;
        return glm::vec2(r * std::cos(angle), r * std::sin(angle));
    }
};
