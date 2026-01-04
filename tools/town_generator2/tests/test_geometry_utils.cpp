#include <doctest/doctest.h>
#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Segment.hpp"
#include "town_generator2/geom/Spline.hpp"
#include "town_generator2/geom/GeomUtils.hpp"
#include "town_generator2/geom/Circle.hpp"
#include "town_generator2/utils/MathUtils.hpp"

using namespace town_generator2::geom;
using namespace town_generator2::utils;

TEST_SUITE("Segment") {
    TEST_CASE("Segment construction from PointPtr") {
        auto start = makePoint(0, 0);
        auto end = makePoint(3, 4);

        Segment seg(start, end);

        CHECK(seg.start->x == 0);
        CHECK(seg.start->y == 0);
        CHECK(seg.end->x == 3);
        CHECK(seg.end->y == 4);
    }

    TEST_CASE("Segment construction from Point values") {
        Segment seg(Point(1, 2), Point(5, 6));

        CHECK(seg.start->x == 1);
        CHECK(seg.start->y == 2);
        CHECK(seg.end->x == 5);
        CHECK(seg.end->y == 6);
    }

    TEST_CASE("Segment default construction") {
        Segment seg;

        CHECK(seg.start.get() != nullptr);
        CHECK(seg.end.get() != nullptr);
        CHECK(seg.start->x == 0);
        CHECK(seg.start->y == 0);
    }

    TEST_CASE("Segment dx/dy") {
        Segment seg(Point(1, 2), Point(4, 6));

        CHECK(seg.dx() == 3);
        CHECK(seg.dy() == 4);
    }

    TEST_CASE("Segment vector") {
        Segment seg(Point(1, 1), Point(4, 5));
        Point v = seg.vector();

        CHECK(v.x == 3);
        CHECK(v.y == 4);
    }

    TEST_CASE("Segment length") {
        Segment seg(Point(0, 0), Point(3, 4));
        CHECK(seg.length() == doctest::Approx(5.0));
    }

    TEST_CASE("Segment zero length") {
        Segment seg(Point(5, 5), Point(5, 5));
        CHECK(seg.length() == doctest::Approx(0.0));
    }
}

TEST_SUITE("Circle") {
    TEST_CASE("Circle default construction") {
        Circle c;

        CHECK(c.x == 0);
        CHECK(c.y == 0);
        CHECK(c.r == 0);
    }

    TEST_CASE("Circle parameterized construction") {
        Circle c(3.0, 4.0, 5.0);

        CHECK(c.x == 3.0);
        CHECK(c.y == 4.0);
        CHECK(c.r == 5.0);
    }
}

TEST_SUITE("Spline") {
    TEST_CASE("Spline startCurve") {
        Point p0(0, 0);
        Point p1(5, 0);
        Point p2(10, 0);

        auto result = Spline::startCurve(p0, p1, p2);

        CHECK(result.size() == 2);
        // Control point and p1
        CHECK(result[1].x == doctest::Approx(5.0));
        CHECK(result[1].y == doctest::Approx(0.0));
    }

    TEST_CASE("Spline endCurve") {
        Point p0(0, 0);
        Point p1(5, 0);
        Point p2(10, 0);

        auto result = Spline::endCurve(p0, p1, p2);

        CHECK(result.size() == 2);
        // Control point and p2
        CHECK(result[1].x == doctest::Approx(10.0));
        CHECK(result[1].y == doctest::Approx(0.0));
    }

    TEST_CASE("Spline midCurve") {
        Point p0(0, 0);
        Point p1(5, 0);
        Point p2(10, 0);
        Point p3(15, 0);

        auto result = Spline::midCurve(p0, p1, p2, p3);

        CHECK(result.size() == 4);
        // Should return p1a, p12, p2a, p2
        CHECK(result[3].x == doctest::Approx(10.0));
        CHECK(result[3].y == doctest::Approx(0.0));
    }

    TEST_CASE("Spline with curved path") {
        Point p0(0, 0);
        Point p1(5, 5);
        Point p2(10, 0);

        auto result = Spline::startCurve(p0, p1, p2);

        // The curve should produce control points
        CHECK(result.size() == 2);
    }
}

TEST_SUITE("GeomUtils") {
    TEST_CASE("intersectLines - perpendicular lines") {
        // Horizontal line at y=5 from x=0
        // Vertical line at x=3 from y=0
        auto result = GeomUtils::intersectLines(
            0, 5, 10, 0,   // horizontal line: (0,5) + t*(10,0)
            3, 0, 0, 10    // vertical line: (3,0) + t*(0,10)
        );

        CHECK(result.has_value());
        // t1 should be 0.3 (x=3 on first line)
        CHECK(result->x == doctest::Approx(0.3).epsilon(0.01));
        // t2 should be 0.5 (y=5 on second line)
        CHECK(result->y == doctest::Approx(0.5).epsilon(0.01));
    }

    TEST_CASE("intersectLines - parallel lines (no intersection)") {
        // Two horizontal parallel lines
        auto result = GeomUtils::intersectLines(
            0, 0, 10, 0,   // line at y=0
            0, 5, 10, 0    // line at y=5
        );

        CHECK_FALSE(result.has_value());
    }

    TEST_CASE("intersectLines - diagonal lines") {
        // Line from origin with slope 1
        // Line from (0,2) with slope -1
        auto result = GeomUtils::intersectLines(
            0, 0, 1, 1,    // y = x
            0, 2, 1, -1    // y = -x + 2
        );

        CHECK(result.has_value());
        // Intersection at (1, 1): t1=1, t2=1
        CHECK(result->x == doctest::Approx(1.0).epsilon(0.01));
        CHECK(result->y == doctest::Approx(1.0).epsilon(0.01));
    }

    TEST_CASE("interpolate - midpoint") {
        Point p1(0, 0);
        Point p2(10, 10);

        Point mid = GeomUtils::interpolate(p1, p2, 0.5);

        CHECK(mid.x == doctest::Approx(5.0));
        CHECK(mid.y == doctest::Approx(5.0));
    }

    TEST_CASE("interpolate - at start") {
        Point p1(0, 0);
        Point p2(10, 10);

        Point start = GeomUtils::interpolate(p1, p2, 0.0);

        CHECK(start.x == doctest::Approx(0.0));
        CHECK(start.y == doctest::Approx(0.0));
    }

    TEST_CASE("interpolate - at end") {
        Point p1(0, 0);
        Point p2(10, 10);

        Point end = GeomUtils::interpolate(p1, p2, 1.0);

        CHECK(end.x == doctest::Approx(10.0));
        CHECK(end.y == doctest::Approx(10.0));
    }

    TEST_CASE("interpolate - quarter") {
        Point p1(0, 0);
        Point p2(8, 4);

        Point quarter = GeomUtils::interpolate(p1, p2, 0.25);

        CHECK(quarter.x == doctest::Approx(2.0));
        CHECK(quarter.y == doctest::Approx(1.0));
    }

    TEST_CASE("scalar (dot product)") {
        // Perpendicular vectors have dot product 0
        CHECK(GeomUtils::scalar(1, 0, 0, 1) == doctest::Approx(0.0));

        // Parallel vectors
        CHECK(GeomUtils::scalar(2, 0, 3, 0) == doctest::Approx(6.0));

        // General case
        CHECK(GeomUtils::scalar(1, 2, 3, 4) == doctest::Approx(11.0));
    }

    TEST_CASE("cross product (2D)") {
        // i x j = k (positive)
        CHECK(GeomUtils::cross(1, 0, 0, 1) == doctest::Approx(1.0));

        // j x i = -k (negative)
        CHECK(GeomUtils::cross(0, 1, 1, 0) == doctest::Approx(-1.0));

        // Parallel vectors have zero cross product
        CHECK(GeomUtils::cross(2, 0, 4, 0) == doctest::Approx(0.0));

        // General case: (1,2) x (3,4) = 1*4 - 2*3 = -2
        CHECK(GeomUtils::cross(1, 2, 3, 4) == doctest::Approx(-2.0));
    }

    TEST_CASE("distance2line - point on line") {
        // Distance from (5, 0) to horizontal line at y=0
        double d = GeomUtils::distance2line(0, 0, 10, 0, 5, 0);
        CHECK(d == doctest::Approx(0.0).epsilon(0.001));
    }

    TEST_CASE("distance2line - point above line") {
        // Distance from (5, 3) to horizontal line at y=0
        double d = GeomUtils::distance2line(0, 0, 1, 0, 5, 3);
        CHECK(std::abs(d) == doctest::Approx(3.0).epsilon(0.001));
    }
}

TEST_SUITE("MathUtils") {
    TEST_CASE("gate - clamp double") {
        CHECK(MathUtils::gate(5.0, 0.0, 10.0) == 5.0);
        CHECK(MathUtils::gate(-5.0, 0.0, 10.0) == 0.0);
        CHECK(MathUtils::gate(15.0, 0.0, 10.0) == 10.0);
        CHECK(MathUtils::gate(0.0, 0.0, 10.0) == 0.0);
        CHECK(MathUtils::gate(10.0, 0.0, 10.0) == 10.0);
    }

    TEST_CASE("gatei - clamp int") {
        CHECK(MathUtils::gatei(5, 0, 10) == 5);
        CHECK(MathUtils::gatei(-5, 0, 10) == 0);
        CHECK(MathUtils::gatei(15, 0, 10) == 10);
        CHECK(MathUtils::gatei(0, 0, 10) == 0);
        CHECK(MathUtils::gatei(10, 0, 10) == 10);
    }

    TEST_CASE("sign") {
        CHECK(MathUtils::sign(5.0) == 1);
        CHECK(MathUtils::sign(-5.0) == -1);
        CHECK(MathUtils::sign(0.0) == 0);
        CHECK(MathUtils::sign(0.001) == 1);
        CHECK(MathUtils::sign(-0.001) == -1);
    }
}
