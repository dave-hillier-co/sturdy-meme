#pragma once

#include "Ward.h"
#include "../geom/GeomUtils.h"
#include <cmath>

namespace towngenerator {
namespace wards {

/**
 * Farm - Agricultural area with housing and fields
 * Ported from Haxe Farm class
 *
 * Creates a small housing area positioned within the patch,
 * then subdivides it orthogonally
 */
class Farm : public Ward {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    Farm(Model* model, Patch* patch)
        : Ward(model, patch)
    {}

    /**
     * Create the farm geometry with housing area and fields
     * Overrides Ward::createGeometry()
     */
    void createGeometry() override {
        // Create 4x4 housing area centered at origin
        Polygon housing = Polygon::rect(-2.0f, -2.0f, 4.0f, 4.0f);

        // Position between random point and centroid
        Point randomPoint = patch->shape.random();
        Point centroid = patch->shape.centroid();
        float interpolation = 0.3f + Random::randomFloat() * 0.4f;
        Point pos = GeomUtils::interpolate(randomPoint, centroid, interpolation);

        // Rotate randomly
        housing.rotate(Random::randomFloat() * static_cast<float>(M_PI));

        // Move to position
        housing.offset(pos.x, pos.y);

        // Create orthogonal building subdivisions
        geometry = Ward::createOrthoBuilding(housing, 8.0f, 0.5f);
    }

    /**
     * Get display label for this ward type
     * @return "Farm"
     */
    const char* getLabel() const override {
        return "Farm";
    }
};

} // namespace wards
} // namespace towngenerator
