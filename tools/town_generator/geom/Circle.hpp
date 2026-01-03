/**
 * Ported from: Source/com/watabou/geom/Circle.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

namespace town {

class Circle {
public:
    float x;
    float y;
    float r;

    Circle(float x = 0.0f, float y = 0.0f, float r = 0.0f)
        : x(x), y(y), r(r) {}
};

} // namespace town
