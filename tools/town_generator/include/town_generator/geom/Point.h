#pragma once

#include <cmath>

namespace town_generator {

struct Point {
    float x = 0.0f;
    float y = 0.0f;

    Point() = default;
    Point(float x, float y) : x(x), y(y) {}

    Point clone() const {
        return Point(x, y);
    }

    float length() const {
        return std::sqrt(x * x + y * y);
    }

    void normalize(float targetLength = 1.0f) {
        float len = length();
        if (len > 0) {
            float scale = targetLength / len;
            x *= scale;
            y *= scale;
        }
    }

    Point add(const Point& q) const {
        return Point(x + q.x, y + q.y);
    }

    Point subtract(const Point& q) const {
        return Point(x - q.x, y - q.y);
    }

    float distance(const Point& q) const {
        return subtract(q).length();
    }

    static Point interpolate(const Point& a, const Point& b, float t) {
        return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    // PointExtender methods
    void set(const Point& q) {
        x = q.x;
        y = q.y;
    }

    Point scale(float f) const {
        return Point(x * f, y * f);
    }

    Point norm(float targetLength = 1.0f) const {
        Point p = clone();
        p.normalize(targetLength);
        return p;
    }

    void addEq(const Point& q) {
        x += q.x;
        y += q.y;
    }

    void subEq(const Point& q) {
        x -= q.x;
        y -= q.y;
    }

    void scaleEq(float f) {
        x *= f;
        y *= f;
    }

    float atan() const {
        return std::atan2(y, x);
    }

    float dot(const Point& p2) const {
        return x * p2.x + y * p2.y;
    }

    Point rotate90() const {
        return Point(-y, x);
    }

    bool equals(const Point& other, float epsilon = 0.0001f) const {
        return std::abs(x - other.x) < epsilon && std::abs(y - other.y) < epsilon;
    }

    Point operator+(const Point& q) const { return add(q); }
    Point operator-(const Point& q) const { return subtract(q); }
    Point operator*(float f) const { return scale(f); }
    bool operator==(const Point& other) const { return equals(other); }
    bool operator!=(const Point& other) const { return !equals(other); }
};

} // namespace town_generator
