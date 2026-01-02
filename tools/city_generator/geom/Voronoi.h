#pragma once

#include "Point.h"
#include "Circle.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

namespace towngenerator {
namespace geom {

// Forward declarations
class Triangle;
class Region;

class Triangle {
public:
    Point p1, p2, p3;  // vertices (counter-clockwise)
    Point c;           // circumcircle center
    float r;           // circumcircle radius

    Triangle(const Point& p1, const Point& p2, const Point& p3)
        : p1(p1), p2(p2), p3(p3) {
        // Calculate circumcircle center and radius using perpendicular bisector intersection
        // Formula for circumcenter:
        // D = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
        // cx = ((x1^2 + y1^2) * (y2 - y3) + (x2^2 + y2^2) * (y3 - y1) + (x3^2 + y3^2) * (y1 - y2)) / D
        // cy = ((x1^2 + y1^2) * (x3 - x2) + (x2^2 + y2^2) * (x1 - x3) + (x3^2 + y3^2) * (x2 - x1)) / D

        float x1 = p1.x, y1 = p1.y;
        float x2 = p2.x, y2 = p2.y;
        float x3 = p3.x, y3 = p3.y;

        float D = 2.0f * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));

        if (std::abs(D) < 1e-10f) {
            // Degenerate triangle (collinear points), use centroid as fallback
            c.x = (x1 + x2 + x3) / 3.0f;
            c.y = (y1 + y2 + y3) / 3.0f;
            r = std::numeric_limits<float>::max();
        } else {
            float x1sq = x1 * x1 + y1 * y1;
            float x2sq = x2 * x2 + y2 * y2;
            float x3sq = x3 * x3 + y3 * y3;

            c.x = (x1sq * (y2 - y3) + x2sq * (y3 - y1) + x3sq * (y1 - y2)) / D;
            c.y = (x1sq * (x3 - x2) + x2sq * (x1 - x3) + x3sq * (x2 - x1)) / D;

            // Calculate radius as distance from circumcenter to any vertex
            r = Point::distance(c, p1);
        }
    }

    // Test if point is inside circumcircle
    bool inCircumcircle(const Point& p) const {
        float dist = Point::distance(c, p);
        return dist < r;
    }

    // Check if triangle contains a specific point (as a vertex)
    bool hasVertex(const Point& p) const {
        const float epsilon = 1e-6f;
        return (Point::distance(p1, p) < epsilon) ||
               (Point::distance(p2, p) < epsilon) ||
               (Point::distance(p3, p) < epsilon);
    }

    // Get vertices as an array
    std::vector<Point> getVertices() const {
        return {p1, p2, p3};
    }
};

class Region {
public:
    Point seed;                           // the original point this region is for
    std::vector<Triangle*> vertices;      // triangles that have seed as a vertex

    explicit Region(const Point& seed) : seed(seed) {}

    // Calculate center as average of triangle circumcenters
    // (In Haxe, it's weighted by something - we'll use simple average for now)
    Point center() const {
        if (vertices.empty()) {
            return seed;
        }

        float sumX = 0.0f;
        float sumY = 0.0f;
        int count = 0;

        for (const Triangle* t : vertices) {
            sumX += t->c.x;
            sumY += t->c.y;
            count++;
        }

        if (count == 0) {
            return seed;
        }

        return Point(sumX / count, sumY / count);
    }

    // Check if regions share an edge (have 2+ common triangles)
    bool isAdjacent(const Region& other) const {
        int commonCount = 0;
        for (const Triangle* t1 : vertices) {
            for (const Triangle* t2 : other.vertices) {
                if (t1 == t2) {
                    commonCount++;
                    if (commonCount >= 2) {
                        return true;
                    }
                }
            }
        }
        return commonCount >= 2;
    }
};

class Voronoi {
private:
    std::vector<std::unique_ptr<Triangle>> triangles;
    std::vector<std::unique_ptr<Region>> regions;
    std::vector<Point> frame;    // 4 corner points of bounding box
    std::vector<Point> points;   // all added points
    float width;
    float height;

    // Helper function to check if a point is a frame point
    bool isFramePoint(const Point& p) const {
        const float epsilon = 1e-6f;
        for (const Point& fp : frame) {
            if (Point::distance(p, fp) < epsilon) {
                return true;
            }
        }
        return false;
    }

    // Helper to find region for a point
    Region* findRegion(const Point& p) {
        const float epsilon = 1e-6f;
        for (auto& region : regions) {
            if (Point::distance(region->seed, p) < epsilon) {
                return region.get();
            }
        }
        return nullptr;
    }

public:
    // Constructor: creates bounding box frame and initial triangulation
    Voronoi(float width, float height) : width(width), height(height) {
        // Create frame as 4 corners with some padding
        float padding = std::max(width, height) * 0.1f;
        frame.push_back(Point(-padding, -padding));              // bottom-left
        frame.push_back(Point(width + padding, -padding));       // bottom-right
        frame.push_back(Point(width + padding, height + padding)); // top-right
        frame.push_back(Point(-padding, height + padding));       // top-left

        // Create 2 initial triangles covering the bounding box
        // Triangle 1: bottom-left, bottom-right, top-right
        triangles.push_back(std::make_unique<Triangle>(frame[0], frame[1], frame[2]));
        // Triangle 2: bottom-left, top-right, top-left
        triangles.push_back(std::make_unique<Triangle>(frame[0], frame[2], frame[3]));
    }

    // Incremental point insertion using Bowyer-Watson algorithm
    void addPoint(const Point& p) {
        points.push_back(p);

        // Step 1: Find all triangles whose circumcircle contains p
        std::vector<Triangle*> badTriangles;
        for (auto& tri : triangles) {
            if (tri->inCircumcircle(p)) {
                badTriangles.push_back(tri.get());
            }
        }

        // Step 2: Find the boundary of the polygonal hole (edges that appear only once)
        struct Edge {
            Point p1, p2;
            Edge(const Point& p1, const Point& p2) : p1(p1), p2(p2) {}

            bool operator==(const Edge& other) const {
                const float epsilon = 1e-6f;
                return (Point::distance(p1, other.p1) < epsilon && Point::distance(p2, other.p2) < epsilon) ||
                       (Point::distance(p1, other.p2) < epsilon && Point::distance(p2, other.p1) < epsilon);
            }
        };

        std::vector<Edge> polygon;
        for (Triangle* tri : badTriangles) {
            Edge edges[3] = {
                Edge(tri->p1, tri->p2),
                Edge(tri->p2, tri->p3),
                Edge(tri->p3, tri->p1)
            };

            for (const Edge& edge : edges) {
                // Check if this edge is shared with another bad triangle
                bool shared = false;
                for (Triangle* other : badTriangles) {
                    if (other == tri) continue;

                    Edge otherEdges[3] = {
                        Edge(other->p1, other->p2),
                        Edge(other->p2, other->p3),
                        Edge(other->p3, other->p1)
                    };

                    for (const Edge& otherEdge : otherEdges) {
                        if (edge == otherEdge) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared) break;
                }

                // If edge is not shared, it's on the boundary
                if (!shared) {
                    polygon.push_back(edge);
                }
            }
        }

        // Step 3: Remove bad triangles
        triangles.erase(
            std::remove_if(triangles.begin(), triangles.end(),
                [&badTriangles](const std::unique_ptr<Triangle>& tri) {
                    return std::find(badTriangles.begin(), badTriangles.end(), tri.get()) != badTriangles.end();
                }),
            triangles.end()
        );

        // Step 4: Create new triangles from p to each edge of the polygon
        for (const Edge& edge : polygon) {
            triangles.push_back(std::make_unique<Triangle>(edge.p1, edge.p2, p));
        }

        // Step 5: Create/update Region for p
        Region* region = findRegion(p);
        if (!region) {
            regions.push_back(std::make_unique<Region>(p));
            region = regions.back().get();
        }

        // Update region's triangles
        region->vertices.clear();
        for (auto& tri : triangles) {
            if (tri->hasVertex(p)) {
                region->vertices.push_back(tri.get());
            }
        }

        // Update regions for all other points that might be affected
        for (auto& r : regions) {
            if (r.get() != region) {
                r->vertices.clear();
                for (auto& tri : triangles) {
                    if (tri->hasVertex(r->seed)) {
                        r->vertices.push_back(tri.get());
                    }
                }
            }
        }
    }

    // Get triangulation (triangles not containing frame points)
    std::vector<Triangle*> triangulation() const {
        std::vector<Triangle*> result;
        for (const auto& tri : triangles) {
            if (!isFramePoint(tri->p1) && !isFramePoint(tri->p2) && !isFramePoint(tri->p3)) {
                result.push_back(tri.get());
            }
        }
        return result;
    }

    // Lloyd relaxation: move points toward region centroids
    void relax() {
        std::vector<Point> newPoints;

        for (const auto& region : regions) {
            Point centroid = region->center();
            newPoints.push_back(centroid);
        }

        // Clear current triangulation
        triangles.clear();
        regions.clear();
        points.clear();

        // Recreate initial frame triangulation
        triangles.push_back(std::make_unique<Triangle>(frame[0], frame[1], frame[2]));
        triangles.push_back(std::make_unique<Triangle>(frame[0], frame[2], frame[3]));

        // Re-add all points at their new positions
        for (const Point& p : newPoints) {
            addPoint(p);
        }
    }

    // Factory method: create Voronoi and add all points with relaxation
    static Voronoi build(const std::vector<Point>& vertices, float width, float height, int iterations = 0) {
        Voronoi voronoi(width, height);

        // Add all initial points
        for (const Point& p : vertices) {
            voronoi.addPoint(p);
        }

        // Apply Lloyd relaxation
        for (int i = 0; i < iterations; i++) {
            voronoi.relax();
        }

        return voronoi;
    }

    // Getters
    const std::vector<std::unique_ptr<Triangle>>& getTriangles() const { return triangles; }
    const std::vector<std::unique_ptr<Region>>& getRegions() const { return regions; }
    const std::vector<Point>& getPoints() const { return points; }
    const std::vector<Point>& getFrame() const { return frame; }
};

} // namespace geom
} // namespace towngenerator
