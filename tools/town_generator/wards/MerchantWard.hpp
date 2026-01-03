/**
 * Ported from: Source/com/watabou/towngenerator/wards/MerchantWard.hx
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

class MerchantWard : public CommonWard {
public:
    MerchantWard(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : CommonWard(
            model,
            patch,
            50.0f + 60.0f * Random::getFloat() * Random::getFloat(),  // medium to large
            0.5f + Random::getFloat() * 0.3f,                          // moderately regular
            0.7f,                                                       // size chaos
            0.15f                                                       // empty prob
        )
    {}

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    std::string getLabel() const override {
        return "Merchant";
    }
};

// Implementation note: rateLocation needs Model access
// Merchant ward should be as close to the center as possible
// return patch.shape.distance( model.plaza != null ? model.plaza.shape.center : model.center );

} // namespace town
