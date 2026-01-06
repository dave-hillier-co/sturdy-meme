#pragma once

#include "town_generator/wards/Ward.h"
#include <vector>

namespace town_generator {
namespace wards {

/**
 * Park - Open green space with paths and features
 *
 * Faithful port from mfcg.js Park/Hf (green area generation).
 * Parks contain:
 * - Smoothed organic boundary
 * - Internal paths
 * - Optional structures (pavilion, fountain, benches)
 * - Tree spawn points for rendering
 */
class Park : public Ward {
public:
    // Smoothed green area boundary
    geom::Polygon greenArea;

    // Internal paths through the park
    std::vector<std::vector<geom::Point>> paths;

    // Tree spawn points
    std::vector<geom::Point> trees;

    // Features (fountains, benches, etc.)
    std::vector<geom::Polygon> features;

    Park() = default;

    std::string getName() const override { return "Park"; }

    void createGeometry() override;

    // Spawn trees for rendering (faithful to mfcg.js spawnTrees)
    std::vector<geom::Point> spawnTrees() const;

    bool operator==(const Park& other) const { return Ward::operator==(other); }
    bool operator!=(const Park& other) const { return !(*this == other); }

private:
    // Create wavy boundary for organic look
    geom::Polygon createWavyBoundary(const geom::Polygon& shape);

    // Create internal paths
    void createPaths();

    // Add park features (fountain, benches)
    void addFeatures();
};

} // namespace wards
} // namespace town_generator
