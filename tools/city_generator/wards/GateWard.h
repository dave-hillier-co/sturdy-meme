#pragma once

#include "CommonWard.h"

namespace towngenerator {
namespace wards {

/**
 * GateWard - District near city gates
 * Ported from Haxe GateWard class
 *
 * Small to medium buildings with moderate chaos, located near city entrances
 */
class GateWard : public CommonWard {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    GateWard(Model* model, Patch* patch)
        : CommonWard(model, patch,
            10.0f + 50.0f * Random::randomFloat() * Random::randomFloat(),  // small to medium-large
            0.5f + Random::randomFloat() * 0.3f,  // moderate chaos (0.5-0.8)
            0.7f)   // size chaos, default emptyProb (0.04)
    {}

    /**
     * Get display label for this ward type
     * @return "Gate"
     */
    const char* getLabel() const override {
        return "Gate";
    }
};

} // namespace wards
} // namespace towngenerator
