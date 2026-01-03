#include "town_generator/geom/Voronoi.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace town_generator {
namespace geom {

// Triangle implementation
Triangle::Triangle(const Point& p1, const Point& p2, const Point& p3)
    : p1(p1), p2(p2), p3(p3) {

    // Calculate circumcircle center and radius
    // Using perpendicular bisectors of edges

    double ax = p1.x, ay = p1.y;
    double bx = p2.x, by = p2.y;
    double cx = p3.x, cy = p3.y;

    double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

    if (std::abs(d) < 1e-10) {
        // Degenerate triangle
        c = Point((ax + bx + cx) / 3.0, (ay + by + cy) / 3.0);
        r = 0;
        return;
    }

    double ax2 = ax * ax;
    double ay2 = ay * ay;
    double bx2 = bx * bx;
    double by2 = by * by;
    double cx2 = cx * cx;
    double cy2 = cy * cy;

    double ux = ((ax2 + ay2) * (by - cy) + (bx2 + by2) * (cy - ay) + (cx2 + cy2) * (ay - by)) / d;
    double uy = ((ax2 + ay2) * (cx - bx) + (bx2 + by2) * (ax - cx) + (cx2 + cy2) * (bx - ax)) / d;

    c = Point(ux, uy);
    r = Point::distance(c, p1);
}

// Region implementation
void Region::sortVertices() {
    // Sort triangles by angle from seed to circumcenter
    std::sort(vertices.begin(), vertices.end(),
        [this](Triangle* a, Triangle* b) {
            double angleA = std::atan2(a->c.y - seed.y, a->c.x - seed.x);
            double angleB = std::atan2(b->c.y - seed.y, b->c.x - seed.x);
            return angleA < angleB;
        });
}

Point Region::center() const {
    if (vertices.empty()) return seed;

    Point sum;
    for (const auto* tr : vertices) {
        sum.addEq(tr->c);
    }
    sum.scaleEq(1.0 / vertices.size());
    return sum;
}

std::vector<Region*> Region::neighbors(const std::vector<std::unique_ptr<Region>>& allRegions) const {
    std::vector<Region*> result;

    for (const auto& other : allRegions) {
        if (other.get() == this) continue;

        // Check if they share a triangle
        for (const auto* myTr : vertices) {
            for (const auto* theirTr : other->vertices) {
                if (myTr == theirTr) {
                    result.push_back(other.get());
                    goto nextRegion;
                }
            }
        }
        nextRegion:;
    }

    return result;
}

// Voronoi implementation
Voronoi::Voronoi(double width, double height) {
    // Create bounding frame (large triangle that contains all points)
    double margin = std::max(width, height) * 2;

    frame_.push_back(Point(-margin, -margin));
    frame_.push_back(Point(width + margin, -margin));
    frame_.push_back(Point(width / 2, height + margin));

    // Initial triangle containing the frame
    triangles.push_back(std::make_unique<Triangle>(frame_[0], frame_[1], frame_[2]));

    // Create regions for frame points
    for (const auto& p : frame_) {
        auto region = std::make_unique<Region>(p);
        region->vertices.push_back(triangles[0].get());
        regions.push_back(std::move(region));
    }
}

Region* Voronoi::findRegion(const Point& p) {
    for (auto& region : regions) {
        if (region->seed == p) {
            return region.get();
        }
    }
    return nullptr;
}

void Voronoi::updateRegions(Triangle* tr) {
    // Add this triangle to all three vertex regions
    auto* r1 = findRegion(tr->p1);
    auto* r2 = findRegion(tr->p2);
    auto* r3 = findRegion(tr->p3);

    if (r1) r1->vertices.push_back(tr);
    if (r2) r2->vertices.push_back(tr);
    if (r3) r3->vertices.push_back(tr);
}

void Voronoi::addPoint(const Point& p) {
    // Find all triangles whose circumcircle contains the new point
    std::vector<Triangle*> badTriangles;

    for (auto& tr : triangles) {
        if (tr->isInCircumcircle(p)) {
            badTriangles.push_back(tr.get());
        }
    }

    if (badTriangles.empty()) return;

    // Find the boundary of the polygonal hole
    std::vector<std::pair<Point, Point>> boundary;

    for (auto* tr : badTriangles) {
        std::pair<Point, Point> edges[3] = {
            {tr->p1, tr->p2},
            {tr->p2, tr->p3},
            {tr->p3, tr->p1}
        };

        for (const auto& edge : edges) {
            bool isShared = false;

            for (auto* other : badTriangles) {
                if (other == tr) continue;

                // Check if this edge is shared with another bad triangle
                if ((other->p1 == edge.first || other->p2 == edge.first || other->p3 == edge.first) &&
                    (other->p1 == edge.second || other->p2 == edge.second || other->p3 == edge.second)) {
                    isShared = true;
                    break;
                }
            }

            if (!isShared) {
                boundary.push_back(edge);
            }
        }
    }

    // Remove bad triangles from regions
    for (auto* tr : badTriangles) {
        auto* r1 = findRegion(tr->p1);
        auto* r2 = findRegion(tr->p2);
        auto* r3 = findRegion(tr->p3);

        if (r1) {
            auto it = std::find(r1->vertices.begin(), r1->vertices.end(), tr);
            if (it != r1->vertices.end()) r1->vertices.erase(it);
        }
        if (r2) {
            auto it = std::find(r2->vertices.begin(), r2->vertices.end(), tr);
            if (it != r2->vertices.end()) r2->vertices.erase(it);
        }
        if (r3) {
            auto it = std::find(r3->vertices.begin(), r3->vertices.end(), tr);
            if (it != r3->vertices.end()) r3->vertices.erase(it);
        }
    }

    // Remove bad triangles
    triangles.erase(
        std::remove_if(triangles.begin(), triangles.end(),
            [&badTriangles](const std::unique_ptr<Triangle>& tr) {
                return std::find(badTriangles.begin(), badTriangles.end(), tr.get()) != badTriangles.end();
            }),
        triangles.end());

    // Create new region for the new point
    auto newRegion = std::make_unique<Region>(p);

    // Create new triangles from boundary edges to new point
    for (const auto& edge : boundary) {
        auto newTr = std::make_unique<Triangle>(edge.first, edge.second, p);
        Triangle* trPtr = newTr.get();

        updateRegions(trPtr);
        newRegion->vertices.push_back(trPtr);

        triangles.push_back(std::move(newTr));
    }

    regions.push_back(std::move(newRegion));
}

std::vector<Region*> Voronoi::partitioning() {
    std::vector<Region*> result;

    for (auto& region : regions) {
        // Skip regions that touch the frame
        bool touchesFrame = false;
        for (const auto& framePoint : frame_) {
            if (region->seed == framePoint) {
                touchesFrame = true;
                break;
            }
        }

        if (!touchesFrame) {
            region->sortVertices();
            result.push_back(region.get());
        }
    }

    return result;
}

std::vector<Point> Voronoi::relax(const std::vector<Point>& vertices, double width, double height) {
    Voronoi voronoi(width, height);

    for (const auto& v : vertices) {
        voronoi.addPoint(v);
    }

    std::vector<Point> relaxed;
    auto regions = voronoi.partitioning();

    for (auto* region : regions) {
        relaxed.push_back(region->center());
    }

    return relaxed;
}

Voronoi Voronoi::build(const std::vector<Point>& vertices) {
    if (vertices.empty()) {
        return Voronoi(100, 100);
    }

    // Find bounds
    double minX = vertices[0].x, maxX = vertices[0].x;
    double minY = vertices[0].y, maxY = vertices[0].y;

    for (const auto& v : vertices) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }

    double width = maxX - minX;
    double height = maxY - minY;

    Voronoi voronoi(width * 2, height * 2);

    for (const auto& v : vertices) {
        voronoi.addPoint(v);
    }

    return voronoi;
}

} // namespace geom
} // namespace town_generator
