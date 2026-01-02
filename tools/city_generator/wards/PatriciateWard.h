#pragma once

#include "CommonWard.h"
#include "../utils/Random.h"

namespace towngenerator {
namespace wards {

using utils::Random;

/**
 * PatriciateWard - Wealthy patrician/noble district
 * Ported from Haxe PatriciateWard class
 *
 * Large buildings with regular, orderly layout.
 * Represents wealthy merchant and noble estates.
 */
class PatriciateWard : public CommonWard {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    PatriciateWard(Model* model, Patch* patch)
        : CommonWard(model, patch,
            80.0f + 30.0f * Random::randomFloat(),  // large buildings
            0.5f + Random::randomFloat() * 0.3f,    // moderately regular
            0.2f)                                    // low size chaos (uniform)
    {}

    /**
     * Rate how suitable a patch is for a patriciate ward
     *
     * TODO: Full implementation should include:
     * - Negative rating when bordering parks
     * - Positive rating when bordering slums
     * - Prefers affluent areas
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return Suitability score (higher is better)
     */
    static float rateLocation(Model* model, Patch* patch) {
        // Placeholder implementation - returns neutral rating
        // Original Haxe had comments indicating complex logic but no implementation
        return 0.0f;
    }

    /**
     * Get display label for this ward type
     * @return "Patriciate"
     */
    const char* getLabel() const override {
        return "Patriciate";
    }
};

} // namespace wards
} // namespace towngenerator
