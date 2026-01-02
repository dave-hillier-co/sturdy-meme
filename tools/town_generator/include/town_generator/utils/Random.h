#pragma once

#include <cstdint>
#include <chrono>

namespace town_generator {

class Random {
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

    static float getFloat() {
        return static_cast<float>(next()) / n;
    }

    static float normal() {
        return (getFloat() + getFloat() + getFloat()) / 3.0f;
    }

    static int getInt(int min, int max) {
        return static_cast<int>(min + static_cast<float>(next()) / n * (max - min));
    }

    static bool getBool(float chance = 0.5f) {
        return getFloat() < chance;
    }

    static float fuzzy(float f = 1.0f) {
        if (f == 0) {
            return 0.5f;
        }
        return (1.0f - f) / 2.0f + f * normal();
    }

private:
    static constexpr float g = 48271.0f;
    static constexpr int64_t n = 2147483647;

    static int next() {
        seed = static_cast<int>((static_cast<int64_t>(seed) * static_cast<int64_t>(g)) % n);
        return seed;
    }

    static inline int seed = 1;
};

} // namespace town_generator
