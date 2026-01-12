#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/GeomUtils.h"
#include <vector>
#include <unordered_set>
#include <cmath>

namespace town_generator {
namespace geom {

/**
 * Chaikin - Chaikin curve subdivision algorithm
 *
 * Implements Chaikin's corner-cutting algorithm for curve smoothing.
 * Used for smoothing roads, walls, coastlines, etc.
 *
 * Faithful port from mfcg-clean/geometry/Chaikin.js
 */
class Chaikin {
public:
    /**
     * Apply Chaikin subdivision to a polygon or polyline
     * @param points Input points
     * @param closed Whether the curve is closed
     * @param iterations Number of subdivision iterations
     * @param anchors Points that should not be moved
     * @return Smoothed points
     */
    static std::vector<Point> render(
        const std::vector<Point>& points,
        bool closed = true,
        int iterations = 2,
        const std::vector<Point>& anchors = {}
    ) {
        if (points.size() < 2) {
            return points;
        }

        // Build anchor set for fast lookup
        std::unordered_set<size_t> anchorIndices;
        for (size_t i = 0; i < points.size(); ++i) {
            for (const auto& anchor : anchors) {
                if (points[i].equals(anchor, 0.0001)) {
                    anchorIndices.insert(i);
                    break;
                }
            }
        }

        std::vector<Point> result = points;

        for (int iter = 0; iter < iterations; ++iter) {
            std::vector<Point> newPoints;
            size_t n = result.size();

            // Rebuild anchor indices for new result
            std::unordered_set<size_t> currentAnchors;
            for (size_t i = 0; i < n; ++i) {
                for (const auto& anchor : anchors) {
                    if (result[i].equals(anchor, 0.0001)) {
                        currentAnchors.insert(i);
                        break;
                    }
                }
            }

            for (size_t i = 0; i < n; ++i) {
                const Point& curr = result[i];
                const Point& next = result[(i + 1) % n];

                // Don't process last edge if not closed
                if (!closed && i == n - 1) {
                    break;
                }

                bool currIsAnchor = currentAnchors.count(i) > 0;
                bool nextIsAnchor = currentAnchors.count((i + 1) % n) > 0;

                if (currIsAnchor) {
                    // Keep anchor points
                    newPoints.push_back(curr);
                    if (!nextIsAnchor) {
                        newPoints.push_back(GeomUtils::lerp(curr, next, 0.25));
                    }
                } else if (nextIsAnchor) {
                    // Approaching anchor
                    newPoints.push_back(GeomUtils::lerp(curr, next, 0.75));
                } else {
                    // Normal subdivision
                    newPoints.push_back(GeomUtils::lerp(curr, next, 0.25));
                    newPoints.push_back(GeomUtils::lerp(curr, next, 0.75));
                }
            }

            // Handle endpoints for open curves
            if (!closed) {
                // Preserve first point
                bool firstIsAnchor = false;
                for (const auto& anchor : anchors) {
                    if (result[0].equals(anchor, 0.0001)) {
                        firstIsAnchor = true;
                        break;
                    }
                }
                if (!firstIsAnchor) {
                    newPoints.insert(newPoints.begin(), result[0]);
                }

                // Preserve last point
                bool lastIsAnchor = false;
                for (const auto& anchor : anchors) {
                    if (result[n - 1].equals(anchor, 0.0001)) {
                        lastIsAnchor = true;
                        break;
                    }
                }
                if (!lastIsAnchor) {
                    newPoints.push_back(result[n - 1]);
                }
            }

            result = std::move(newPoints);
        }

        return result;
    }

    /**
     * Smooth an open polyline
     * @param points Input points
     * @param iterations Number of iterations
     * @return Smoothed points
     */
    static std::vector<Point> smoothOpen(const std::vector<Point>& points, int iterations = 2) {
        return render(points, false, iterations, {});
    }

    /**
     * Smooth a closed polygon
     * @param points Input points
     * @param iterations Number of iterations
     * @return Smoothed points
     */
    static std::vector<Point> smoothClosed(const std::vector<Point>& points, int iterations = 2) {
        return render(points, true, iterations, {});
    }
};

/**
 * PolygonSmoother - Additional polygon smoothing utilities
 */
class PolygonSmoother {
public:
    /**
     * Smooth polygon with per-edge control
     * @param poly Polygon points
     * @param anchors Points to keep fixed
     * @param iterations Number of iterations
     * @return Smoothed polygon
     */
    static std::vector<Point> smooth(
        const std::vector<Point>& poly,
        const std::vector<Point>& anchors,
        int iterations
    ) {
        if (poly.size() < 3) return poly;
        return Chaikin::render(poly, true, iterations, anchors);
    }

    /**
     * Smooth open polyline with endpoint anchors
     * @param points Input points
     * @param anchors Additional anchor points
     * @param iterations Number of iterations
     * @return Smoothed polyline
     */
    static std::vector<Point> smoothOpen(
        const std::vector<Point>& points,
        const std::vector<Point>& anchors,
        int iterations
    ) {
        if (points.size() < 2) return points;

        // Add endpoints as anchors
        std::vector<Point> allAnchors = anchors;

        bool hasFirst = false, hasLast = false;
        for (const auto& a : allAnchors) {
            if (a.equals(points.front(), 0.0001)) hasFirst = true;
            if (a.equals(points.back(), 0.0001)) hasLast = true;
        }

        if (!hasFirst) allAnchors.push_back(points.front());
        if (!hasLast) allAnchors.push_back(points.back());

        return Chaikin::render(points, false, iterations, allAnchors);
    }

    /**
     * Inset polygon and optionally add rounded corners
     * @param poly Polygon points
     * @param amount Inset amount
     * @return Inset polygon or empty if invalid
     */
    static std::vector<Point> inset(const std::vector<Point>& poly, double amount) {
        if (poly.size() < 3) return {};

        size_t n = poly.size();
        std::vector<Point> result;
        result.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            const Point& prev = poly[(i + n - 1) % n];
            const Point& curr = poly[i];
            const Point& next = poly[(i + 1) % n];

            // Compute edge directions
            Point d1 = curr.subtract(prev).norm();
            Point d2 = next.subtract(curr).norm();

            // Compute normals (inward)
            Point n1(-d1.y, d1.x);
            Point n2(-d2.y, d2.x);

            // Average normal for corner
            Point avgNormal = n1.add(n2).norm();

            // Compute inset distance (handle acute angles)
            double dot = d1.dot(d2);
            double scale = amount / std::max(0.5, std::sqrt((1 + dot) / 2));

            Point newPoint = curr.add(avgNormal.scale(scale));
            result.push_back(newPoint);
        }

        // Check if result is valid (positive area)
        if (polygonArea(result) <= 0) {
            return {};
        }

        return result;
    }

private:
    static double polygonArea(const std::vector<Point>& poly) {
        double area = 0;
        size_t n = poly.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            area += poly[i].x * poly[j].y;
            area -= poly[j].x * poly[i].y;
        }
        return area / 2;
    }
};

} // namespace geom
} // namespace town_generator
