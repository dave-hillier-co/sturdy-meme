#pragma once

#include "../geom/Polygon.h"
#include "../geom/Voronoi.h"
#include <vector>

namespace towngenerator {

// Forward declaration of Ward (in wards namespace)
namespace wards {
class Ward;
}

namespace building {

using geom::Point;
using geom::Polygon;
using geom::Region;

class Patch {
public:
    Polygon shape;
    wards::Ward* ward;
    bool withinWalls;
    bool withinCity;

    explicit Patch(const std::vector<Point>& vertices)
        : shape(vertices)
        , ward(nullptr)
        , withinWalls(false)
        , withinCity(false)
    {}

    static Patch fromRegion(const Region& r) {
        std::vector<Point> vertices;
        vertices.reserve(r.vertices.size());
        for (const auto* tr : r.vertices) {
            vertices.push_back(tr->c);
        }
        return Patch(vertices);
    }
};

} // namespace building
} // namespace towngenerator
