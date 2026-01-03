/**
 * Ported from: Source/com/watabou/utils/Random.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

#include <cstdint>
#include <chrono>

namespace town {

class Random {
private:
    static constexpr double g = 48271.0;
    static constexpr int64_t n = 2147483647;

    static inline int seed = 1;

    static int next() {
        seed = static_cast<int>((static_cast<int64_t>(seed) * static_cast<int64_t>(g)) % n);
        return seed;
    }

public:
    static void reset(int newSeed = -1) {
        if (newSeed != -1) {
            seed = newSeed;
        } else {
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            seed = static_cast<int>(millis % n);
        }
    }

    static int getSeed() {
        return seed;
    }

    static float Float() {
        return static_cast<float>(next()) / static_cast<float>(n);
    }

    // Alias for consistency with calling conventions
    static float getFloat() { return Float(); }

    static float normal() {
        return (Float() + Float() + Float()) / 3.0f;
    }

    static int Int(int min, int max) {
        return static_cast<int>(min + static_cast<float>(next()) / static_cast<float>(n) * (max - min));
    }

    // Alias for consistency
    static int getInt(int min, int max) { return Int(min, max); }

    static bool Bool(float chance = 0.5f) {
        return Float() < chance;
    }

    // Alias for consistency
    static bool getBool(float chance = 0.5f) { return Bool(chance); }

    static float fuzzy(float f = 1.0f) {
        if (f == 0.0f) {
            return 0.5f;
        } else {
            return (1.0f - f) / 2.0f + f * normal();
        }
    }
};

} // namespace town
