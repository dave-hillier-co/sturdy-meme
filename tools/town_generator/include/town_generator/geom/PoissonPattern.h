#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <cmath>

namespace town_generator {
namespace geom {

/**
 * FillablePoly - Simple polygon wrapper for point containment testing
 */
class FillablePoly {
public:
    std::vector<Point> polygon;
    double x, y, width, height;  // Bounding box

    FillablePoly() : x(0), y(0), width(0), height(0) {}

    explicit FillablePoly(const std::vector<Point>& poly) : polygon(poly) {
        computeBounds();
    }

    void computeBounds() {
        if (polygon.empty()) {
            x = y = width = height = 0;
            return;
        }

        double minX = polygon[0].x, maxX = polygon[0].x;
        double minY = polygon[0].y, maxY = polygon[0].y;

        for (const auto& p : polygon) {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }

        x = minX;
        y = minY;
        width = maxX - minX;
        height = maxY - minY;
    }

    /**
     * Check if point is inside polygon using ray casting algorithm
     */
    bool containsPoint(const Point& point) const {
        bool inside = false;
        size_t n = polygon.size();

        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Point& pi = polygon[i];
            const Point& pj = polygon[j];

            if ((pi.y > point.y) != (pj.y > point.y) &&
                point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x) {
                inside = !inside;
            }
        }

        return inside;
    }
};

/**
 * PoissonPattern - Poisson disk sampling pattern generator
 *
 * Generates evenly-distributed random points using Bridson's algorithm.
 * Used for placing trees and other natural features.
 *
 * Faithful port from mfcg-clean/geometry/PoissonPattern.js
 */
class PoissonPattern {
public:
    double width;
    double height;
    double dist;
    double dist2;
    double cellSize;
    int gridWidth;
    int gridHeight;
    std::vector<int> grid;  // -1 for empty cells, index into points otherwise
    std::vector<Point> points;
    std::vector<size_t> queue;  // Indices into points

    /**
     * Create a Poisson pattern
     * @param width Pattern width
     * @param height Pattern height
     * @param dist Minimum distance between points
     * @param unevenness Amount of randomness to add (0-1)
     */
    PoissonPattern(double width, double height, double dist, double unevenness = 0)
        : width(width)
        , height(height)
        , dist(dist)
        , dist2(dist * dist)
        , cellSize(dist / std::sqrt(2.0))
        , gridWidth(static_cast<int>(std::ceil(width / cellSize)))
        , gridHeight(static_cast<int>(std::ceil(height / cellSize)))
    {
        grid.resize(gridWidth * gridHeight, -1);

        // Generate initial point
        emit(Point(
            width * utils::Random::floatVal(),
            height * utils::Random::floatVal()
        ));

        // Generate all points
        while (step()) {}

        // Add unevenness if requested
        if (unevenness > 0) {
            uneven(unevenness);
        }
    }

    /**
     * Add a point to the pattern
     */
    void emit(const Point& point) {
        int pointIdx = static_cast<int>(points.size());
        points.push_back(point);
        queue.push_back(points.size() - 1);

        int gridIdx = static_cast<int>(point.y / cellSize) * gridWidth +
                      static_cast<int>(point.x / cellSize);
        if (gridIdx >= 0 && gridIdx < static_cast<int>(grid.size())) {
            grid[gridIdx] = pointIdx;
        }
    }

    /**
     * Try to add more points around existing ones
     * @return True if there are more points to process
     */
    bool step() {
        if (queue.empty()) return false;

        // Pick a random point from the queue
        size_t idx = static_cast<size_t>(utils::Random::floatVal() * queue.size());
        const Point& point = points[queue[idx]];
        bool found = false;

        // Try to place new points around it
        for (int i = 0; i < 50; ++i) {
            double r = dist * (1 + 0.1 * utils::Random::floatVal());
            double angle = 2 * M_PI * utils::Random::floatVal();
            Point candidate(
                point.x + r * std::cos(angle),
                point.y + r * std::sin(angle)
            );

            // Wrap around (toroidal)
            warp(candidate);

            if (validate(candidate)) {
                found = true;
                emit(candidate);
            }
        }

        if (!found) {
            // Remove point from queue if no new points were placed
            queue.erase(queue.begin() + idx);
        }

        return !queue.empty();
    }

    /**
     * Wrap point coordinates to stay within bounds (toroidal)
     */
    void warp(Point& point) const {
        if (point.x < 0) {
            point.x += width;
        } else if (point.x >= width) {
            point.x -= width;
        }

        if (point.y < 0) {
            point.y += height;
        } else if (point.y >= height) {
            point.y -= height;
        }
    }

    /**
     * Check if a point is valid (far enough from other points)
     */
    bool validate(const Point& point) const {
        double px = point.x;
        double py = point.y;
        int cellX = static_cast<int>(px / cellSize);
        int cellY = static_cast<int>(py / cellSize);

        // Check neighboring cells
        for (int dy = -2; dy <= 2; ++dy) {
            int gridY = ((cellY + dy) + gridHeight) % gridHeight;
            for (int dx = -2; dx <= 2; ++dx) {
                int gridX = ((cellX + dx) + gridWidth) % gridWidth;
                int neighborIdx = grid[gridY * gridWidth + gridX];

                if (neighborIdx >= 0) {
                    const Point& neighbor = points[neighborIdx];
                    // Check distance considering wrapping
                    double distX = std::abs(neighbor.x - px);
                    double distY = std::abs(neighbor.y - py);

                    // Handle wrapping
                    if (distX > width / 2) distX = width - distX;
                    if (distY > height / 2) distY = height - distY;

                    if (distX * distX + distY * distY < dist2) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    /**
     * Add random displacement to make pattern less regular
     * @param amount Displacement amount (0-1)
     */
    void uneven(double amount) {
        double maxOffset = dist * amount * 0.5;
        for (auto& point : points) {
            point.x += (utils::Random::floatVal() * 2 - 1) * maxOffset;
            point.y += (utils::Random::floatVal() * 2 - 1) * maxOffset;
            warp(point);
        }
    }

    /**
     * Fill a polygon with points from this pattern
     * @param shape Fillable polygon
     * @return Points inside the polygon
     */
    std::vector<Point> fill(const FillablePoly& shape) const {
        std::vector<Point> result;

        for (const auto& point : points) {
            // Offset point to shape bounds
            double x = shape.x + std::fmod(point.x, width);
            double y = shape.y + std::fmod(point.y, height);
            Point translated(x, y);

            if (shape.containsPoint(translated)) {
                result.push_back(translated);
            }
        }

        return result;
    }

    /**
     * Get all generated points
     */
    const std::vector<Point>& getPoints() const {
        return points;
    }
};

} // namespace geom
} // namespace town_generator
