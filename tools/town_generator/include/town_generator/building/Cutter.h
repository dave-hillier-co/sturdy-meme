#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/geom/GeomUtils.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace town_generator {

class Cutter {
public:
    static std::vector<Polygon> bisect(Polygon& poly, const Point& vertex,
                                        float ratio = 0.5f, float angle = 0.0f, float gap = 0.0f);

    static std::vector<Polygon> radial(const Polygon& poly, const Point* center = nullptr, float gap = 0.0f);

    static std::vector<Polygon> semiRadial(const Polygon& poly, const Point* center = nullptr, float gap = 0.0f);

    static std::vector<Polygon> ring(const Polygon& poly, float thickness);
};

} // namespace town_generator
