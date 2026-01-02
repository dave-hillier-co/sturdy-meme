#pragma once

#include "Ward.h"
#include "../building/Model.h"
#include "../building/Cutter.h"
#include <limits>

namespace towngenerator {
namespace wards {

/**
 * Cathedral - Religious/temple ward with distinctive architecture
 * Ported from Haxe Cathedral class
 *
 * Creates either concentric ring pattern or large orthogonal building
 * Prefers locations near the plaza
 */
class Cathedral : public Ward {
public:
    using Ward::Ward;

    /**
     * Create cathedral geometry
     * 40% chance of ring pattern, 60% chance of orthogonal building
     */
    void createGeometry() override {
        Polygon block = getCityBlock();

        // 40% chance of ring pattern
        if (Random::randomFloat() < 0.4f) {
            // Ring pattern with random depth
            float depth = 2.0f + Random::randomFloat() * 4.0f;
            geometry = Cutter::ring(block, depth);
        } else {
            // Large orthogonal building
            geometry = Ward::createOrthoBuilding(block, 50.0f, 0.8f);
        }
    }

    /**
     * Rate location suitability for cathedral placement
     *
     * Prefers patches that border the plaza (negative score = better)
     * Otherwise rates by distance to plaza/center weighted by area
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return Suitability score (lower is better)
     */
    static float rateLocation(Model* model, Patch* patch) {
        if (model == nullptr || patch == nullptr) {
            return std::numeric_limits<float>::max();
        }

        // If there's a plaza and this patch borders it, highly prefer it
        if (model->plaza != nullptr && patch->shape.borders(model->plaza->shape)) {
            // Negative score (lower is better), inversely proportional to area
            return -1.0f / patch->shape.square();
        }

        // Otherwise rate by distance to plaza (or center) weighted by area
        Point target = (model->plaza != nullptr)
            ? model->plaza->shape.center()
            : model->center;

        float distance = patch->shape.distance(target);
        float area = patch->shape.square();

        // Distance * area (prefer close and small patches)
        return distance * area;
    }

    /**
     * Get ward label
     */
    const char* getLabel() const override {
        return "Temple";
    }
};

} // namespace wards
} // namespace towngenerator
