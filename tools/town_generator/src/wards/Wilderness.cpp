#include "town_generator/wards/Wilderness.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Forester.h"
#include "town_generator/utils/Random.h"
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace wards {

void Wilderness::createGeometry() {
    if (!patch) return;

    // Wilderness has no buildings, just a green area
    geometry.clear();

    // Get available area after street/wall insets with tower corner rounding
    greenArea = getAvailable();
    if (greenArea.length() < 3) {
        greenArea = patch->shape;  // Fallback to full shape if inset fails
    }

    // Trees are generated on demand via spawnTrees()
    treesGenerated = false;

    SDL_Log("Wilderness: Created green area with %zu vertices", greenArea.length());
}

std::vector<geom::Point> Wilderness::spawnTrees() const {
    if (treesGenerated) {
        return trees;
    }

    trees.clear();

    if (greenArea.length() < 3) {
        treesGenerated = true;
        return trees;
    }

    // High density for wilderness (0.7-0.9)
    double density = 0.7 + utils::Random::floatVal() * 0.2;

    // Use Forester for natural tree distribution
    trees = utils::Forester::fillArea(greenArea, density, 2.5);

    treesGenerated = true;

    SDL_Log("Wilderness: Spawned %zu trees", trees.size());

    return trees;
}

} // namespace wards
} // namespace town_generator
