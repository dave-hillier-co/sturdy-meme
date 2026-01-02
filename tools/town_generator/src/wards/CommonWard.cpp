#include <town_generator/wards/CommonWard.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/geom/Polygon.h>

namespace town_generator {

CommonWard::CommonWard(Model* model, Patch* patch)
    : Ward(model, patch)
    , minSq_(0.0f)
    , gridChaos_(0.0f)
    , sizeChaos_(0.0f)
    , emptyProb_(0.04f) {
}

CommonWard::CommonWard(Model* model, Patch* patch, float minSq, float gridChaos, float sizeChaos, float emptyProb)
    : Ward(model, patch)
    , minSq_(minSq)
    , gridChaos_(gridChaos)
    , sizeChaos_(sizeChaos)
    , emptyProb_(emptyProb) {
}

void CommonWard::createGeometry() {
    if (!patch) return;

    Polygon block = getCityBlock();
    geometry = Ward::createAlleys(block, minSq_, gridChaos_, sizeChaos_, emptyProb_);

    // TODO: Implement filterOutskirts when model->isEnclosed is available
    // if (!model->isEnclosed(patch)) {
    //     filterOutskirts();
    // }
}

} // namespace town_generator
