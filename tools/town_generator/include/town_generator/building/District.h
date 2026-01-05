#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Patch.h"
#include "town_generator/wards/Ward.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <memory>

namespace town_generator {
namespace building {

class Model;

/**
 * District - Groups multiple patches into a cohesive area with shared parameters
 * Faithful port from mfcg.js District class
 *
 * Districts allow adjacent patches of the same ward type to share:
 * - Alley parameters (minSq, gridChaos, sizeChaos, blockSize)
 * - Visual styling
 * - Building density patterns
 */
class District {
public:
    // The patches in this district
    std::vector<Patch*> patches;

    // The unified ward type for this district
    wards::Ward* ward = nullptr;

    // Shared alley parameters for all patches in district
    wards::AlleyParams alleys;

    // Greenery level (0-1)
    double greenery = 0.0;

    // Whether this is an urban (walled) district
    bool urban = false;

    // The model this district belongs to
    Model* model = nullptr;

    // Border of the district (circumference of all patches)
    geom::Polygon border;

    // District type (from ward)
    std::string type;

    District() = default;

    // Create a district from a starting patch
    explicit District(Patch* startPatch, Model* model);

    // Build the district by growing from start patch to include neighbors of same type
    void build();

    // Create the alley parameters for this district (faithful to mfcg.js createParams)
    void createParams();

    // Get the combined shape of all patches
    geom::Polygon getShape() const;

    // Create geometry for all patches in district
    void createGeometry();
};

/**
 * DistrictBuilder - Creates districts from a model's patches
 * Faithful port from mfcg.js DistrictBuilder
 */
class DistrictBuilder {
public:
    explicit DistrictBuilder(Model* model) : model_(model) {}

    // Build all districts from patches
    std::vector<std::unique_ptr<District>> build();

private:
    Model* model_;

    // Find all patches that can be grouped into a district starting from seed
    std::vector<Patch*> growDistrict(Patch* seed, std::vector<Patch*>& unassigned);
};

} // namespace building
} // namespace town_generator
