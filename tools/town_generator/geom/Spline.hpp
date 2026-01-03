/**
 * Ported from: Source/com/watabou/geom/Spline.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

#include "../geom/Point.hpp"
#include <vector>

namespace town {

class Spline {
public:
    static inline float curvature = 0.1f;

    static std::vector<Point> startCurve(const Point& p0, const Point& p1, const Point& p2) {
        Point tangent = p2.subtract(p0);
        Point control = p1.subtract(tangent.scale(curvature));
        return {control, p1};
    }

    static std::vector<Point> endCurve(const Point& p0, const Point& p1, const Point& p2) {
        Point tangent = p2.subtract(p0);
        Point control = p1.add(tangent.scale(curvature));
        return {control, p2};
    }

    static std::vector<Point> midCurve(const Point& p0, const Point& p1, const Point& p2, const Point& p3) {
        Point dir = p2.subtract(p1);
        Point tangent1 = p2.subtract(p0);
        Point tangent2 = p3.subtract(p1);

        Point p1a = p1.add(tangent1.scale(curvature));
        Point p2a = p2.subtract(tangent2.scale(curvature));
        Point p12 = p1a.add(p2a).scale(0.5f);

        return {p1a, p12, p2a, p2};
    }
};

} // namespace town
