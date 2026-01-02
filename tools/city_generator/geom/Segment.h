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
};

} // namespace geom
} // namespace towngenerator
