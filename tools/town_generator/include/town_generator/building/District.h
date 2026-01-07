#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Cell.h"
#include "town_generator/wards/Ward.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <memory>

namespace town_generator {
namespace building {

class City;

/**
 * District - Groups multiple cells into a cohesive area with shared parameters
 * Faithful port from mfcg.js District class
 *
 * Districts allow adjacent cells of the same ward type to share:
 * - Alley parameters (minSq, gridChaos, sizeChaos, blockSize)
 * - Visual styling
 * - Building density patterns
 */
class District {
public:
    // The cells in this district
    std::vector<Cell*> cells;

    // The unified ward type for this district
    wards::Ward* ward = nullptr;

    // Shared alley parameters for all cells in district
    wards::AlleyParams alleys;

    // Greenery level (0-1)
    double greenery = 0.0;

    // Whether this is an urban (walled) district
    bool urban = false;

    // The model this district belongs to
    City* model = nullptr;

    // Border of the district (circumference of all cells)
    geom::Polygon border;

    // District type (from ward)
    std::string type;

    District() = default;

    // Create a district from a starting patch
    explicit District(Cell* startPatch, City* model);

    // Build the district by growing from start patch to include neighbors of same type
    void build();

    // Create the alley parameters for this district (faithful to mfcg.js createParams)
    void createParams();

    // Get the combined shape of all cells
    geom::Polygon getShape() const;

    // Create geometry for all cells in district
    void createGeometry();
};

// NOTE: DistrictBuilder functionality is provided by WardGroupBuilder in WardGroup.h
// The WardGroupBuilder class groups adjacent cells of the same ward type and
// handles block/lot generation. District is kept for naming/styling purposes.

} // namespace building
} // namespace town_generator
