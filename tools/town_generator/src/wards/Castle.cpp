#include <town_generator/wards/Castle.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/CurtainWall.h>
#include <town_generator/geom/Polygon.h>
#include <cmath>

namespace town_generator {

Castle::Castle(Model* model, Patch* patch)
    : Ward(model, patch) {
    // Create the curtain wall for this castle
    std::vector<Patch*> patches = {patch};
    std::vector<Point> reserved;

    // Reserve corners that touch patches outside the city
    // (these should not be gates - gates should face the city)
    for (const auto& v : patch->shape.vertices) {
        auto neighbors = model->patchByVertex(v);
        bool touchesOutside = false;
        for (auto* neighbor : neighbors) {
            if (!neighbor->withinCity) {
                touchesOutside = true;
                break;
            }
        }
        if (touchesOutside) {
            reserved.push_back(v);
        }
    }

    wall = new CurtainWall(true, model, patches, reserved);
}

void Castle::createGeometry() {
    if (!patch) return;

    Polygon block = patch->shape.shrinkEq(Ward::MAIN_STREET * 2);
    float blockSq = std::sqrt(block.square()) * 4;
    geometry = Ward::createOrthoBuilding(block, blockSq, 0.6f);
}

} // namespace town_generator
