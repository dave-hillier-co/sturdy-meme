#pragma once

#include "Point.h"

namespace towngenerator {
namespace geom {

struct Segment {
    Point start;
    Point end;

    // Constructor
    Segment(const Point& start, const Point& end) : start(start), end(end) {}

    // dx - returns horizontal distance
    float dx() const {
        return end.x - start.x;
    }

    // dy - returns vertical distance
    float dy() const {
        return end.y - start.y;
    }

    // vector - returns direction vector from start to end
    Point vector() const {
        return end.subtract(start);
    }

    // length - returns length of segment
    float length() const {
        return Point::distance(start, end);
    }

    // Exact equality operator (for container compatibility)
    bool operator==(const Segment& other) const {
        return start == other.start && end == other.end;
    }

    bool operator!=(const Segment& other) const {
        return !(*this == other);
    }

    // Less-than for container compatibility
    bool operator<(const Segment& other) const {
        if (start != other.start) return start < other.start;
        return end < other.end;
    }

    // Epsilon-based geometric equality check
    bool equals(const Segment& other, float epsilon = EPSILON) const {
        return start.equals(other.start, epsilon) && end.equals(other.end, epsilon);
    }
};

} // namespace geom
} // namespace towngenerator
