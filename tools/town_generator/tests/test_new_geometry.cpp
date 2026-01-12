/**
 * Test compilation of new geometry headers
 */

#include "town_generator/geom/Chaikin.h"
#include "town_generator/geom/PoissonPattern.h"
#include "town_generator/geom/DCEL.h"
#include "town_generator/geom/PolyBool.h"
#include "town_generator/geom/SkeletonBuilder.h"
#include <iostream>
#include <vector>
#include <cassert>

using namespace town_generator::geom;
using namespace town_generator::utils;

void testChaikin() {
    std::vector<Point> square = {
        Point(0, 0),
        Point(10, 0),
        Point(10, 10),
        Point(0, 10)
    };

    auto smoothed = Chaikin::smoothClosed(square, 2);
    assert(smoothed.size() > square.size());
    std::cout << "Chaikin: Smoothed " << square.size() << " points to " << smoothed.size() << " points\n";

    // Test open curve
    std::vector<Point> line = {Point(0, 0), Point(5, 5), Point(10, 0)};
    auto smoothedLine = Chaikin::smoothOpen(line, 2);
    assert(smoothedLine.size() > line.size());
    std::cout << "Chaikin: Smoothed open line from " << line.size() << " to " << smoothedLine.size() << " points\n";
}

void testPoissonPattern() {
    Random::reset(12345);

    PoissonPattern pattern(100, 100, 10, 0);
    const auto& points = pattern.getPoints();

    assert(points.size() > 0);
    std::cout << "PoissonPattern: Generated " << points.size() << " points\n";

    // Verify minimum distance
    bool allValid = true;
    for (size_t i = 0; i < points.size() && allValid; ++i) {
        for (size_t j = i + 1; j < points.size(); ++j) {
            double dist = Point::distance(points[i], points[j]);
            // Allow for toroidal wrapping
            double wrapDistX = std::min(
                std::abs(points[i].x - points[j].x),
                100 - std::abs(points[i].x - points[j].x)
            );
            double wrapDistY = std::min(
                std::abs(points[i].y - points[j].y),
                100 - std::abs(points[i].y - points[j].y)
            );
            double wrapDist = std::sqrt(wrapDistX * wrapDistX + wrapDistY * wrapDistY);
            if (std::min(dist, wrapDist) < 9.9) {
                allValid = false;
            }
        }
    }
    assert(allValid);
    std::cout << "PoissonPattern: All points maintain minimum distance\n";
}

void testDCEL() {
    // Create a simple DCEL from two adjacent triangles
    std::vector<std::vector<Point>> polygons = {
        {Point(0, 0), Point(10, 0), Point(5, 10)},
        {Point(10, 0), Point(15, 10), Point(5, 10)}
    };

    DCEL dcel(polygons);

    assert(dcel.faces.size() == 2);
    assert(dcel.edges.size() == 6);  // 3 edges per triangle
    std::cout << "DCEL: Created with " << dcel.faces.size() << " faces and " << dcel.edges.size() << " edges\n";

    // Test face polygon extraction
    auto poly = dcel.faces[0]->getPoly();
    assert(poly.size() == 3);
    std::cout << "DCEL: Face 0 has " << poly.size() << " vertices\n";
}

void testPolyBool() {
    // Create two overlapping squares
    std::vector<Point> square1 = {
        Point(0, 0),
        Point(10, 0),
        Point(10, 10),
        Point(0, 10)
    };

    std::vector<Point> square2 = {
        Point(5, 5),
        Point(15, 5),
        Point(15, 15),
        Point(5, 15)
    };

    auto intersection = PolyBool::intersect(square1, square2);
    assert(intersection.size() >= 4);  // Should have at least 4 vertices
    std::cout << "PolyBool: Intersection has " << intersection.size() << " vertices\n";

    // Test containment
    assert(PolyBool::containsPoint(square1, Point(5, 5)));
    assert(!PolyBool::containsPoint(square1, Point(15, 15)));
    std::cout << "PolyBool: Point containment tests passed\n";
}

void testSkeletonBuilder() {
    // Create a simple rectangle
    std::vector<Point> rect = {
        Point(0, 0),
        Point(20, 0),
        Point(20, 10),
        Point(0, 10)
    };

    SkeletonBuilder skeleton(rect, true);

    // Should have some bones
    assert(skeleton.bones.size() > 0);
    std::cout << "SkeletonBuilder: Generated " << skeleton.bones.size() << " skeleton bones\n";

    // Get skeleton edges
    auto edges = skeleton.getSkeletonEdges();
    std::cout << "SkeletonBuilder: " << edges.size() << " skeleton edges\n";
}

int main() {
    std::cout << "=== Testing New Geometry Classes ===\n\n";

    testChaikin();
    std::cout << "\n";

    testPoissonPattern();
    std::cout << "\n";

    testDCEL();
    std::cout << "\n";

    testPolyBool();
    std::cout << "\n";

    testSkeletonBuilder();
    std::cout << "\n";

    std::cout << "=== All Tests Passed ===\n";
    return 0;
}
