#pragma once

#include "CommonWard.h"
#include <cmath>

namespace towngenerator {
namespace wards {

/**
 * Slum - Poor residential district with chaotic layout
 * Ported from Haxe Slum class
 *
 * Small to medium buildings with high chaos, located away from plaza/center
 */
class Slum : public CommonWard {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    Slum(Model* model, Patch* patch)
        : CommonWard(model, patch,
            10.0f + 30.0f * Random::randomFloat() * Random::randomFloat(),  // small to medium
            0.6f + Random::randomFloat() * 0.4f,  // chaotic grid (0.6-1.0)
            0.8f,   // size chaos
            0.03f)  // low empty probability
    {}

    /**
     * Rate how suitable a patch is for slum
     * Prefers locations away from plaza/center (lower status areas)
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return Negative distance from plaza/center (farther is better)
     */
    static float rateLocation(Model* model, Patch* patch) {
        Point referencePoint = (model->plaza != nullptr)
            ? model->plaza->shape.centroid()
            : model->center;

        return -patch->shape.distance(referencePoint);
    }

    /**
     * Get display label for this ward type
     * @return "Slum"
     */
    const char* getLabel() const override {
        return "Slum";
    }
};

} // namespace wards
} // namespace towngenerator
