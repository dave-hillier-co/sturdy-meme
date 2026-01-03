#pragma once

#include "town_generator/geom/Point.h"

namespace town_generator {
namespace geom {

/**
 * Segment - Line segment, faithful port from Haxe TownGeneratorOS
 */
class Segment {
public:
    Point start;
    Point end;

    Segment() : start(), end() {}
    Segment(const Point& start, const Point& end) : start(start), end(end) {}

    // Computed properties
    double dx() const { return end.x - start.x; }
    double dy() const { return end.y - start.y; }

    Point vector() const { return end.subtract(start); }
    double length() const { return Point::distance(start, end); }

    // Equality operators
    bool operator==(const Segment& other) const {
        return start == other.start && end == other.end;
    }

    bool operator!=(const Segment& other) const {
        return !(*this == other);
    }
};

} // namespace geom
} // namespace town_generator
