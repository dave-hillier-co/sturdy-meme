#pragma once

#include "CommonWard.h"
#include "../utils/Random.h"

namespace towngenerator {
namespace wards {

using utils::Random;

/**
 * CraftsmenWard - Craftsmen/artisan district
 * Ported from Haxe CraftsmenWard class
 *
 * Small to large buildings with moderately regular layout.
 * Represents workshops and artisan quarters.
 */
class CraftsmenWard : public CommonWard {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    CraftsmenWard(Model* model, Patch* patch)
        : CommonWard(model, patch,
            10.0f + 80.0f * Random::randomFloat() * Random::randomFloat(),  // small to large
            0.5f + Random::randomFloat() * 0.2f,                            // moderately regular
            0.6f)                                                            // size chaos
    {}

    /**
     * Get display label for this ward type
     * @return "Craftsmen"
     */
    const char* getLabel() const override {
        return "Craftsmen";
    }
};

} // namespace wards
} // namespace towngenerator
