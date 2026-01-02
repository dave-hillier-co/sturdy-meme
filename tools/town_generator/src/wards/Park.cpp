#include <town_generator/wards/Park.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/Cutter.h>
#include <town_generator/geom/Polygon.h>

namespace town_generator {

Park::Park(Model* model, Patch* patch)
    : Ward(model, patch) {
}

void Park::createGeometry() {
    if (!patch) return;

    Polygon block = getCityBlock();

    if (block.compactness() >= 0.7f) {
        geometry = Cutter::radial(block, nullptr, Ward::ALLEY);
    } else {
        geometry = Cutter::semiRadial(block, nullptr, Ward::ALLEY);
    }
}

} // namespace town_generator
