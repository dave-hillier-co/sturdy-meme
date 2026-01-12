#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/GeomUtils.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace town_generator {
namespace geom {

/**
 * PolyBool - Boolean operations on polygons
 *
 * Provides polygon intersection (AND) operations.
 *
 * Faithful port from mfcg-clean/geometry/PolyBool.js
 */
class PolyBool {
public:
    static constexpr double EPSILON = 0.0001;

    /**
     * Check if two points are approximately equal
     */
    static bool pointsEqual(const Point& a, const Point& b, double epsilon = EPSILON) {
        return std::abs(a.x - b.x) < epsilon && std::abs(a.y - b.y) < epsilon;
    }

    /**
     * Find index of a point in polygon (with tolerance)
     * @return Index or -1 if not found
     */
    static int findPointIndex(const std::vector<Point>& poly, const Point& point) {
        for (size_t i = 0; i < poly.size(); ++i) {
            if (pointsEqual(poly[i], point)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /**
     * Check if point is inside polygon
     */
    static bool containsPoint(const std::vector<Point>& poly, const Point& point) {
        bool inside = false;
        size_t n = poly.size();

        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Point& pi = poly[i];
            const Point& pj = poly[j];

            if ((pi.y > point.y) != (pj.y > point.y) &&
                point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x) {
                inside = !inside;
            }
        }

        return inside;
    }

    /**
     * Augment two polygons by finding all intersection points
     * and inserting them into both polygons
     *
     * @param polyA First polygon
     * @param polyB Second polygon
     * @return Pair of augmented polygons with intersection points
     */
    static std::pair<std::vector<Point>, std::vector<Point>> augmentPolygons(
        const std::vector<Point>& polyA,
        const std::vector<Point>& polyB
    ) {
        size_t lenA = polyA.size();
        size_t lenB = polyB.size();

        // Arrays to store intersection points for each edge
        struct Intersection {
            double a;  // Parameter along edge A
            double b;  // Parameter along edge B
            Point p;   // Intersection point
        };

        std::vector<std::vector<Intersection>> intersectionsA(lenA);
        std::vector<std::vector<Intersection>> intersectionsB(lenB);

        // Find all intersections between edges
        for (size_t i = 0; i < lenA; ++i) {
            const Point& a1 = polyA[i];
            const Point& a2 = polyA[(i + 1) % lenA];
            double ax = a1.x;
            double ay = a1.y;
            double adx = a2.x - ax;
            double ady = a2.y - ay;

            for (size_t j = 0; j < lenB; ++j) {
                const Point& b1 = polyB[j];
                const Point& b2 = polyB[(j + 1) % lenB];
                double bx = b1.x;
                double by = b1.y;

                auto result = GeomUtils::intersectLines(
                    ax, ay, adx, ady,
                    bx, by, b2.x - bx, b2.y - by
                );

                if (result.has_value() &&
                    result->x >= 0 && result->x <= 1 &&
                    result->y >= 0 && result->y <= 1) {
                    // Found valid intersection
                    Point point = GeomUtils::lerp(a1, a2, result->x);
                    Intersection inter{result->x, result->y, point};
                    intersectionsA[i].push_back(inter);
                    intersectionsB[j].push_back(inter);
                }
            }
        }

        // Build augmented polygon A
        std::vector<Point> augmentedA;
        for (size_t i = 0; i < lenA; ++i) {
            augmentedA.push_back(polyA[i]);
            auto& edgeIntersections = intersectionsA[i];
            if (!edgeIntersections.empty()) {
                // Sort by parameter along edge
                std::sort(edgeIntersections.begin(), edgeIntersections.end(),
                    [](const Intersection& x, const Intersection& y) {
                        return x.a < y.a;
                    });
                for (const auto& inter : edgeIntersections) {
                    augmentedA.push_back(inter.p);
                }
            }
        }

        // Build augmented polygon B
        std::vector<Point> augmentedB;
        for (size_t i = 0; i < lenB; ++i) {
            augmentedB.push_back(polyB[i]);
            auto& edgeIntersections = intersectionsB[i];
            if (!edgeIntersections.empty()) {
                // Sort by parameter along edge
                std::sort(edgeIntersections.begin(), edgeIntersections.end(),
                    [](const Intersection& x, const Intersection& y) {
                        return x.b < y.b;
                    });
                for (const auto& inter : edgeIntersections) {
                    augmentedB.push_back(inter.p);
                }
            }
        }

        return {augmentedA, augmentedB};
    }

    /**
     * Compute intersection (AND) of two polygons
     *
     * @param polyA First polygon
     * @param polyB Second polygon
     * @param returnA If no intersection, return polyA instead of polyB
     * @return Intersection polygon, or empty if no intersection
     */
    static std::vector<Point> intersect(
        const std::vector<Point>& polyA,
        const std::vector<Point>& polyB,
        bool returnA = false
    ) {
        auto [augA, augB] = augmentPolygons(polyA, polyB);

        // If no new points were added, check containment
        if (augA.size() == polyA.size()) {
            // No intersections - check if one contains the other
            if (containsPoint(polyA, polyB[0])) {
                return returnA ? polyA : polyB;
            }
            if (containsPoint(polyB, polyA[0])) {
                return returnA ? std::vector<Point>{} : polyA;
            }
            return returnA ? polyA : std::vector<Point>{};
        }

        // Start from a vertex that was added (intersection point)
        std::vector<Point>* currentPoly = &augA;
        std::vector<Point>* otherPoly = &augB;
        int startIdx = -1;
        Point startPoint;

        for (size_t i = 0; i < augA.size(); ++i) {
            // Check if this point was in the original polygon
            bool found = false;
            for (const auto& origPoint : polyA) {
                if (pointsEqual(augA[i], origPoint)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // This point is an intersection
                startIdx = static_cast<int>(i);
                startPoint = augA[i];
                break;
            }
        }

        if (startIdx == -1) {
            return returnA ? polyA : std::vector<Point>{};
        }

        // Check which polygon to trace first
        size_t nextIdx = (startIdx + 1) % augA.size();
        Point testPoint = GeomUtils::lerp(startPoint, augA[nextIdx], 0.5);
        if (!containsPoint(polyB, testPoint)) {
            currentPoly = &augB;
            otherPoly = &augA;
            startIdx = findPointIndex(augB, startPoint);
        }

        // Trace the intersection boundary
        std::vector<Point> result;
        int idx = startIdx;

        while (true) {
            result.push_back((*currentPoly)[idx]);

            int nextIndex = (idx + 1) % static_cast<int>(currentPoly->size());
            const Point& nextPoint = (*currentPoly)[nextIndex];

            // Check if we've completed the loop
            if (!result.empty() && (nextPoint == result[0] || pointsEqual(nextPoint, result[0]))) {
                return result;
            }

            // Check if next point is in other polygon (switch polygons)
            int otherIdx = findPointIndex(*otherPoly, nextPoint);
            if (otherIdx != -1) {
                // Switch polygons
                idx = otherIdx;
                std::swap(currentPoly, otherPoly);
            } else {
                idx = nextIndex;
            }

            // Safety check
            if (result.size() > augA.size() + augB.size()) {
                break;
            }
        }

        return result;
    }

    /**
     * Alias for intersect() - polygon AND operation
     */
    static std::vector<Point> polygonAnd(
        const std::vector<Point>& polyA,
        const std::vector<Point>& polyB,
        bool returnA = false
    ) {
        return intersect(polyA, polyB, returnA);
    }
};

} // namespace geom
} // namespace town_generator
