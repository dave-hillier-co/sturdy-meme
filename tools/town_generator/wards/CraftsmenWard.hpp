/**
 * Ported from: Source/com/watabou/towngenerator/wards/CraftsmenWard.hx
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

class CraftsmenWard : public CommonWard {
public:
    CraftsmenWard(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : CommonWard(
            model,
            patch,
            10.0f + 80.0f * Random::getFloat() * Random::getFloat(),  // small to large
            0.5f + Random::getFloat() * 0.2f,                          // moderately regular
            0.6f                                                        // size chaos
        )
    {}

    std::string getLabel() const override {
        return "Craftsmen";
    }
};

} // namespace town
