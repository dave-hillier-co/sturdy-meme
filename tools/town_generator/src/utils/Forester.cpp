#include "town_generator/utils/Forester.h"
#include "town_generator/geom/GeomUtils.h"
#include <cmath>

namespace town_generator {
namespace utils {

// Static member initialization
Perlin* Forester::noise_ = nullptr;
bool Forester::noiseInitialized_ = false;

Perlin& Forester::getNoise() {
    if (!noiseInitialized_) {
        // Initialize with a random seed for variety
        noise_ = new Perlin(Random::intVal(0, 2147483647));
        noise_->gridSize = 0.1;  // Larger features
        noise_->amplitude = 1.0;
        noiseInitialized_ = true;
    }
    return *noise_;
}

std::vector<geom::Point> Forester::generateHexGrid(
    double minX, double minY,
    double maxX, double maxY,
    double spacing
) {
    std::vector<geom::Point> points;

    // Hex grid: offset every other row by half spacing
    double rowHeight = spacing * std::sqrt(3.0) / 2.0;
    int row = 0;

    for (double y = minY; y <= maxY; y += rowHeight, ++row) {
        double xOffset = (row % 2 == 0) ? 0.0 : spacing / 2.0;
        for (double x = minX + xOffset; x <= maxX; x += spacing) {
            points.push_back(geom::Point(x, y));
        }
    }

    return points;
}

std::vector<geom::Point> Forester::fillArea(
    const geom::Polygon& poly,
    double density,
    double spacing
) {
    std::vector<geom::Point> result;

    if (poly.length() < 3 || density <= 0) {
        return result;
    }

    // Get bounding box
    auto bounds = poly.getBounds();

    // Generate hex grid points
    auto gridPoints = generateHexGrid(
        bounds.left, bounds.top,
        bounds.right, bounds.bottom,
        spacing
    );

    // Filter points by polygon containment and noise density
    Perlin& noise = getNoise();

    for (const auto& p : gridPoints) {
        // Check if point is inside polygon
        if (!poly.contains(p)) {
            continue;
        }

        // Use noise to filter by density
        // Faithful to mfcg.js: (noise.get(x, y) + 1) / 2 < density
        double noiseVal = (noise.get(p.x, p.y) + 1.0) / 2.0;
        if (noiseVal < density) {
            result.push_back(p);
        }
    }

    return result;
}

std::vector<geom::Point> Forester::fillLine(
    const geom::Point& start,
    const geom::Point& end,
    double density
) {
    std::vector<geom::Point> result;

    if (density <= 0) {
        return result;
    }

    // Calculate number of candidate points along line
    // Faithful to mfcg.js: Math.ceil(distance / 3)
    double dist = geom::Point::distance(start, end);
    int numPoints = static_cast<int>(std::ceil(dist / 3.0));
    if (numPoints < 1) numPoints = 1;

    Perlin& noise = getNoise();

    for (int i = 0; i < numPoints; ++i) {
        // Interpolate with random offset within segment
        // Faithful to mfcg.js: (k + random) / numPoints
        double t = (static_cast<double>(i) + Random::floatVal()) / numPoints;

        geom::Point p = geom::GeomUtils::lerp(start, end, t);

        // Use noise to filter by density
        double noiseVal = (noise.get(p.x, p.y) + 1.0) / 2.0;
        if (noiseVal < density) {
            result.push_back(p);
        }
    }

    return result;
}

} // namespace utils
} // namespace town_generator
