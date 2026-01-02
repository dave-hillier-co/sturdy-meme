#pragma once

#include "../geom/Polygon.h"
#include "../geom/Point.h"
#include "../geom/GeomUtils.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <optional>

namespace towngenerator {
namespace building {

using geom::Point;
using geom::Polygon;
using geom::GeomUtils;

/**
 * Polygon subdivision utilities for building lot division
 * Ported from Haxe Cutter class
 */
class Cutter {
public:
    /**
     * Bisect - Divides a polygon into two parts
     *
     * @param polygon The polygon to divide
     * @param vertex Starting vertex index for the cut edge
     * @param ratio Position along the edge (0.0 to 1.0)
     * @param rotation Rotation angle for cut direction (radians)
     * @param gap Distance to offset the two resulting polygons
     * @return Array of 2 polygons
     */
    static std::vector<Polygon> bisect(
        const Polygon& polygon,
        size_t vertex,
        float ratio,
        float rotation,
        float gap
    ) {
        if (polygon.vertices.size() < 3 || vertex >= polygon.vertices.size()) {
            return {polygon};
        }

        // Get the edge starting at vertex
        size_t nextVertex = (vertex + 1) % polygon.vertices.size();
        Point p0 = polygon.vertices[vertex];
        Point p1 = polygon.vertices[nextVertex];

        // Create cut point at ratio along the edge
        Point cutPoint = GeomUtils::interpolate(p0, p1, ratio);

        // Get the edge direction and rotate it
        Point edgeDir = p1.subtract(p0).normalize();
        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);
        Point cutDir(
            edgeDir.x * cosR - edgeDir.y * sinR,
            edgeDir.x * sinR + edgeDir.y * cosR
        );

        // Find where the cut line intersects the polygon
        std::vector<std::pair<size_t, Point>> intersections;

        for (size_t i = 0; i < polygon.vertices.size(); ++i) {
            size_t j = (i + 1) % polygon.vertices.size();

            // Skip the edge we're starting from
            if (i == vertex) continue;

            Point v0 = polygon.vertices[i];
            Point v1 = polygon.vertices[j];
            Point edgeDirection = v1.subtract(v0);

            auto result = GeomUtils::intersectLines(cutPoint, cutDir, v0, edgeDirection);

            if (result.has_value()) {
                float t1 = result->x;  // Parameter along cut line
                float t2 = result->y;  // Parameter along polygon edge

                // Check if intersection is within the edge (0 to 1) and in front of cut
                if (t2 >= 0.0f && t2 <= 1.0f && t1 > 0.01f) {
                    Point intersection = v0.add(edgeDirection.scale(t2));
                    intersections.push_back({i, intersection});
                    break;  // Only need first intersection
                }
            }
        }

        if (intersections.empty()) {
            return {polygon};
        }

        // Build the two polygons
        size_t cutEdge = intersections[0].first;
        Point intersection = intersections[0].second;

        // First polygon: from cutPoint to intersection, then following edges
        std::vector<Point> verts1;
        verts1.push_back(cutPoint);

        // Add vertices from nextVertex to cutEdge (inclusive)
        size_t idx = nextVertex;
        while (idx != cutEdge) {
            verts1.push_back(polygon.vertices[idx]);
            idx = (idx + 1) % polygon.vertices.size();
        }
        verts1.push_back(polygon.vertices[cutEdge]);
        verts1.push_back(intersection);

        // Second polygon: from intersection back to cutPoint
        std::vector<Point> verts2;
        verts2.push_back(intersection);

        idx = (cutEdge + 1) % polygon.vertices.size();
        while (idx != vertex) {
            verts2.push_back(polygon.vertices[idx]);
            idx = (idx + 1) % polygon.vertices.size();
        }
        verts2.push_back(polygon.vertices[vertex]);
        verts2.push_back(cutPoint);

        Polygon poly1(verts1);
        Polygon poly2(verts2);

        // Apply gap offset if specified
        if (gap > 0.0f) {
            // Calculate perpendicular direction for offset
            Point perpDir = cutDir.rotate90();

            poly1.offset(-perpDir.x * gap * 0.5f, -perpDir.y * gap * 0.5f);
            poly2.offset(perpDir.x * gap * 0.5f, perpDir.y * gap * 0.5f);
        }

        return {poly1, poly2};
    }

    /**
     * Radial - Divides polygon into triangular sectors from a center point
     *
     * @param polygon The polygon to divide
     * @param center Center point (if null, uses centroid)
     * @param gap Distance to offset sectors
     * @return Array of triangular polygons
     */
    static std::vector<Polygon> radial(
        const Polygon& polygon,
        std::optional<Point> center,
        float gap
    ) {
        if (polygon.vertices.size() < 3) {
            return {polygon};
        }

        // Use centroid if center not provided
        Point centerPoint = center.value_or(polygon.centroid());

        std::vector<Polygon> result;
        result.reserve(polygon.vertices.size());

        // Create triangle for each edge
        for (size_t i = 0; i < polygon.vertices.size(); ++i) {
            size_t j = (i + 1) % polygon.vertices.size();

            std::vector<Point> triangle = {
                centerPoint,
                polygon.vertices[i],
                polygon.vertices[j]
            };

            Polygon sector(triangle);

            // Apply gap by shrinking uniformly
            if (gap > 0.0f) {
                sector = sector.shrinkEq(gap);
            }

            // Only add if still valid after shrinking
            if (sector.vertices.size() >= 3) {
                result.push_back(sector);
            }
        }

        return result;
    }

    /**
     * SemiRadial - Like radial but uses the polygon vertex closest to centroid
     *
     * @param polygon The polygon to divide
     * @param center Ignored (kept for API compatibility)
     * @param gap Distance to offset sectors
     * @return Array of polygons
     */
    static std::vector<Polygon> semiRadial(
        const Polygon& polygon,
        std::optional<Point> center,
        float gap
    ) {
        if (polygon.vertices.size() < 3) {
            return {polygon};
        }

        // Find vertex closest to centroid
        Point centroidPoint = polygon.centroid();
        float minDist = std::numeric_limits<float>::max();
        size_t closestVertex = 0;

        for (size_t i = 0; i < polygon.vertices.size(); ++i) {
            float dist = Point::distance(polygon.vertices[i], centroidPoint);
            if (dist < minDist) {
                minDist = dist;
                closestVertex = i;
            }
        }

        // Use that vertex as center
        Point centerPoint = polygon.vertices[closestVertex];

        std::vector<Polygon> result;
        result.reserve(polygon.vertices.size() - 1);

        // Create triangles from center vertex to all non-adjacent edges
        for (size_t i = 0; i < polygon.vertices.size(); ++i) {
            // Skip the center vertex itself and its adjacent edges
            if (i == closestVertex) continue;

            size_t j = (i + 1) % polygon.vertices.size();

            // Skip edges adjacent to center vertex
            if (j == closestVertex) continue;

            std::vector<Point> triangle = {
                centerPoint,
                polygon.vertices[i],
                polygon.vertices[j]
            };

            Polygon sector(triangle);

            // Apply gap by shrinking uniformly
            if (gap > 0.0f) {
                sector = sector.shrinkEq(gap);
            }

            // Only add if still valid after shrinking
            if (sector.vertices.size() >= 3) {
                result.push_back(sector);
            }
        }

        return result;
    }

    /**
     * Ring - Creates concentric strips by cutting parallel to edges
     *
     * @param polygon The polygon to divide
     * @param depth Distance to inset for each ring
     * @return Array of strip polygons
     */
    static std::vector<Polygon> ring(const Polygon& polygon, float depth) {
        if (polygon.vertices.size() < 3 || depth <= 0.0f) {
            return {polygon};
        }

        std::vector<Polygon> result;
        Polygon current = polygon;

        const float minArea = depth * depth;  // Stop when too small
        const int maxIterations = 100;  // Prevent infinite loops
        int iterations = 0;

        while (current.vertices.size() >= 3 &&
               current.square() > minArea &&
               iterations < maxIterations) {

            // Create inset polygon
            Polygon inset = current.shrinkEq(depth);

            // Check if inset is valid
            if (inset.vertices.size() < 3 || inset.square() < minArea) {
                // Add remaining center as final piece
                result.push_back(current);
                break;
            }

            // Create ring strip (difference between current and inset)
            // For simplicity, we'll add the current outer ring
            // In a full implementation, this would compute the actual ring geometry
            result.push_back(current);

            current = inset;
            iterations++;
        }

        // Add final center piece if it's large enough
        if (current.vertices.size() >= 3 && current.square() >= minArea) {
            result.push_back(current);
        }

        return result;
    }

private:
    // Helper to compute actual ring strip between two polygons
    // This is a simplified version - a full implementation would need
    // robust polygon boolean operations
    static Polygon computeRingStrip(const Polygon& outer, const Polygon& inner) {
        // For now, just return outer
        // Full implementation would compute outer - inner using clipper library
        return outer;
    }
};

} // namespace building
} // namespace towngenerator
