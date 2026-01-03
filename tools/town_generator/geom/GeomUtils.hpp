/**
 * Ported from: Source/com/watabou/geom/GeomUtils.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

#include "../geom/Point.hpp"
#include <cmath>
#include <optional>

namespace town {

class GeomUtils {
public:
    // Returns intersection parameters t1, t2 for two lines.
    // Line 1: (x1, y1) + t1 * (dx1, dy1)
    // Line 2: (x2, y2) + t2 * (dx2, dy2)
    // Returns nullopt if lines are parallel.
    static std::optional<Point> intersectLines(
        float x1, float y1, float dx1, float dy1,
        float x2, float y2, float dx2, float dy2
    ) {
        float d = dx1 * dy2 - dy1 * dx2;
        if (d == 0) {
            return std::nullopt;
        }

        float t2 = (dy1 * (x2 - x1) - dx1 * (y2 - y1)) / d;
        float t1 = dx1 != 0 ?
            (x2 - x1 + dx2 * t2) / dx1 :
            (y2 - y1 + dy2 * t2) / dy1;

        return Point(t1, t2);
    }

    static Point interpolate(const Point& p1, const Point& p2, float ratio = 0.5f) {
        Point d = p2.subtract(p1);
        return Point(p1.x + d.x * ratio, p1.y + d.y * ratio);
    }

    static float scalar(float x1, float y1, float x2, float y2) {
        return x1 * x2 + y1 * y2;
    }

    static float cross(float x1, float y1, float x2, float y2) {
        return x1 * y2 - y1 * x2;
    }

    static float distance2line(float x1, float y1, float dx1, float dy1, float x0, float y0) {
        return (dx1 * y0 - dy1 * x0 + (y1 + dy1) * x1 - (x1 + dx1) * y1) / std::sqrt(dx1 * dx1 + dy1 * dy1);
    }
};

} // namespace town
