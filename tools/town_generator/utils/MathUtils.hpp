/**
 * Ported from: Source/com/watabou/utils/MathUtils.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

namespace town {

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
};

} // namespace town
