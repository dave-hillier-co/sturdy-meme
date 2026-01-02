#include <town_generator/wards/MilitaryWard.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/utils/Random.h>
#include <town_generator/geom/Polygon.h>
#include <cmath>
#include <limits>

namespace town_generator {

MilitaryWard::MilitaryWard(Model* model, Patch* patch)
    : Ward(model, patch) {
}

void MilitaryWard::createGeometry() {
    if (!patch) return;

    Polygon block = getCityBlock();
    float minSq = std::sqrt(block.square()) * (1.0f + Random::getFloat());
    float gridChaos = 0.1f + Random::getFloat() * 0.3f;  // regular
    float sizeChaos = 0.3f;
    float emptyProb = 0.25f;  // squares

    geometry = Ward::createAlleys(block, minSq, gridChaos, sizeChaos, emptyProb);
}

float MilitaryWard::rateLocation(Model* model, Patch* patch) {
    // Military ward should border the citadel or the city walls
    // TODO: Implement when Model class is available with citadel and wall
    (void)model;
    (void)patch;
    return 0.0f;
}

} // namespace town_generator
