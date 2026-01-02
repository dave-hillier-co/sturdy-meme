#pragma once

#include <cmath>

namespace towngenerator {
namespace geom {

struct Point {
    float x;
    float y;

    // Constructor
    Point(float x = 0.0f, float y = 0.0f) : x(x), y(y) {}

    // Subtract - returns new Point
    Point subtract(const Point& other) const {
        return Point(x - other.x, y - other.y);
    }

    // Add - returns new Point
    Point add(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }

    // Static distance
    static float distance(const Point& p1, const Point& p2) {
        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Length property - returns magnitude
    float length() const {
        return std::sqrt(x * x + y * y);
    }

    // Normalize to given length
    Point normalize(float length = 1.0f) const {
        float len = this->length();
        if (len == 0.0f) {
            return Point(0.0f, 0.0f);
        }
        float scale = length / len;
        return Point(x * scale, y * scale);
    }

    // Scale - returns scaled copy
    Point scale(float factor) const {
        return Point(x * factor, y * factor);
    }

    // Rotate90 - returns perpendicular point (90 degrees counter-clockwise)
    Point rotate90() const {
        return Point(-y, x);
    }
};

} // namespace geom
} // namespace towngenerator
