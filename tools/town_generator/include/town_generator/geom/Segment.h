#pragma once

#include <town_generator/geom/Point.h>

namespace town_generator {

class Segment {
public:
    Point start;
    Point end;

    Segment(const Point& start, const Point& end) : start(start), end(end) {}

    float dx() const { return end.x - start.x; }
    float dy() const { return end.y - start.y; }
    Point vector() const { return end.subtract(start); }
    float length() const { return start.distance(end); }
};

} // namespace town_generator
