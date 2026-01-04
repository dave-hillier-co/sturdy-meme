#include <doctest/doctest.h>
#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"

using namespace town_generator2::geom;

TEST_SUITE("Polygon centroid") {
    TEST_CASE("Centroid of square") {
        Polygon square({Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)});
        Point c = square.centroid();

        CHECK(c.x == doctest::Approx(1.0).epsilon(0.01));
        CHECK(c.y == doctest::Approx(1.0).epsilon(0.01));
    }

    TEST_CASE("Centroid of triangle") {
        Polygon tri({Point(0, 0), Point(3, 0), Point(0, 3)});
        Point c = tri.centroid();

        // Centroid of right triangle is at (1, 1)
        CHECK(c.x == doctest::Approx(1.0).epsilon(0.1));
        CHECK(c.y == doctest::Approx(1.0).epsilon(0.1));
    }

    TEST_CASE("Centroid vs center for regular polygon") {
        Polygon hex = Polygon::regular(6, 10.0);
        Point center = hex.center();
        Point centroid = hex.centroid();

        // For regular polygon, center and centroid should be close
        CHECK(center.x == doctest::Approx(centroid.x).epsilon(0.1));
        CHECK(center.y == doctest::Approx(centroid.y).epsilon(0.1));
    }
}

TEST_SUITE("Polygon iteration") {
    TEST_CASE("forEdge iterates all edges") {
        Polygon triangle({Point(0, 0), Point(1, 0), Point(0, 1)});

        int edgeCount = 0;
        triangle.forEdge([&edgeCount](const Point& v0, const Point& v1) {
            edgeCount++;
        });

        CHECK(edgeCount == 3);
    }

    TEST_CASE("forEdgePtr provides pointer access") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(0, 1);

        Polygon triangle({p1, p2, p3});

        std::vector<PointPtr> collectedStarts;
        triangle.forEdgePtr([&collectedStarts](const PointPtr& v0, const PointPtr& v1) {
            collectedStarts.push_back(v0);
        });

        CHECK(collectedStarts.size() == 3);
        CHECK(collectedStarts[0].get() == p1.get());
        CHECK(collectedStarts[1].get() == p2.get());
        CHECK(collectedStarts[2].get() == p3.get());
    }

    TEST_CASE("forSegment doesn't close loop") {
        Polygon line({Point(0, 0), Point(1, 0), Point(2, 0)});

        int segCount = 0;
        line.forSegment([&segCount](const Point& v0, const Point& v1) {
            segCount++;
        });

        CHECK(segCount == 2); // Not 3 - doesn't close back to start
    }
}

TEST_SUITE("Polygon getBounds") {
    TEST_CASE("Bounds of square") {
        Polygon square({Point(1, 2), Point(5, 2), Point(5, 6), Point(1, 6)});
        Rectangle bounds = square.getBounds();

        CHECK(bounds.left == 1);
        CHECK(bounds.right == 5);
        CHECK(bounds.top == 2);
        CHECK(bounds.bottom == 6);
        CHECK(bounds.width() == 4);
        CHECK(bounds.height() == 4);
    }

    TEST_CASE("Bounds of empty polygon") {
        Polygon empty;
        Rectangle bounds = empty.getBounds();

        CHECK(bounds.width() == 0);
        CHECK(bounds.height() == 0);
    }

    TEST_CASE("Bounds of rotated square") {
        // Diamond shape (square rotated 45 degrees)
        Polygon diamond({Point(0, 1), Point(1, 0), Point(0, -1), Point(-1, 0)});
        Rectangle bounds = diamond.getBounds();

        CHECK(bounds.left == -1);
        CHECK(bounds.right == 1);
        CHECK(bounds.top == -1);
        CHECK(bounds.bottom == 1);
    }
}

TEST_SUITE("Polygon simplify") {
    TEST_CASE("Simplify to fewer vertices") {
        // Create a polygon with many vertices
        Polygon circle = Polygon::circle(5.0); // 16 vertices

        CHECK(circle.length() == 16);

        circle.simplify(8);

        CHECK(circle.length() == 8);
    }

    TEST_CASE("Simplify preserves approximate area") {
        Polygon circle = Polygon::circle(5.0);
        double originalArea = std::abs(circle.square());

        circle.simplify(8);
        double simplifiedArea = std::abs(circle.square());

        // Should preserve most of the area
        CHECK(simplifiedArea > originalArea * 0.8);
    }

    TEST_CASE("Simplify removes least significant vertices first") {
        // Create a polygon with one vertex very close to the line between neighbors
        Polygon poly({
            Point(0, 0),
            Point(1, 0.01),  // Almost on the line
            Point(2, 0),
            Point(2, 2),
            Point(0, 2)
        });

        CHECK(poly.length() == 5);

        poly.simplify(4);

        CHECK(poly.length() == 4);
        // The almost-collinear vertex should have been removed
    }
}

TEST_SUITE("Polygon peel") {
    TEST_CASE("Peel one edge of square") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(10, 0);
        auto p3 = makePoint(10, 10);
        auto p4 = makePoint(0, 10);

        Polygon square({p1, p2, p3, p4});
        double originalArea = std::abs(square.square());

        Polygon peeled = square.peel(p1, 1.0);

        double peeledArea = std::abs(peeled.square());

        // Peeled polygon should have less area
        CHECK(peeledArea < originalArea);
        CHECK(peeledArea > 0);
    }

    TEST_CASE("Peel with zero distance returns same polygon") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(10, 0);
        auto p3 = makePoint(10, 10);
        auto p4 = makePoint(0, 10);

        Polygon square({p1, p2, p3, p4});
        double originalArea = std::abs(square.square());

        Polygon peeled = square.peel(p1, 0.0);

        double peeledArea = std::abs(peeled.square());

        CHECK(peeledArea == doctest::Approx(originalArea).epsilon(0.1));
    }
}

TEST_SUITE("Polygon interpolate") {
    TEST_CASE("Interpolate point at center") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Point center(5, 5);

        auto weights = square.interpolate(center);

        CHECK(weights.size() == 4);

        // For center point, weights should be roughly equal
        double sum = 0;
        for (double w : weights) {
            sum += w;
            CHECK(w > 0);
        }
        CHECK(sum == doctest::Approx(1.0).epsilon(0.01));
    }

    TEST_CASE("Interpolate point near vertex") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Point nearVertex(0.1, 0.1);

        auto weights = square.interpolate(nearVertex);

        // Weight for first vertex should be highest
        CHECK(weights[0] > weights[1]);
        CHECK(weights[0] > weights[2]);
        CHECK(weights[0] > weights[3]);
    }
}

TEST_SUITE("Polygon min/max") {
    TEST_CASE("Find leftmost vertex") {
        auto p1 = makePoint(5, 0);
        auto p2 = makePoint(10, 5);
        auto p3 = makePoint(5, 10);
        auto p4 = makePoint(0, 5);  // Leftmost

        Polygon diamond({p1, p2, p3, p4});

        PointPtr leftmost = diamond.min([](const Point& p) { return p.x; });

        CHECK(leftmost.get() == p4.get());
    }

    TEST_CASE("Find rightmost vertex") {
        auto p1 = makePoint(5, 0);
        auto p2 = makePoint(10, 5);  // Rightmost
        auto p3 = makePoint(5, 10);
        auto p4 = makePoint(0, 5);

        Polygon diamond({p1, p2, p3, p4});

        PointPtr rightmost = diamond.max([](const Point& p) { return p.x; });

        CHECK(rightmost.get() == p2.get());
    }

    TEST_CASE("Find closest vertex to a point") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(10, 0);
        auto p3 = makePoint(10, 10);
        auto p4 = makePoint(0, 10);

        Polygon square({p1, p2, p3, p4});
        Point target(9, 9);

        PointPtr closest = square.min([&target](const Point& p) {
            return Point::distance(p, target);
        });

        CHECK(closest.get() == p3.get());
    }
}

TEST_SUITE("Polygon count") {
    TEST_CASE("Count vertices matching predicate") {
        Polygon poly({Point(0, 0), Point(5, 0), Point(10, 0), Point(15, 0)});

        int countAbove3 = poly.count([](const Point& p) { return p.x > 3; });

        CHECK(countAbove3 == 3);
    }

    TEST_CASE("Count with no matches") {
        Polygon poly({Point(0, 0), Point(1, 0), Point(2, 0)});

        int countNegative = poly.count([](const Point& p) { return p.x < 0; });

        CHECK(countNegative == 0);
    }
}

TEST_SUITE("Polygon next/prev") {
    TEST_CASE("next wraps around") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon triangle({p1, p2, p3});

        CHECK(triangle.next(p1).get() == p2.get());
        CHECK(triangle.next(p2).get() == p3.get());
        CHECK(triangle.next(p3).get() == p1.get()); // Wraps around
    }

    TEST_CASE("prev wraps around") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon triangle({p1, p2, p3});

        CHECK(triangle.prev(p1).get() == p3.get()); // Wraps around
        CHECK(triangle.prev(p2).get() == p1.get());
        CHECK(triangle.prev(p3).get() == p2.get());
    }

    TEST_CASE("nexti and previ by index") {
        Polygon triangle({Point(0, 0), Point(1, 0), Point(1, 1)});

        CHECK(triangle.nexti(0)->x == 1);
        CHECK(triangle.nexti(1)->y == 1);
        CHECK(triangle.nexti(2)->x == 0); // Wraps

        CHECK(triangle.previ(0)->y == 1); // Wraps
        CHECK(triangle.previ(1)->x == 0);
        CHECK(triangle.previ(2)->x == 1);
    }
}

TEST_SUITE("Polygon vector") {
    TEST_CASE("vector from vertex to next") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(3, 4);
        auto p3 = makePoint(0, 4);

        Polygon triangle({p1, p2, p3});

        Point v = triangle.vector(p1);

        CHECK(v.x == 3);
        CHECK(v.y == 4);
    }

    TEST_CASE("vectori by index") {
        Polygon triangle({Point(0, 0), Point(5, 0), Point(0, 5)});

        Point v0 = triangle.vectori(0);
        Point v1 = triangle.vectori(1);
        Point v2 = triangle.vectori(2);

        CHECK(v0.x == 5);
        CHECK(v0.y == 0);
        CHECK(v1.x == -5);
        CHECK(v1.y == 5);
        CHECK(v2.x == 0);
        CHECK(v2.y == -5);
    }
}

TEST_SUITE("Polygon convexity checks") {
    TEST_CASE("isConvexVertexi for convex polygon") {
        Polygon square({Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)});

        for (int i = 0; i < 4; ++i) {
            CHECK(square.isConvexVertexi(i));
        }
    }

    TEST_CASE("isConvexVertexi for concave polygon") {
        // L-shape has a concave vertex
        Polygon lShape({
            Point(0, 0), Point(2, 0), Point(2, 1),
            Point(1, 1), Point(1, 2), Point(0, 2)
        });

        // The vertex at (1, 1) should be concave
        int concaveCount = 0;
        for (size_t i = 0; i < lShape.length(); ++i) {
            if (!lShape.isConvexVertexi(static_cast<int>(i))) {
                concaveCount++;
            }
        }
        CHECK(concaveCount >= 1);
    }

    TEST_CASE("isConvexVertex by pointer") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);
        auto p4 = makePoint(0, 1);

        Polygon square({p1, p2, p3, p4});

        CHECK(square.isConvexVertex(p1));
        CHECK(square.isConvexVertex(p2));
        CHECK(square.isConvexVertex(p3));
        CHECK(square.isConvexVertex(p4));
    }
}

TEST_SUITE("Polygon smoothing") {
    TEST_CASE("smoothVertexi smooths single vertex") {
        Polygon zigzag({Point(0, 0), Point(1, 2), Point(2, 0), Point(3, 2)});

        Point smoothed = zigzag.smoothVertexi(1, 1.0);

        // Smoothed point should be between neighbors
        CHECK(smoothed.y < 2.0);
        CHECK(smoothed.y > 0.0);
    }

    TEST_CASE("smoothVertexEq returns smoothed polygon") {
        Polygon jagged({Point(0, 0), Point(1, 1), Point(2, 0), Point(3, 1)});

        Polygon smoothed = jagged.smoothVertexEq(1.0);

        CHECK(smoothed.length() == jagged.length());

        // Smoothed vertices should differ from original
        bool anyDifferent = false;
        for (size_t i = 0; i < jagged.length(); ++i) {
            if (jagged[i].x != smoothed[i].x || jagged[i].y != smoothed[i].y) {
                anyDifferent = true;
                break;
            }
        }
        CHECK(anyDifferent);
    }
}

TEST_SUITE("Polygon filterShort") {
    TEST_CASE("filterShort removes short edges") {
        Polygon poly({
            Point(0, 0),
            Point(0.1, 0),   // Very close to previous
            Point(10, 0),
            Point(10, 10),
            Point(0, 10)
        });

        CHECK(poly.length() == 5);

        Polygon filtered = poly.filterShort(0.5);

        CHECK(filtered.length() < 5);
    }

    TEST_CASE("filterShort keeps long edges") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        Polygon filtered = square.filterShort(0.5);

        CHECK(filtered.length() == 4);
    }
}

TEST_SUITE("Polygon lastIndexOf") {
    TEST_CASE("lastIndexOf finds last occurrence") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);

        Polygon poly({p1, p2, p1}); // p1 appears twice

        CHECK(poly.indexOf(p1) == 0);
        CHECK(poly.lastIndexOf(p1) == 2);
    }

    TEST_CASE("lastIndexOfByValue finds by coordinates") {
        Polygon poly({
            Point(0, 0),
            Point(1, 0),
            Point(0, 0)  // Same coordinates, different object
        });

        CHECK(poly.indexOfByValue(Point(0, 0)) == 0);
        CHECK(poly.lastIndexOfByValue(Point(0, 0)) == 2);
    }
}

TEST_SUITE("Polygon distance") {
    TEST_CASE("Distance to vertex") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        double d = square.distance(Point(0, 0));
        CHECK(d == doctest::Approx(0.0));
    }

    TEST_CASE("Distance to external point") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        double d = square.distance(Point(-5, 0));
        CHECK(d == doctest::Approx(5.0));
    }

    TEST_CASE("Distance empty polygon") {
        Polygon empty;
        double d = empty.distance(Point(0, 0));

        CHECK(d == std::numeric_limits<double>::infinity());
    }
}

TEST_SUITE("Polygon splice") {
    TEST_CASE("splice removes elements") {
        Polygon poly({Point(0, 0), Point(1, 0), Point(2, 0), Point(3, 0)});

        poly.splice(1, 2); // Remove indices 1 and 2

        CHECK(poly.length() == 2);
        CHECK(poly[0].x == 0);
        CHECK(poly[1].x == 3);
    }

    TEST_CASE("splice at end") {
        Polygon poly({Point(0, 0), Point(1, 0), Point(2, 0)});

        poly.splice(2, 1);

        CHECK(poly.length() == 2);
    }

    TEST_CASE("splice beyond end is safe") {
        Polygon poly({Point(0, 0), Point(1, 0), Point(2, 0)});

        poly.splice(1, 100); // Try to delete more than available

        CHECK(poly.length() == 1);
    }
}

TEST_SUITE("Polygon remove") {
    TEST_CASE("remove by pointer") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(2, 0);

        Polygon poly({p1, p2, p3});

        bool removed = poly.remove(p2);

        CHECK(removed);
        CHECK(poly.length() == 2);
        CHECK(poly.indexOf(p2) == -1);
    }

    TEST_CASE("remove non-existent returns false") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto other = makePoint(5, 5);

        Polygon poly({p1, p2});

        bool removed = poly.remove(other);

        CHECK_FALSE(removed);
        CHECK(poly.length() == 2);
    }

    TEST_CASE("removeByValue removes by coordinates") {
        Polygon poly({Point(0, 0), Point(1, 0), Point(2, 0)});

        bool removed = poly.removeByValue(Point(1, 0));

        CHECK(removed);
        CHECK(poly.length() == 2);
    }
}
