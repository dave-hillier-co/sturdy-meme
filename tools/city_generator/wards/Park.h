#pragma once

#include "Ward.h"
#include "../building/Cutter.h"

namespace towngenerator {
namespace wards {

/**
 * Park - Green space with radial layout
 * Ported from Haxe Park class
 *
 * Uses radial or semi-radial subdivision based on block compactness
 */
class Park : public Ward {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    Park(Model* model, Patch* patch)
        : Ward(model, patch)
    {}

    /**
     * Create the park geometry with radial subdivision
     * Overrides Ward::createGeometry()
     */
    void createGeometry() override {
        auto block = getCityBlock();
        float compactness = block.compactness();

        // Use radial if compact, semi-radial if not
        if (compactness >= 0.7f) {
            geometry = Cutter::radial(block, std::nullopt, Ward::ALLEY);
        } else {
            geometry = Cutter::semiRadial(block, std::nullopt, Ward::ALLEY);
        }
    }

    /**
     * Get display label for this ward type
     * @return "Park"
     */
    const char* getLabel() const override {
        return "Park";
    }
};

} // namespace wards
} // namespace towngenerator
