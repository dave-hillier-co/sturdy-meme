#pragma once

#include <town_generator/geom/Point.h>
#include <vector>

namespace town_generator {

class Spline {
public:
    static float curvature;

    static std::vector<Point> startCurve(const Point& p0, const Point& p1, const Point& p2) {
        Point tangent = p2.subtract(p0);
        Point control = p1.subtract(tangent.scale(curvature));
        return {control, p1};
    }

    static std::vector<Point> endCurve(const Point& p0, const Point& p1, const Point& p2) {
        Point tangent = p2.subtract(p0);
        Point control = p1.add(tangent.scale(curvature));
        return {control, p2};
    }

    static std::vector<Point> midCurve(const Point& p0, const Point& p1, const Point& p2, const Point& p3) {
        Point tangent1 = p2.subtract(p0);
        Point tangent2 = p3.subtract(p1);

        Point p1a = p1.add(tangent1.scale(curvature));
        Point p2a = p2.subtract(tangent2.scale(curvature));
        Point p12 = p1a.add(p2a).scale(0.5f);

        return {p1a, p12, p2a, p2};
    }
};

} // namespace town_generator
