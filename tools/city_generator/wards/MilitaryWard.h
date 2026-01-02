#pragma once

#include "Ward.h"
#include <cmath>
#include <limits>

namespace towngenerator {
namespace wards {

/**
 * MilitaryWard - Military district with chaotic alley layout
 * Ported from Haxe MilitaryWard class
 *
 * Located near walls or citadel, with high empty lot probability
 */
class MilitaryWard : public Ward {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    MilitaryWard(Model* model, Patch* patch)
        : Ward(model, patch)
    {}

    /**
     * Create the ward geometry with chaotic alleys
     * Overrides Ward::createGeometry()
     */
    void createGeometry() override {
        auto block = getCityBlock();
        float area = block.square();
        float minSq = std::sqrt(area) * (1.0f + Random::randomFloat());
        float gridChaos = 0.1f + Random::randomFloat() * 0.3f;
        float sizeChaos = 0.3f;
        float emptyProb = 0.25f;

        geometry = Ward::createAlleys(block, minSq, gridChaos, sizeChaos, emptyProb);
    }

    /**
     * Rate how suitable a patch is for military ward
     * Prefers locations near walls or citadel
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return 0 if borders citadel, 1 if borders wall, infinity otherwise
     */
    static float rateLocation(Model* model, Patch* patch) {
        // Check if borders citadel
        if (model->citadel != nullptr &&
            model->citadel->shape.borders(patch->shape)) {
            return 0.0f;
        }

        // Check if borders wall
        if (model->wall != nullptr && model->wall->borders(*patch)) {
            return 1.0f;
        }

        // If no citadel and no wall, rate as 0
        if (model->citadel == nullptr && model->wall == nullptr) {
            return 0.0f;
        }

        // Otherwise, not suitable
        return std::numeric_limits<float>::infinity();
    }

    /**
     * Get display label for this ward type
     * @return "Military"
     */
    const char* getLabel() const override {
        return "Military";
    }
};

} // namespace wards
} // namespace towngenerator
