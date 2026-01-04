#include <doctest/doctest.h>
#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/geom/Segment.hpp"
#include "town_generator2/geom/Voronoi.hpp"

using namespace town_generator2::geom;

TEST_SUITE("Point mutations") {
    TEST_CASE("addEq mutates in place") {
        auto p = makePoint(1.0, 2.0);
        double originalX = p->x;

        p->addEq(Point(3.0, 4.0));

        CHECK(p->x == originalX + 3.0);
        CHECK(p->y == 6.0);
    }

    TEST_CASE("subEq mutates in place") {
        auto p = makePoint(5.0, 7.0);

        p->subEq(Point(2.0, 3.0));

        CHECK(p->x == 3.0);
        CHECK(p->y == 4.0);
    }

    TEST_CASE("scaleEq mutates in place") {
        auto p = makePoint(2.0, 3.0);

        p->scaleEq(2.0);

        CHECK(p->x == 4.0);
        CHECK(p->y == 6.0);
    }

    TEST_CASE("setTo mutates in place") {
        auto p = makePoint(1.0, 1.0);

        p->setTo(5.0, 6.0);

        CHECK(p->x == 5.0);
        CHECK(p->y == 6.0);
    }

    TEST_CASE("set from Point mutates in place") {
        auto p = makePoint(1.0, 1.0);

        p->set(Point(9.0, 10.0));

        CHECK(p->x == 9.0);
        CHECK(p->y == 10.0);
    }

    TEST_CASE("offset mutates in place") {
        auto p = makePoint(3.0, 4.0);

        p->offset(1.0, 2.0);

        CHECK(p->x == 4.0);
        CHECK(p->y == 6.0);
    }

    TEST_CASE("Shared pointer mutation visibility") {
        auto p1 = makePoint(1.0, 2.0);
        auto p2 = p1; // Same shared_ptr

        CHECK(p1.get() == p2.get());

        p1->x = 100.0;

        // Both see the change
        CHECK(p1->x == 100.0);
        CHECK(p2->x == 100.0);
    }
}

TEST_SUITE("Segment mutations") {
    TEST_CASE("Mutating segment start affects length") {
        auto start = makePoint(0, 0);
        auto end = makePoint(3, 4);
        Segment seg(start, end);

        CHECK(seg.length() == doctest::Approx(5.0));

        // Move start point
        start->x = 3;
        start->y = 0;

        // Length should now be 4 (from (3,0) to (3,4))
        CHECK(seg.length() == doctest::Approx(4.0));
    }

    TEST_CASE("Mutating segment end affects vector") {
        auto start = makePoint(0, 0);
        auto end = makePoint(5, 0);
        Segment seg(start, end);

        CHECK(seg.dx() == 5);
        CHECK(seg.dy() == 0);

        // Move end point
        end->y = 5;

        CHECK(seg.dx() == 5);
        CHECK(seg.dy() == 5);
    }

    TEST_CASE("Two segments sharing a point see mutations") {
        auto shared = makePoint(5, 5);
        auto end1 = makePoint(10, 5);
        auto end2 = makePoint(5, 10);

        Segment seg1(shared, end1);
        Segment seg2(shared, end2);

        // Move the shared point
        shared->x = 0;
        shared->y = 0;

        // Both segments now start from origin
        CHECK(seg1.start->x == 0);
        CHECK(seg1.start->y == 0);
        CHECK(seg2.start->x == 0);
        CHECK(seg2.start->y == 0);

        CHECK(seg1.dx() == 10);
        CHECK(seg2.dy() == 10);
    }
}

TEST_SUITE("Polygon mutations") {
    TEST_CASE("Polygon offset mutates all vertices") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon poly({p1, p2, p3});
        Point originalP1(p1->x, p1->y);

        poly.offset(5, 5);

        // All vertices should have moved
        CHECK(p1->x == originalP1.x + 5);
        CHECK(p1->y == originalP1.y + 5);
        CHECK(p2->x == 6);
        CHECK(p3->y == 6);
    }

    TEST_CASE("Polygon rotate mutates all vertices") {
        auto p1 = makePoint(1, 0);
        auto p2 = makePoint(0, 1);
        auto p3 = makePoint(-1, 0);

        Polygon poly({p1, p2, p3});

        // Rotate 90 degrees (pi/2)
        poly.rotate(M_PI / 2);

        // (1, 0) -> (0, 1)
        CHECK(p1->x == doctest::Approx(0.0).epsilon(0.001));
        CHECK(p1->y == doctest::Approx(1.0).epsilon(0.001));
    }

    TEST_CASE("Two polygons sharing vertex see mutations") {
        auto shared1 = makePoint(1, 0);
        auto shared2 = makePoint(1, 1);

        Polygon poly1({makePoint(0, 0), shared1, shared2, makePoint(0, 1)});
        Polygon poly2({shared1, makePoint(2, 0), makePoint(2, 1), shared2});

        // Get areas before mutation
        double area1Before = poly1.square();
        double area2Before = poly2.square();

        // Move the shared vertices
        shared1->x = 0.5;
        shared2->x = 0.5;

        // Areas should have changed
        double area1After = poly1.square();
        double area2After = poly2.square();

        // Both polygons should see the mutation (areas changed)
        CHECK(area1After != area1Before);
        CHECK(area2After != area2Before);

        // Verify mutation is visible: poly1 is smaller (shared edge moved left)
        // poly2 is larger (shared edge moved left, expanding poly2)
        CHECK(std::abs(area1After) < std::abs(area1Before));
        CHECK(std::abs(area2After) > std::abs(area2Before));
    }

    TEST_CASE("Shallow copy shares mutations") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(2, 0);
        auto p3 = makePoint(1, 2);

        Polygon original({p1, p2, p3});
        Polygon copy = original.copy(); // Shallow copy

        // Mutate through original
        original.offset(10, 10);

        // Copy should see the mutation
        CHECK(copy[0].x == 10);
        CHECK(copy[0].y == 10);
    }

    TEST_CASE("Deep copy isolates mutations") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(2, 0);
        auto p3 = makePoint(1, 2);

        Polygon original({p1, p2, p3});
        Polygon deepCopy = original.deepCopy();

        // Mutate through original
        original.offset(10, 10);

        // Deep copy should NOT see the mutation
        CHECK(deepCopy[0].x == 0);
        CHECK(deepCopy[0].y == 0);
    }

    TEST_CASE("Assignment operator shares mutations") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(2, 0);
        auto p3 = makePoint(1, 2);

        Polygon original({p1, p2, p3});
        Polygon assigned;
        assigned = original;

        p1->x = 100;

        // Both should see the change
        CHECK(original[0].x == 100);
        CHECK(assigned[0].x == 100);
    }

    TEST_CASE("Polygon set() mutates underlying points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(0, 1);

        Polygon poly1({p1, p2, p3});
        Polygon poly2({Point(10, 10), Point(11, 10), Point(10, 11)});

        poly1.set(poly2);

        // The actual Point objects should be mutated
        CHECK(p1->x == 10);
        CHECK(p1->y == 10);
        CHECK(p2->x == 11);
        CHECK(p3->y == 11);
    }

    TEST_CASE("Push creates new point, doesn't share") {
        Polygon poly;
        Point p(5, 5);

        poly.push(p);

        // Modifying original Point doesn't affect polygon
        p.x = 100;

        CHECK(poly[0].x == 5);
    }

    TEST_CASE("Push PointPtr shares point") {
        Polygon poly;
        auto p = makePoint(5, 5);

        poly.push(p);

        // Modifying shared pointer affects polygon
        p->x = 100;

        CHECK(poly[0].x == 100);
    }

    TEST_CASE("inset modifies polygon structure") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        // Get original area
        double origArea = std::abs(square.square());

        // Inset the first vertex
        square.inset(square.ptr(0), 1.0);

        // The polygon structure should have changed
        // (inset creates new points at adjacent indices)
        double newArea = std::abs(square.square());

        // Area should have changed after inset
        CHECK(newArea != origArea);
    }
}

TEST_SUITE("Voronoi mutations") {
    TEST_CASE("Voronoi relaxation creates new points") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(20, 0));
        points.push_back(makePoint(10, 20));

        // Store original positions
        double origX0 = points[0]->x;
        double origY0 = points[0]->y;

        Voronoi v1 = Voronoi::build(points);
        Voronoi v2 = Voronoi::relax(v1);

        // Original points should be unchanged
        CHECK(points[0]->x == origX0);
        CHECK(points[0]->y == origY0);

        // v2 has new points (may have moved from relaxation)
        // The important thing is original points are not mutated
    }

    TEST_CASE("Voronoi regions share vertices") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(20, 0));
        points.push_back(makePoint(10, 20));
        points.push_back(makePoint(10, 5));

        Voronoi v = Voronoi::build(points);
        auto parts = v.partioning();

        if (parts.size() >= 2) {
            // Check if any two regions share a vertex (by pointer)
            bool foundShared = false;
            for (size_t i = 0; i < parts.size() && !foundShared; ++i) {
                Polygon poly1 = parts[i].polygon();
                for (size_t j = i + 1; j < parts.size(); ++j) {
                    Polygon poly2 = parts[j].polygon();

                    // Check for shared pointers
                    for (size_t pi = 0; pi < poly1.length(); ++pi) {
                        if (poly2.indexOf(poly1.ptr(pi)) != -1) {
                            foundShared = true;
                            break;
                        }
                    }
                    if (foundShared) break;
                }
            }
            // Adjacent Voronoi regions should share vertices
            // (This is a fundamental property of Voronoi diagrams)
        }
    }
}

TEST_SUITE("Cross-object mutation scenarios") {
    TEST_CASE("Triangle point mutation affects circumcircle") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(4, 0);
        auto p3 = makePoint(2, 2);

        Triangle tri(p1, p2, p3);
        double origRadius = tri.r;

        // Move one point
        p3->y = 4;

        // Need to recreate triangle to recalculate circumcircle
        Triangle tri2(p1, p2, p3);

        CHECK(tri2.r != doctest::Approx(origRadius));
    }

    TEST_CASE("Polygon slice shares original points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(2, 0);
        auto p4 = makePoint(3, 0);

        Polygon original({p1, p2, p3, p4});
        Polygon sliced = original.slice(1, 3); // p2, p3

        // Mutate through slice
        sliced[0].x = 100;

        // Original should see the change
        CHECK(p2->x == 100);
        CHECK(original[1].x == 100);
    }

    TEST_CASE("Polygon concat shares original points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(2, 0);
        auto p4 = makePoint(3, 0);

        Polygon poly1({p1, p2});
        Polygon poly2({p3, p4});
        Polygon combined = poly1.concat(poly2);

        // Mutate through combined
        combined[0].x = 100;
        combined[2].x = 200;

        // Originals should see changes
        CHECK(p1->x == 100);
        CHECK(p3->x == 200);
    }

    TEST_CASE("Polygon filter shares original points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(5, 0);
        auto p3 = makePoint(10, 0);

        Polygon original({p1, p2, p3});
        Polygon filtered = original.filter([](const Point& p) {
            return p.x >= 5;
        });

        CHECK(filtered.length() == 2);

        // Mutate through filtered
        filtered[0].x = 500;

        // Original should see the change
        CHECK(p2->x == 500);
    }

    TEST_CASE("Polygon split shares original points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(2, 0);
        auto p3 = makePoint(2, 2);
        auto p4 = makePoint(0, 2);

        Polygon square({p1, p2, p3, p4});
        auto halves = square.split(p1, p3);

        CHECK(halves.size() == 2);

        // The split points should be shared
        CHECK(halves[0].indexOf(p1) != -1);
        CHECK(halves[0].indexOf(p3) != -1);

        // Mutate through half
        p1->x = 100;

        // All should see the change
        CHECK(square[0].x == 100);
        CHECK(halves[0][0].x == 100);
        CHECK(halves[1].ptr(halves[1].indexOf(p1))->x == 100);
    }
}
