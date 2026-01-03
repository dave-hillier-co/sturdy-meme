/**
 * Ported from: Source/com/watabou/towngenerator/wards/Cathedral.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>

#include "../wards/Ward.hpp"
#include "../building/Patch.hpp"
#include "../utils/Random.hpp"
#include "../building/Cutter.hpp"

namespace town {

// Forward declaration
class Model;

class Cathedral : public Ward {
public:
    Cathedral(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : Ward(model, patch) {}

    void createGeometry() override {
        Polygon block = getCityBlock();
        if (Random::getBool(0.4f)) {
            geometry = Cutter::ring(block, 2.0f + Random::getFloat() * 4.0f);
        } else {
            geometry = Ward::createOrthoBuilding(block, 50.0f, 0.8f);
        }
    }

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    std::string getLabel() const override {
        return "Temple";
    }
};

// Implementation note: rateLocation is defined in Model.hpp or a cpp file
// because it needs access to Model's plaza member
//
// Ideally the main temple should overlook the plaza,
// otherwise it should be as close to the plaza as possible
// return if (model.plaza != null && patch.shape.borders( model.plaza.shape ))
//     -1/patch.shape.square
// else
//     patch.shape.distance( model.plaza != null ? model.plaza.shape.center : model.center ) * patch.shape.square;

} // namespace town
