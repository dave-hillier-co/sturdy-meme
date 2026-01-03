/**
 * Ported from: Source/com/watabou/towngenerator/wards/Slum.hx
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

class Slum : public CommonWard {
public:
    Slum(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : CommonWard(
            model,
            patch,
            10.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // small to medium
            0.6f + Random::getFloat() * 0.4f,                          // chaotic
            0.8f,                                                       // size chaos
            0.03f                                                       // empty prob
        )
    {}

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    std::string getLabel() const override {
        return "Slum";
    }
};

// Implementation note: rateLocation needs Model access
// Slums should be as far from the center as possible
// return -patch.shape.distance( model.plaza != null ? model.plaza.shape.center : model.center );

} // namespace town
