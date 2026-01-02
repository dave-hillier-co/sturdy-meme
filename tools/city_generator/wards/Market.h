#pragma once

#include "Ward.h"
#include "../building/Model.h"
#include <limits>
#include <algorithm>
#include <string>

namespace towngenerator {
namespace wards {

/**
 * Market - Plaza/marketplace ward
 * Ported from Haxe Market class
 *
 * Creates an open plaza with optional centerpiece (statue or fountain)
 * Prevents adjacent markets from being placed
 */
class Market : public Ward {
public:
    using Ward::Ward;

    /**
     * Create market geometry
     * Creates a centerpiece (statue or fountain) in the plaza
     * 60% chance of square statue, 40% chance of circular fountain
     */
    void createGeometry() override {
        // Random radius for centerpiece
        float radius = 2.0f + Random::randomFloat() * 4.0f;

        // 60% chance of square (4 sides), 40% chance of circle (16 sides)
        int sides = (Random::randomFloat() < 0.6f) ? 4 : 16;

        // Create centerpiece polygon
        Polygon centerpiece = Polygon::regular(sides, radius);

        // Center it on the patch centroid
        Point patchCenter = patch->shape.centroid();
        centerpiece.offset(patchCenter.x, patchCenter.y);

        // Add random rotation for visual variety
        float rotation = Random::randomFloat() * 2.0f * M_PI;
        Point center = centerpiece.center();
        centerpiece.offset(-center.x, -center.y);
        centerpiece.rotate(rotation);
        centerpiece.offset(center.x, center.y);

        geometry = {centerpiece};
    }

    /**
     * Rate location suitability for market placement
     *
     * Prevents adjacent markets (returns infinity if neighbor is market)
     * Otherwise rates by area ratio to plaza (larger patches preferred)
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return Suitability score (lower is better, infinity = invalid)
     */
    static float rateLocation(Model* model, Patch* patch) {
        if (model == nullptr || patch == nullptr) {
            return std::numeric_limits<float>::max();
        }

        // Check all neighboring patches for existing markets
        // A neighbor is a patch that shares at least one vertex
        for (const auto& vertex : patch->shape.vertices) {
            auto neighbors = model->patchByVertex(vertex);

            for (auto* neighbor : neighbors) {
                // Skip self
                if (neighbor == patch) continue;

                // Check if neighbor is a market by comparing label
                if (neighbor->ward != nullptr) {
                    const char* label = neighbor->ward->getLabel();
                    if (label != nullptr && std::string(label) == "Market") {
                        // Adjacent market found - return infinity (invalid placement)
                        return std::numeric_limits<float>::infinity();
                    }
                }
            }
        }

        // No adjacent markets - rate by area ratio to plaza
        // Larger patches are preferred (lower ratio is better)
        if (model->plaza != nullptr) {
            float plazaArea = model->plaza->shape.square();
            float patchArea = patch->shape.square();

            // Return inverse ratio (smaller values are better for larger patches)
            return plazaArea / patchArea;
        }

        // No plaza - use inverse of patch area
        return 1.0f / patch->shape.square();
    }

    /**
     * Get ward label
     */
    const char* getLabel() const override {
        return "Market";
    }
};

} // namespace wards
} // namespace towngenerator
