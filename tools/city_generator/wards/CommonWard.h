#pragma once

#include "Ward.h"

namespace towngenerator {
namespace wards {

/**
 * CommonWard - Residential/commercial ward with alley-based building layout
 * Ported from Haxe CommonWard class
 *
 * Creates a grid of buildings with alleys, supporting configurable chaos and empty lots.
 */
class CommonWard : public Ward {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     * @param minSq Minimum building area
     * @param gridChaos Grid randomization factor (0 = regular grid, 1 = chaotic)
     * @param sizeChaos Size variation factor
     * @param emptyProb Probability of empty lots (default 0.04)
     */
    CommonWard(Model* model, Patch* patch,
               float minSq_, float gridChaos_, float sizeChaos_,
               float emptyProb_ = 0.04f)
        : Ward(model, patch)
        , minSq(minSq_)
        , gridChaos(gridChaos_)
        , sizeChaos(sizeChaos_)
        , emptyProb(emptyProb_)
    {}

    /**
     * Create the ward geometry by subdividing into alleys and buildings
     * Overrides Ward::createGeometry()
     */
    void createGeometry() override {
        auto block = getCityBlock();
        geometry = Ward::createAlleys(block, minSq, gridChaos, sizeChaos, emptyProb);

        // Filter outskirts if not fully enclosed
        // TODO: implement isEnclosed check in Model
        filterOutskirts();
    }

protected:
    float minSq;        // Minimum building square footage
    float gridChaos;    // Grid randomization (0 = regular grid, 1 = chaotic)
    float sizeChaos;    // Building size variation
    float emptyProb;    // Probability of empty lot
};

} // namespace wards
} // namespace towngenerator
