#pragma once

#include <cmath>

namespace towngenerator {
namespace geom {

// Global epsilon for geometric comparisons
constexpr float EPSILON = 1e-6f;

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

    // Exact equality operator (for container compatibility)
    // Use equals() for geometric comparison with epsilon
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point& other) const {
        return !(*this == other);
    }

    // Less-than for container compatibility (lexicographic order)
    bool operator<(const Point& other) const {
        if (x != other.x) return x < other.x;
        return y < other.y;
    }

    // Epsilon-based geometric equality check
    // Use this for geometric comparisons, NOT operator==
    bool equals(const Point& other, float epsilon = EPSILON) const {
        return std::abs(x - other.x) < epsilon && std::abs(y - other.y) < epsilon;
    }

    // Check if within distance threshold
    bool near(const Point& other, float threshold) const {
        return distance(*this, other) < threshold;
    }

    // Arithmetic operators
    Point operator+(const Point& other) const {
        return add(other);
    }

    Point operator-(const Point& other) const {
        return subtract(other);
    }

    Point operator*(float factor) const {
        return scale(factor);
    }

    Point operator/(float factor) const {
        return Point(x / factor, y / factor);
    }

    Point& operator+=(const Point& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Point& operator-=(const Point& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Point& operator*=(float factor) {
        x *= factor;
        y *= factor;
        return *this;
    }

    Point& operator/=(float factor) {
        x /= factor;
        y /= factor;
        return *this;
    }
};

} // namespace geom
} // namespace towngenerator
