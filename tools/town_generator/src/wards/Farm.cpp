#include <town_generator/wards/Farm.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/utils/Random.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/geom/GeomUtils.h>
#include <cmath>

namespace town_generator {

Farm::Farm(Model* model, Patch* patch)
    : Ward(model, patch) {
}

void Farm::createGeometry() {
    if (!patch) return;

    Polygon housing = Polygon::rect(4.0f, 4.0f);

    // Pick a random point on the patch boundary and interpolate towards centroid
    const auto& vertices = patch->shape.vertices;
    if (vertices.empty()) return;

    int randomIndex = Random::getInt(0, static_cast<int>(vertices.size()));
    Point randomPoint = vertices[randomIndex];
    Point pos = GeomUtils::interpolate(randomPoint, patch->shape.centroid(),
                                        0.3f + Random::getFloat() * 0.4f);

    housing.rotate(Random::getFloat() * static_cast<float>(M_PI));
    housing.offset(pos);

    geometry = Ward::createOrthoBuilding(housing, 8.0f, 0.5f);
}

} // namespace town_generator
