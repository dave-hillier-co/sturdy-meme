#include <town_generator/wards/Market.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/utils/Random.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/geom/GeomUtils.h>
#include <cmath>
#include <limits>

namespace town_generator {

Market::Market(Model* model, Patch* patch)
    : Ward(model, patch) {
}

void Market::createGeometry() {
    if (!patch) return;

    // Fountain or statue
    bool statue = Random::getBool(0.6f);
    // We always offset a statue and sometimes a fountain
    bool offset = statue || Random::getBool(0.3f);

    Point v0, v1;
    bool hasEdge = false;

    if (statue || offset) {
        // We need an edge both for rotating a statue and offsetting
        float maxLength = -1.0f;
        patch->shape.forEdge([&v0, &v1, &maxLength, &hasEdge](const Point& p0, const Point& p1) {
            float len = p0.distance(p1);
            if (len > maxLength) {
                maxLength = len;
                v0 = p0;
                v1 = p1;
                hasEdge = true;
            }
        });
    }

    Polygon object;
    if (statue) {
        object = Polygon::rect(1.0f + Random::getFloat(), 1.0f + Random::getFloat());
        if (hasEdge) {
            object.rotate(std::atan2(v1.y - v0.y, v1.x - v0.x));
        }
    } else {
        object = Polygon::circle(1.0f + Random::getFloat());
    }

    if (offset && hasEdge) {
        Point gravity = GeomUtils::interpolate(v0, v1);
        Point target = GeomUtils::interpolate(patch->shape.centroid(), gravity,
                                               0.2f + Random::getFloat() * 0.4f);
        object.offset(target);
    } else {
        object.offset(patch->shape.centroid());
    }

    geometry = {object};
}

float Market::rateLocation(Model* model, Patch* patch) {
    // One market should not touch another
    // Market shouldn't be much larger than the plaza
    // TODO: Implement when Model class is available
    (void)model;
    if (!patch) return std::numeric_limits<float>::infinity();
    return patch->shape.square();
}

} // namespace town_generator
