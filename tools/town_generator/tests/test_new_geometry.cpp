/**
 * Test compilation of new geometry and building headers
 */

#include "town_generator/geom/Chaikin.h"
#include "town_generator/geom/PoissonPattern.h"
#include "town_generator/geom/DCEL.h"
#include "town_generator/geom/PolyBool.h"
#include "town_generator/geom/SkeletonBuilder.h"
#include "town_generator/building/Blueprint.h"
#include "town_generator/utils/Bloater.h"
#include "town_generator/utils/PathTracker.h"
#include <iostream>
#include <vector>
#include <cassert>

using namespace town_generator::geom;
using namespace town_generator::utils;
using namespace town_generator::building;

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

void testBlueprint() {
    // Test basic construction
    Blueprint bp(50, 12345);
    assert(bp.size == 50);
    assert(bp.seed == 12345);
    assert(bp.citadel == true);  // Default
    assert(bp.walls == true);    // Default
    std::cout << "Blueprint: Basic construction passed\n";

    // Test randomized creation
    Random::reset(12345);
    Blueprint randomBp = Blueprint::create(50, 12345);
    assert(randomBp.size == 50);
    assert(randomBp.random == true);
    std::cout << "Blueprint: Randomized creation passed\n";

    // Test clone
    Blueprint cloned = randomBp.clone();
    assert(cloned.size == randomBp.size);
    assert(cloned.seed == randomBp.seed);
    assert(cloned.walls == randomBp.walls);
    std::cout << "Blueprint: Clone passed\n";

    // Test population estimate
    int pop = bp.estimatePopulation();
    assert(pop > 0);
    std::cout << "Blueprint: Population estimate = " << pop << "\n";
}

void testBloater() {
    // Create a simple square
    std::vector<Point> square = {
        Point(0, 0),
        Point(10, 0),
        Point(10, 10),
        Point(0, 10)
    };

    // Test bloat - adds intermediate points
    auto bloated = Bloater::bloat(square, 2.0);
    assert(bloated.size() >= square.size());
    std::cout << "Bloater: Bloated from " << square.size() << " to " << bloated.size() << " points\n";

    // Test bloatSmooth
    auto smoothBloated = Bloater::bloatSmooth(square, 1.0, 3);
    assert(smoothBloated.size() > square.size());
    std::cout << "Bloater: Smooth bloated to " << smoothBloated.size() << " points\n";

    // Test offset (inflate)
    auto inflated = Bloater::inflate(square, 1.0);
    assert(inflated.size() == square.size());
    // Verify points moved (either inward or outward depending on winding)
    bool pointsMoved = false;
    for (size_t i = 0; i < square.size(); ++i) {
        double dist = Point::distance(square[i], inflated[i]);
        if (dist > 0.5) {
            pointsMoved = true;
            break;
        }
    }
    assert(pointsMoved);
    std::cout << "Bloater: Inflate moved points\n";

    // Test deflate
    auto deflated = Bloater::deflate(square, 1.0);
    assert(deflated.size() == square.size());
    std::cout << "Bloater: Deflate passed\n";
}

void testPathTracker() {
    // Create a simple path: L-shape
    std::vector<Point> path = {
        Point(0, 0),
        Point(10, 0),
        Point(10, 10)
    };

    PathTracker tracker(path);

    // Test total length
    double length = tracker.getTotalLength();
    assert(std::abs(length - 20.0) < 0.001);  // 10 + 10
    std::cout << "PathTracker: Total length = " << length << "\n";

    // Test getPos at start
    auto pos0 = tracker.getPos(0);
    assert(pos0.has_value());
    assert(std::abs(pos0->x - 0) < 0.001);
    assert(std::abs(pos0->y - 0) < 0.001);
    std::cout << "PathTracker: Position at 0 = (" << pos0->x << ", " << pos0->y << ")\n";

    // Test getPos at midpoint of first segment
    auto pos5 = tracker.getPos(5);
    assert(pos5.has_value());
    assert(std::abs(pos5->x - 5) < 0.001);
    assert(std::abs(pos5->y - 0) < 0.001);
    std::cout << "PathTracker: Position at 5 = (" << pos5->x << ", " << pos5->y << ")\n";

    // Test getPos at corner
    auto pos10 = tracker.getPos(10);
    assert(pos10.has_value());
    assert(std::abs(pos10->x - 10) < 0.001);
    assert(std::abs(pos10->y - 0) < 0.001);
    std::cout << "PathTracker: Position at 10 = (" << pos10->x << ", " << pos10->y << ")\n";

    // Test getPos on second segment
    auto pos15 = tracker.getPos(15);
    assert(pos15.has_value());
    assert(std::abs(pos15->x - 10) < 0.001);
    assert(std::abs(pos15->y - 5) < 0.001);
    std::cout << "PathTracker: Position at 15 = (" << pos15->x << ", " << pos15->y << ")\n";

    // Test getPos past end
    auto posPastEnd = tracker.getPos(25);
    assert(!posPastEnd.has_value());
    std::cout << "PathTracker: Position past end returns nullopt\n";

    // Test sample
    auto samples = tracker.sample(5);
    assert(samples.size() == 5);
    std::cout << "PathTracker: Sampled " << samples.size() << " points\n";

    // Test sampleSpaced
    auto spaced = tracker.sampleSpaced(5.0);
    assert(spaced.size() >= 4);  // Should have at least 4 points (0, 5, 10, 15)
    std::cout << "PathTracker: Spaced sample has " << spaced.size() << " points\n";

    // Test tangent
    tracker.reset();
    tracker.getPos(5);  // Move to middle of first segment
    auto tangent = tracker.getTangentNormalized();
    assert(std::abs(tangent.x - 1) < 0.001);  // Should be (1, 0)
    assert(std::abs(tangent.y - 0) < 0.001);
    std::cout << "PathTracker: Tangent at 5 = (" << tangent.x << ", " << tangent.y << ")\n";

    // Test normal
    auto normal = tracker.getNormal();
    assert(std::abs(normal.x - 0) < 0.001);  // Should be (0, 1)
    assert(std::abs(normal.y - 1) < 0.001);
    std::cout << "PathTracker: Normal at 5 = (" << normal.x << ", " << normal.y << ")\n";
}

int main() {
    std::cout << "=== Testing New Geometry and Building Classes ===\n\n";

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

    testBlueprint();
    std::cout << "\n";

    testBloater();
    std::cout << "\n";

    testPathTracker();
    std::cout << "\n";

    std::cout << "=== All Tests Passed ===\n";
    return 0;
}
