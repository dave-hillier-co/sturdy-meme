#pragma once

#include "town_generator/geom/Point.h"
#include <string>

namespace town_generator {
namespace building {

class City;

/**
 * Landmark - Named point of interest that persists across geometry changes
 * Faithful port from mfcg.js Landmark class
 *
 * Uses barycentric coordinates to track position relative to a cell's
 * triangulated shape. When geometry changes (e.g., bloating the citadel),
 * calling update() recalculates the position from the stored coordinates.
 */
class Landmark {
public:
    // Current position in world space
    geom::Point pos;

    // Display name
    std::string name;

    Landmark() = default;

    /**
     * Create a landmark at a position
     * @param model The city model
     * @param pos Initial position
     * @param name Display name (default: "Landmark")
     */
    Landmark(City* model, const geom::Point& pos, const std::string& name = "Landmark");

    /**
     * Assign this landmark to a cell by finding the containing triangle
     * and storing barycentric coordinates
     */
    void assign();

    /**
     * Update position from stored barycentric coordinates
     * Call this after geometry changes
     */
    void update();

    /**
     * Check if this landmark has been successfully assigned to a cell
     */
    bool isAssigned() const { return assigned_; }

    bool operator==(const Landmark& other) const {
        return pos == other.pos && name == other.name;
    }

    bool operator!=(const Landmark& other) const {
        return !(*this == other);
    }

private:
    City* model_ = nullptr;

    // Triangle vertices (from cell triangulation)
    geom::Point* p0_ = nullptr;
    geom::Point* p1_ = nullptr;
    geom::Point* p2_ = nullptr;

    // Barycentric coordinates within the triangle
    double i0_ = 0;
    double i1_ = 0;
    double i2_ = 0;

    // Whether assignment succeeded
    bool assigned_ = false;

    /**
     * Try to assign to a specific polygon
     * @return true if point is inside the polygon
     */
    bool assignPoly(const std::vector<geom::Point>& poly);
};

} // namespace building
} // namespace town_generator
