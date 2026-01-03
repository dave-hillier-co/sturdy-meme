/**
 * Ported from: Source/com/watabou/towngenerator/wards/AdministrationWard.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>

#include "../wards/CommonWard.hpp"
#include "../building/Patch.hpp"
#include "../utils/Random.hpp"

namespace town {

// Forward declaration
class Model;

class AdministrationWard : public CommonWard {
public:
    AdministrationWard(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : CommonWard(
            model,
            patch,
            80.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // large
            0.1f + Random::getFloat() * 0.3f,                          // regular
            0.3f                                                        // size chaos
        )
    {}

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    std::string getLabel() const override {
        return "Administration";
    }
};

// Implementation note: rateLocation needs Model access
// Ideally administration ward should overlook the plaza,
// otherwise it should be as close to the plaza as possible
// return model.plaza != null ?
//     (patch.shape.borders( model.plaza.shape ) ? 0 : patch.shape.distance( model.plaza.shape.center )) :
//     patch.shape.distance( model.center );

} // namespace town
