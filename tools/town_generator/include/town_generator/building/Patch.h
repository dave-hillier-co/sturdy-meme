#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/geom/Voronoi.h>
#include <vector>

namespace town_generator {

class Ward;  // Forward declaration

class Patch {
public:
    Polygon shape;
    Ward* ward = nullptr;

    bool withinWalls = false;
    bool withinCity = false;

    explicit Patch(const std::vector<Point>& vertices) : shape(vertices) {}

    static Patch* fromRegion(const Region& r) {
        std::vector<Point> verts;
        for (auto* tri : r.vertices) {
            verts.push_back(tri->c);
        }
        return new Patch(verts);
    }
};

} // namespace town_generator
