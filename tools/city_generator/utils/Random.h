#pragma once

#include <cmath>
#include <cstdint>
#include <ctime>
#include <optional>

namespace towngenerator {
namespace utils {

/**
 * Random number generator using Linear Congruential Generator (LCG)
 * Ported from Haxe Random class
 *
 * Uses the following LCG parameters:
 * - Multiplier: 48271.0
 * - Modulus: 2147483647 (2^31 - 1)
 */
class Random {
private:
    inline static double currentSeed = 1.0;
    static constexpr double MULTIPLIER = 48271.0;
    static constexpr double MODULUS = 2147483647.0;

public:
    /**
     * Initialize seed, uses system time if no seed provided
     * @param seed Optional seed value, uses current time if not provided
     */
    static void reset(std::optional<int64_t> seed = std::nullopt) {
        if (seed.has_value()) {
            currentSeed = static_cast<double>(seed.value());
        } else {
            currentSeed = static_cast<double>(std::time(nullptr));
        }
    }

    /**
     * Returns current seed value
     * @return Current seed
     */
    static double getSeed() {
        return currentSeed;
    }

    /**
     * Returns random float between 0 and 1
     * Implements LCG: seed = (seed * 48271.0) % 2147483647
     * @return Random float in range [0, 1)
     */
    static double randomFloat() {
        currentSeed = std::fmod(currentSeed * MULTIPLIER, MODULUS);
        return currentSeed / MODULUS;
    }

    /**
     * Returns average of 3 randomFloat() calls
     * Produces a distribution weighted toward the center
     * @return Random float with normal-like distribution
     */
    static double normal() {
        return (randomFloat() + randomFloat() + randomFloat()) / 3.0;
    }

    /**
     * Returns random integer in range [min, max)
     * @param min Minimum value (inclusive)
     * @param max Maximum value (exclusive)
     * @return Random integer in range [min, max)
     */
    static int randomInt(int min, int max) {
        return min + static_cast<int>(randomFloat() * (max - min));
    }

    /**
     * Returns true with given probability
     * @param probability Probability of returning true (default 0.5)
     * @return true with given probability, false otherwise
     */
    static bool randomBool(double probability = 0.5) {
        return randomFloat() < probability;
    }

    /**
     * Applies weighted distribution around target
     * Multiplies target by normal distribution
     * @param target Target value to distribute around
     * @return Value with weighted distribution around target
     */
    static double fuzzy(double target) {
        return target * normal();
    }
};

} // namespace utils
} // namespace towngenerator
