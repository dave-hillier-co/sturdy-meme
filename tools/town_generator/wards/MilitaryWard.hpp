/**
 * Ported from: Source/com/watabou/towngenerator/wards/MilitaryWard.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>
#include <cmath>

#include "../wards/Ward.hpp"
#include "../building/Patch.hpp"
#include "../utils/Random.hpp"

namespace town {

// Forward declaration
class Model;

class MilitaryWard : public Ward {
public:
    MilitaryWard(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : Ward(model, patch) {}

    void createGeometry() override {
        Polygon block = getCityBlock();
        geometry = Ward::createAlleys(
            block,
            std::sqrt(block.square()) * (1.0f + Random::getFloat()),  // min square
            0.1f + Random::getFloat() * 0.3f,                          // regular
            0.3f,                                                       // size chaos
            0.25f                                                       // squares (empty prob)
        );
    }

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    std::string getLabel() const override {
        return "Military";
    }
};

// Implementation note: rateLocation needs Model access
// Military ward should border the citadel or the city walls
// return
//     if (model.citadel != null && model.citadel.shape.borders( patch.shape ))
//         0
//     else if (model.wall != null && model.wall.borders( patch ))
//         1
//     else
//         (model.citadel == null && model.wall == null ? 0 : Math.POSITIVE_INFINITY);

} // namespace town
