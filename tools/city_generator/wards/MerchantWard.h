#pragma once

#include "CommonWard.h"
#include "../building/Model.h"
#include "../utils/Random.h"
#include "../geom/Point.h"

namespace towngenerator {
namespace wards {

using utils::Random;
using geom::Point;

/**
 * MerchantWard - Merchant/trading district
 * Ported from Haxe MerchantWard class
 *
 * Medium to large buildings with moderately regular layout.
 * Prefers locations near the plaza for market access.
 */
class MerchantWard : public CommonWard {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    MerchantWard(Model* model, Patch* patch)
        : CommonWard(model, patch,
            50.0f + 60.0f * Random::randomFloat() * Random::randomFloat(),  // medium to large
            0.5f + Random::randomFloat() * 0.3f,                            // moderately regular
            0.7f,                                                            // size chaos
            0.15f)                                                           // empty probability
    {}

    /**
     * Rate how suitable a patch is for a merchant ward
     * Prefers locations near the plaza (market center)
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return Distance from plaza (lower is better for merchants)
     */
    static float rateLocation(Model* model, Patch* patch) {
        if (patch == nullptr) return 0.0f;

        // Prefer locations near plaza if it exists, otherwise near city center
        Point target = (model->plaza != nullptr) ?
            model->plaza->shape.center() : model->center;

        return patch->shape.distance(target);
    }

    /**
     * Get display label for this ward type
     * @return "Merchant"
     */
    const char* getLabel() const override {
        return "Merchant";
    }
};

} // namespace wards
} // namespace towngenerator
