/**
 * Ported from: Source/com/watabou/towngenerator/wards/CommonWard.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>

#include "../wards/Ward.hpp"
#include "../building/Patch.hpp"

namespace town {

// Forward declaration
class Model;

class CommonWard : public Ward {
protected:
    float minSq;
    float gridChaos;
    float sizeChaos;
    float emptyProb;

public:
    CommonWard(
        std::shared_ptr<Model> model,
        std::shared_ptr<Patch> patch,
        float minSq,
        float gridChaos,
        float sizeChaos,
        float emptyProb = 0.04f
    )
        : Ward(model, patch)
        , minSq(minSq)
        , gridChaos(gridChaos)
        , sizeChaos(sizeChaos)
        , emptyProb(emptyProb)
    {}

    void createGeometry() override {
        Polygon block = getCityBlock();
        geometry = Ward::createAlleys(block, minSq, gridChaos, sizeChaos, emptyProb);

        if (!isEnclosed()) {
            filterOutskirts();
        }
    }

private:
    // Helper to check if patch is enclosed - delegates to Model
    bool isEnclosed();
};

} // namespace town
