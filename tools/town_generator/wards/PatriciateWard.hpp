/**
 * Ported from: Source/com/watabou/towngenerator/wards/PatriciateWard.hx
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

// Forward declarations
class Model;
class Park;
class Slum;

class PatriciateWard : public CommonWard {
public:
    PatriciateWard(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : CommonWard(
            model,
            patch,
            80.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // large
            0.5f + Random::getFloat() * 0.3f,                          // moderately regular
            0.8f,                                                       // size chaos
            0.2f                                                        // empty prob
        )
    {}

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    std::string getLabel() const override {
        return "Patriciate";
    }
};

// Implementation note: rateLocation needs Model access and dynamic_cast to check ward types
// Patriciate ward prefers to border a park and not to border slums
// var rate = 0;
// for (p in model.patches) if (p.ward != null && p.shape.borders( patch.shape )) {
//     if (Std.is( p.ward, Park ))
//         rate--
//     else if (Std.is( p.ward, Slum ))
//         rate++;
// }
// return rate;

} // namespace town
