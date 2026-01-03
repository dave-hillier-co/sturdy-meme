#pragma once

#include "Point.h"  // For EPSILON
#include <cmath>

namespace towngenerator {
namespace geom {

struct Circle {
    float x;
    float y;
    float r;

    Circle(float x = 0.0f, float y = 0.0f, float r = 0.0f) : x(x), y(y), r(r) {}

    // Exact equality operator (for container compatibility)
    bool operator==(const Circle& other) const {
        return x == other.x && y == other.y && r == other.r;
    }

    bool operator!=(const Circle& other) const {
        return !(*this == other);
    }

    // Less-than for container compatibility
    bool operator<(const Circle& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return r < other.r;
    }

    // Epsilon-based geometric equality check
    bool equals(const Circle& other, float epsilon = EPSILON) const {
        return std::abs(x - other.x) < epsilon &&
               std::abs(y - other.y) < epsilon &&
               std::abs(r - other.r) < epsilon;
    }
};

} // namespace geom
} // namespace towngenerator
