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
 * AdministrationWard - Government/administrative district
 * Ported from Haxe AdministrationWard class
 *
 * Large buildings with very regular layout.
 * Prefers locations adjacent to or near the plaza.
 */
class AdministrationWard : public CommonWard {
public:
    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this ward occupies
     */
    AdministrationWard(Model* model, Patch* patch)
        : CommonWard(model, patch,
            80.0f + 30.0f * Random::randomFloat() * Random::randomFloat(),  // large buildings
            0.1f + Random::randomFloat() * 0.3f,                            // very regular
            0.3f)                                                            // moderate size chaos
    {}

    /**
     * Rate how suitable a patch is for an administration ward
     * Prefers locations bordering or near the plaza (civic center)
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return Distance from plaza (0 if bordering, distance otherwise)
     */
    static float rateLocation(Model* model, Patch* patch) {
        if (patch == nullptr) return 0.0f;

        if (model->plaza != nullptr) {
            // Perfect score (0) if bordering plaza, otherwise distance to plaza center
            return patch->shape.borders(model->plaza->shape) ?
                0.0f : patch->shape.distance(model->plaza->shape.center());
        } else {
            // Fallback to distance from city center
            return patch->shape.distance(model->center);
        }
    }

    /**
     * Get display label for this ward type
     * @return "Administration"
     */
    const char* getLabel() const override {
        return "Administration";
    }
};

} // namespace wards
} // namespace towngenerator
