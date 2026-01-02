#pragma once

#include "Point.h"
#include <optional>
#include <cmath>

namespace towngenerator {
namespace geom {

class GeomUtils {
public:
    // Finds intersection of two lines defined by position and direction vectors
    // Returns a Point with parameters t1 and t2 in x,y, or nullopt if parallel
    static std::optional<Point> intersectLines(const Point& p1, const Point& d1,
                                               const Point& p2, const Point& d2) {
        // Lines: L1 = p1 + t1*d1, L2 = p2 + t2*d2
        // At intersection: p1 + t1*d1 = p2 + t2*d2
        // Solving: t1*d1 - t2*d2 = p2 - p1

        float denominator = cross(d1.x, d1.y, d2.x, d2.y);

        // Check if lines are parallel
        if (std::abs(denominator) < 1e-10f) {
            return std::nullopt;
        }

        Point diff = p2.subtract(p1);
        float t1 = cross(diff.x, diff.y, d2.x, d2.y) / denominator;
        float t2 = cross(diff.x, diff.y, d1.x, d1.y) / denominator;

        return Point(t1, t2);
    }

    // Computes point between two points at given ratio
    static Point interpolate(const Point& p1, const Point& p2, float ratio = 0.5f) {
        return Point(
            p1.x + ratio * (p2.x - p1.x),
            p1.y + ratio * (p2.y - p1.y)
        );
    }

    // Dot product of two 2D vectors
    static float scalar(float dx1, float dy1, float dx2, float dy2) {
        return dx1 * dx2 + dy1 * dy2;
    }

    // Cross product of two 2D vectors (returns scalar z component)
    static float cross(float dx1, float dy1, float dx2, float dy2) {
        return dx1 * dy2 - dy1 * dx2;
    }

    // Perpendicular distance from point (x,y) to line defined by position (px,py) and direction (dx,dy)
    static float distance2line(float px, float py, float dx, float dy, float x, float y) {
        // Vector from line position to point
        float vx = x - px;
        float vy = y - py;

        // Distance = |cross product| / |direction|
        float crossProd = std::abs(cross(vx, vy, dx, dy));
        float dirLength = std::sqrt(dx * dx + dy * dy);

        if (dirLength < 1e-10f) {
            // If direction is zero, return distance to the point
            return std::sqrt(vx * vx + vy * vy);
        }

        return crossProd / dirLength;
    }
};

} // namespace geom
} // namespace towngenerator
