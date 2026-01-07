#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/utils/Noise.h"
#include "town_generator/utils/Random.h"
#include <vector>

namespace town_generator {
namespace utils {

/**
 * Forester - Centralized tree/vegetation generation
 * Faithful port from mfcg.js Forester (Ae) class
 *
 * Uses a hex grid pattern combined with Perlin noise filtering
 * to create natural-looking tree distributions.
 */
class Forester {
public:
    /**
     * Fill a polygon area with tree points using hex grid + noise filtering
     * Faithful to mfcg.js Forester.fillArea
     *
     * @param poly The polygon to fill with trees
     * @param density Fill density (0-1), higher = more trees
     * @param spacing Grid spacing (default 3.0 units between tree candidates)
     * @return Vector of tree points inside the polygon
     */
    static std::vector<geom::Point> fillArea(
        const geom::Polygon& poly,
        double density = 1.0,
        double spacing = 3.0
    );

    /**
     * Fill along a line with tree points
     * Faithful to mfcg.js Forester.fillLine
     *
     * @param start Start point of line
     * @param end End point of line
     * @param density Fill density (0-1), higher = more trees
     * @return Vector of tree points along the line
     */
    static std::vector<geom::Point> fillLine(
        const geom::Point& start,
        const geom::Point& end,
        double density = 1.0
    );

    /**
     * Get the shared noise generator (lazy-initialized)
     * Allows consistent noise patterns across fills
     */
    static Perlin& getNoise();

private:
    // Shared Perlin noise for consistent patterns (lazy-initialized)
    static Perlin* noise_;
    static bool noiseInitialized_;

    /**
     * Generate hex grid points within a bounding box
     * @param bounds Bounding rectangle (minX, minY, maxX, maxY)
     * @param spacing Spacing between points
     * @return Grid points
     */
    static std::vector<geom::Point> generateHexGrid(
        double minX, double minY,
        double maxX, double maxY,
        double spacing
    );
};

} // namespace utils
} // namespace town_generator
