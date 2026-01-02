#include <town_generator/wards/Cathedral.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/Cutter.h>
#include <town_generator/utils/Random.h>
#include <town_generator/geom/Polygon.h>

namespace town_generator {

Cathedral::Cathedral(Model* model, Patch* patch)
    : Ward(model, patch) {
}

void Cathedral::createGeometry() {
    if (!patch) return;

    Polygon block = getCityBlock();

    if (Random::getBool(0.4f)) {
        // Ring layout
        geometry = Cutter::ring(block, 2.0f + Random::getFloat() * 4.0f);
    } else {
        // Ortho building layout
        geometry = Ward::createOrthoBuilding(block, 50.0f, 0.8f);
    }
}

float Cathedral::rateLocation(Model* model, Patch* patch) {
    // Ideally the main temple should overlook the plaza,
    // otherwise it should be as close to the plaza as possible
    // TODO: Implement when Model class is available
    // For now, return a neutral rating
    (void)model;
    if (!patch) return 0.0f;
    return patch->shape.square();
}

} // namespace town_generator
