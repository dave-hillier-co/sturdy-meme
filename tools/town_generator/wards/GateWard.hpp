/**
 * Ported from: Source/com/watabou/towngenerator/wards/GateWard.hx
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

class GateWard : public CommonWard {
public:
    GateWard(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : CommonWard(
            model,
            patch,
            10.0f + 50.0f * Random::getFloat() * Random::getFloat(),  // small to medium
            0.5f + Random::getFloat() * 0.3f,                          // grid chaos
            0.7f                                                        // size chaos
        )
    {}

    std::string getLabel() const override {
        return "Gate";
    }
};

} // namespace town
