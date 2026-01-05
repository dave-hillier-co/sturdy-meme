#include "town_generator/geom/GeomUtils.h"
#include <limits>
#include <algorithm>

namespace town_generator {
namespace geom {

std::vector<Point> GeomUtils::rotatePoints(const std::vector<Point>& pts, double angle) {
    std::vector<Point> result;
    result.reserve(pts.size());
    double cosA = std::cos(angle);
    double sinA = std::sin(angle);
    for (const auto& p : pts) {
        result.push_back(Point(p.x * cosA - p.y * sinA, p.x * sinA + p.y * cosA));
    }
    return result;
}

double GeomUtils::polygonArea(const std::vector<Point>& poly) {
    if (poly.size() < 3) return 0;
    double area = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point& p1 = poly[i];
        const Point& p2 = poly[(i + 1) % poly.size()];
        area += p1.x * p2.y - p2.x * p1.y;
    }
    return std::abs(area * 0.5);
}

std::vector<Point> GeomUtils::lir(const std::vector<Point>& poly, size_t edgeIdx) {
    // Simplified LIR algorithm - finds largest inscribed rectangle aligned to edge
    // This is a simplified version of mfcg.js Gb.lir

    if (poly.size() < 3 || edgeIdx >= poly.size()) {
        return poly;
    }

    size_t n = poly.size();
    size_t nextIdx = (edgeIdx + 1) % n;

    // Get the edge direction
    Point edge = poly[nextIdx].subtract(poly[edgeIdx]);
    double edgeLen = edge.length();
    if (edgeLen < 0.0001) return poly;

    // Compute rotation angle to align edge with x-axis
    double angle = std::atan2(edge.y, edge.x);

    // Rotate all points so edge is horizontal
    std::vector<Point> rotated = rotatePoints(poly, -angle);

    // Find bounding box of rotated polygon
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto& p : rotated) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    // The edge should be at y = rotated[edgeIdx].y
    double baseY = rotated[edgeIdx].y;
    double baseX1 = rotated[edgeIdx].x;
    double baseX2 = rotated[nextIdx].x;
    if (baseX1 > baseX2) std::swap(baseX1, baseX2);

    // Find rectangle extending from the edge inward
    // Scan through possible heights
    double bestArea = 0;
    double bestLeft = baseX1, bestRight = baseX2, bestTop = baseY, bestBottom = baseY;

    // Simple approach: use edge as base, find max height while staying inside polygon
    // This is a simplification - the full algorithm is more complex

    // Determine which direction is "inside" the polygon
    Point edgeMid((baseX1 + baseX2) / 2, baseY);
    double testOffset = (maxY - minY) * 0.01;

    // Test both directions to find inside
    double insideY = baseY + testOffset;
    if (insideY > maxY || insideY < minY) {
        insideY = baseY - testOffset;
    }

    // Sample heights to find max inscribed rectangle
    int samples = 10;
    for (int s = 1; s <= samples; ++s) {
        double t = static_cast<double>(s) / samples;
        double testY;
        if (insideY > baseY) {
            testY = baseY + t * (maxY - baseY);
        } else {
            testY = baseY - t * (baseY - minY);
        }

        // Find x-bounds at this y level by intersecting with polygon edges
        double leftBound = minX;
        double rightBound = maxX;

        for (size_t i = 0; i < n; ++i) {
            const Point& p1 = rotated[i];
            const Point& p2 = rotated[(i + 1) % n];

            // Check if this edge crosses the y level
            if ((p1.y <= testY && p2.y > testY) || (p2.y <= testY && p1.y > testY)) {
                // Find x at intersection
                double intersectX = p1.x + (testY - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);

                // Determine if this is left or right bound
                if (intersectX < edgeMid.x) {
                    leftBound = std::max(leftBound, intersectX);
                } else {
                    rightBound = std::min(rightBound, intersectX);
                }
            }
        }

        // Clamp to base edge bounds
        leftBound = std::max(leftBound, baseX1);
        rightBound = std::min(rightBound, baseX2);

        double width = rightBound - leftBound;
        double height = std::abs(testY - baseY);
        double area = width * height;

        if (area > bestArea && width > 0 && height > 0) {
            bestArea = area;
            bestLeft = leftBound;
            bestRight = rightBound;
            if (insideY > baseY) {
                bestBottom = baseY;
                bestTop = testY;
            } else {
                bestTop = baseY;
                bestBottom = testY;
            }
        }
    }

    // Create rectangle corners (in rotated space)
    std::vector<Point> rectRotated = {
        Point(bestLeft, bestBottom),
        Point(bestRight, bestBottom),
        Point(bestRight, bestTop),
        Point(bestLeft, bestTop)
    };

    // Rotate back
    return rotatePoints(rectRotated, angle);
}

std::vector<Point> GeomUtils::lira(const std::vector<Point>& poly) {
    // Try lir for each edge and return the one with largest area
    if (poly.size() < 3) return poly;

    double bestArea = -1;
    std::vector<Point> bestRect;

    for (size_t i = 0; i < poly.size(); ++i) {
        std::vector<Point> rect = lir(poly, i);
        double area = polygonArea(rect);
        if (area > bestArea) {
            bestArea = area;
            bestRect = rect;
        }
    }

    return bestRect.empty() ? poly : bestRect;
}

} // namespace geom
} // namespace town_generator
