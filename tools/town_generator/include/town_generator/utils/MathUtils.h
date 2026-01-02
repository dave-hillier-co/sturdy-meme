#pragma once

#include <cmath>
#include <algorithm>

namespace town_generator {

class MathUtils {
public:
    static float gate(float value, float min, float max) {
        return value < min ? min : (value < max ? value : max);
    }

    static int gatei(int value, int min, int max) {
        return value < min ? min : (value < max ? value : max);
    }

    static int sign(float value) {
        return value == 0 ? 0 : (value < 0 ? -1 : 1);
    }

    static float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }
};

} // namespace town_generator
