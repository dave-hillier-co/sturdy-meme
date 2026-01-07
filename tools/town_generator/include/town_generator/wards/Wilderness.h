#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {
namespace wards {

/**
 * Wilderness - Forest/wild ward type for non-urban areas
 * Faithful port from mfcg.js Wilderness class
 *
 * Represents forested or wild areas outside the city proper.
 * Characterized by:
 * - No buildings
 * - Dense tree coverage
 * - Natural, organic boundary
 */
class Wilderness : public Ward {
public:
    Wilderness() = default;

    std::string getName() const override { return "Wilderness"; }

    void createGeometry() override;

    // Spawn trees using Forester::fillArea
    std::vector<geom::Point> spawnTrees() const;

    // Get the green area polygon (for rendering)
    const geom::Polygon& getGreenArea() const { return greenArea; }

    bool operator==(const Wilderness& other) const { return Ward::operator==(other); }
    bool operator!=(const Wilderness& other) const { return !(*this == other); }

private:
    // The green/forest area (slightly inset from patch shape)
    geom::Polygon greenArea;

    // Cached tree positions
    mutable std::vector<geom::Point> trees;
    mutable bool treesGenerated = false;
};

} // namespace wards
} // namespace town_generator
