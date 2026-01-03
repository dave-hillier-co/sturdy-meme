#pragma once

#include "Point.h"
#include "GeomUtils.h"
#include <vector>
#include <functional>
#include <optional>
#include <array>
#include <cmath>
#include <algorithm>
#include <random>

namespace towngenerator {
namespace geom {

class Polygon {
public:
    // Bounds structure for getBounds()
    struct Bounds {
        float x, y, width, height;
        Bounds(float x = 0, float y = 0, float w = 0, float h = 0)
            : x(x), y(y), width(w), height(h) {}
    };

    std::vector<Point> vertices;

    // Constructors
    Polygon() = default;
    explicit Polygon(const std::vector<Point>& verts) : vertices(verts) {}
    explicit Polygon(std::vector<Point>&& verts) : vertices(std::move(verts)) {}

    // ====================
    // PROPERTIES
    // ====================

    // Area using shoelace formula
    float square() const {
        if (vertices.size() < 3) return 0.0f;

        float area = 0.0f;
        size_t n = vertices.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            area += vertices[i].x * vertices[j].y;
            area -= vertices[j].x * vertices[i].y;
        }
        return std::abs(area) * 0.5f;
    }

    // Perimeter - sum of edge lengths
    float perimeter() const {
        if (vertices.size() < 2) return 0.0f;

        float perim = 0.0f;
        size_t n = vertices.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            perim += Point::distance(vertices[i], vertices[j]);
        }
        return perim;
    }

    // Compactness - 4*PI*area / perimeter^2
    float compactness() const {
        float p = perimeter();
        if (p < 1e-10f) return 0.0f;
        return (4.0f * M_PI * square()) / (p * p);
    }

    // Center - simple average of vertices
    Point center() const {
        if (vertices.empty()) return Point(0, 0);

        float sumX = 0.0f, sumY = 0.0f;
        for (const auto& v : vertices) {
            sumX += v.x;
            sumY += v.y;
        }
        return Point(sumX / vertices.size(), sumY / vertices.size());
    }

    // Centroid - area-weighted center
    Point centroid() const {
        if (vertices.size() < 3) return center();

        float cx = 0.0f, cy = 0.0f, area = 0.0f;
        size_t n = vertices.size();

        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            float cross = vertices[i].x * vertices[j].y - vertices[j].x * vertices[i].y;
            area += cross;
            cx += (vertices[i].x + vertices[j].x) * cross;
            cy += (vertices[i].y + vertices[j].y) * cross;
        }

        area *= 0.5f;
        if (std::abs(area) < 1e-10f) return center();

        cx /= (6.0f * area);
        cy /= (6.0f * area);
        return Point(cx, cy);
    }

    // ====================
    // VERTEX OPERATIONS
    // ====================

    // Indexed access
    Point& operator[](size_t index) {
        return vertices[index];
    }

    const Point& operator[](size_t index) const {
        return vertices[index];
    }

    // Vertex count
    size_t length() const {
        return vertices.size();
    }

    size_t size() const {
        return vertices.size();
    }

    // Test if vertex forms convex angle
    bool isConvexVertex(size_t v) const {
        size_t n = vertices.size();
        if (n < 3 || v >= n) return false;

        size_t prev_idx = (v + n - 1) % n;
        size_t next_idx = (v + 1) % n;

        Point v1 = vertices[v].subtract(vertices[prev_idx]);
        Point v2 = vertices[next_idx].subtract(vertices[v]);

        // Cross product - positive for convex (counter-clockwise)
        return GeomUtils::cross(v1.x, v1.y, v2.x, v2.y) > 0;
    }

    // Smooth vertex with neighbors (factor f)
    Point smoothVertex(size_t v, float f) const {
        size_t n = vertices.size();
        if (n < 3 || v >= n) return vertices[v];

        size_t prev_idx = (v + n - 1) % n;
        size_t next_idx = (v + 1) % n;

        Point avg = GeomUtils::interpolate(vertices[prev_idx], vertices[next_idx], 0.5f);
        return GeomUtils::interpolate(vertices[v], avg, f);
    }

    // Remove vertices closer than threshold
    void filterShort(float threshold) {
        if (vertices.size() < 3) return;

        std::vector<Point> filtered;
        filtered.reserve(vertices.size());

        for (size_t i = 0; i < vertices.size(); ++i) {
            size_t next_idx = (i + 1) % vertices.size();
            float dist = Point::distance(vertices[i], vertices[next_idx]);

            if (dist >= threshold) {
                filtered.push_back(vertices[i]);
            }
        }

        if (filtered.size() >= 3) {
            vertices = std::move(filtered);
        }
    }

    // Iterate edges with callback
    void forEdge(const std::function<void(const Point&, const Point&)>& callback) const {
        size_t n = vertices.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            callback(vertices[i], vertices[j]);
        }
    }

    // Find edge index or -1
    // Uses epsilon-based comparison for geometric matching
    int findEdge(const Point& v0, const Point& v1, float epsilon = EPSILON) const {
        size_t n = vertices.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;

            // Check if edge matches in either direction using geometric equality
            bool forward = vertices[i].equals(v0, epsilon) && vertices[j].equals(v1, epsilon);
            bool backward = vertices[i].equals(v1, epsilon) && vertices[j].equals(v0, epsilon);

            if (forward || backward) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Vector from vertex to next vertex
    Point vector(size_t v) const {
        size_t n = vertices.size();
        if (n == 0 || v >= n) return Point(0, 0);

        size_t next_idx = (v + 1) % n;
        return vertices[next_idx].subtract(vertices[v]);
    }

    // Get next vertex index
    size_t next(size_t v) const {
        return (v + 1) % vertices.size();
    }

    // Get previous vertex index
    size_t prev(size_t v) const {
        size_t n = vertices.size();
        return (v + n - 1) % n;
    }

    // ====================
    // POLYGON MANIPULATION
    // ====================

    // Translate all vertices
    void offset(float dx, float dy) {
        for (auto& v : vertices) {
            v.x += dx;
            v.y += dy;
        }
    }

    // Rotate around origin
    void rotate(float angle) {
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        for (auto& v : vertices) {
            float newX = v.x * cosA - v.y * sinA;
            float newY = v.x * sinA + v.y * cosA;
            v.x = newX;
            v.y = newY;
        }
    }

    // Inset each edge by array of distances
    Polygon shrink(const std::vector<float>& distances) const {
        size_t n = vertices.size();
        if (n < 3 || distances.size() != n) return *this;

        std::vector<Point> newVertices;
        newVertices.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            size_t prev_idx = (i + n - 1) % n;
            size_t next_idx = (i + 1) % n;

            // Get edge vectors
            Point e1 = vertices[i].subtract(vertices[prev_idx]);
            Point e2 = vertices[next_idx].subtract(vertices[i]);

            // Normalize and get perpendiculars (inward normals)
            Point n1 = e1.normalize().rotate90();
            Point n2 = e2.normalize().rotate90();

            // Offset positions
            Point p1 = vertices[i].add(n1.scale(distances[i]));
            Point p2 = vertices[i].add(n2.scale(distances[i]));

            // Find intersection of offset edges
            auto intersection = GeomUtils::intersectLines(p1, e1, p2, e2);

            if (intersection.has_value()) {
                Point result = p1.add(e1.scale(intersection->x));
                newVertices.push_back(result);
            } else {
                newVertices.push_back(vertices[i]);
            }
        }

        return Polygon(newVertices);
    }

    // Inset all edges uniformly
    Polygon shrinkEq(float distance) const {
        std::vector<float> distances(vertices.size(), distance);
        return shrink(distances);
    }

    // Buffer - like shrink but handles self-intersection
    // Simplified version - for full implementation would need polygon clipping library
    Polygon buffer(const std::vector<float>& distances) const {
        // For now, use shrink as a simple approximation
        // A full implementation would use a polygon clipping library like Clipper
        return shrink(distances);
    }

    // Cut polygon with line, returns two polygons
    std::pair<Polygon, Polygon> cut(const Point& p1, const Point& p2) const {
        // This is a complex operation that requires:
        // 1. Find intersection points with polygon edges
        // 2. Split the polygon at these points
        // For a simple implementation, return empty polygons
        // Full implementation would need robust line-polygon intersection

        Polygon empty1, empty2;
        return {empty1, empty2};
    }

    // Split polygon at two vertex indices
    std::pair<Polygon, Polygon> split(size_t i, size_t j) const {
        size_t n = vertices.size();
        if (n < 3 || i >= n || j >= n || i == j) {
            return {*this, Polygon()};
        }

        // Ensure i < j
        if (i > j) std::swap(i, j);

        // First polygon: from i to j
        std::vector<Point> verts1;
        for (size_t k = i; k <= j; ++k) {
            verts1.push_back(vertices[k]);
        }

        // Second polygon: from j to i (wrapping)
        std::vector<Point> verts2;
        for (size_t k = j; k < n; ++k) {
            verts2.push_back(vertices[k]);
        }
        for (size_t k = 0; k <= i; ++k) {
            verts2.push_back(vertices[k]);
        }

        return {Polygon(verts1), Polygon(verts2)};
    }

    // Split polygon at two vertex indices (alternative version)
    std::pair<Polygon, Polygon> spliti(size_t i, size_t j) const {
        return split(i, j);
    }

    // ====================
    // STATIC CONSTRUCTORS
    // ====================

    // Create rectangle
    static Polygon rect(float x, float y, float w, float h) {
        std::vector<Point> verts = {
            Point(x, y),
            Point(x + w, y),
            Point(x + w, y + h),
            Point(x, y + h)
        };
        return Polygon(verts);
    }

    // Create regular polygon
    static Polygon regular(int sides, float radius) {
        std::vector<Point> verts;
        verts.reserve(sides);

        float angleStep = 2.0f * M_PI / sides;
        for (int i = 0; i < sides; ++i) {
            float angle = i * angleStep;
            verts.emplace_back(
                radius * std::cos(angle),
                radius * std::sin(angle)
            );
        }

        return Polygon(verts);
    }

    // Create circle approximation (16-sided)
    static Polygon circle(float radius) {
        return regular(16, radius);
    }

    // ====================
    // QUERIES
    // ====================

    // Get bounding rectangle
    Bounds getBounds() const {
        if (vertices.empty()) return Bounds();

        float minX = vertices[0].x;
        float maxX = vertices[0].x;
        float minY = vertices[0].y;
        float maxY = vertices[0].y;

        for (const auto& v : vertices) {
            minX = std::min(minX, v.x);
            maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y);
            maxY = std::max(maxY, v.y);
        }

        return Bounds(minX, minY, maxX - minX, maxY - minY);
    }

    // Minimum distance from point to any vertex
    float distance(const Point& point) const {
        if (vertices.empty()) return 0.0f;

        float minDist = Point::distance(vertices[0], point);
        for (size_t i = 1; i < vertices.size(); ++i) {
            float dist = Point::distance(vertices[i], point);
            minDist = std::min(minDist, dist);
        }
        return minDist;
    }

    // Check if polygons share an edge
    bool borders(const Polygon& other) const {
        for (size_t i = 0; i < vertices.size(); ++i) {
            size_t j = (i + 1) % vertices.size();
            if (other.findEdge(vertices[i], vertices[j]) >= 0) {
                return true;
            }
        }
        return false;
    }

    // Inverse distance weighting interpolation
    float interpolate(const Point& point) const {
        if (vertices.empty()) return 0.0f;

        float weightSum = 0.0f;
        float valueSum = 0.0f;

        for (const auto& v : vertices) {
            float dist = Point::distance(v, point);
            if (dist < 1e-10f) {
                // Point is on a vertex, return 1.0
                return 1.0f;
            }

            float weight = 1.0f / (dist * dist);
            weightSum += weight;
            valueSum += weight; // Using 1.0 as the value for each vertex
        }

        return weightSum > 0 ? valueSum / weightSum : 0.0f;
    }

    // Point-in-polygon test (ray casting algorithm)
    bool contains(const Point& point) const {
        if (vertices.size() < 3) return false;

        bool inside = false;
        size_t n = vertices.size();

        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            float xi = vertices[i].x, yi = vertices[i].y;
            float xj = vertices[j].x, yj = vertices[j].y;

            bool intersect = ((yi > point.y) != (yj > point.y)) &&
                           (point.x < (xj - xi) * (point.y - yi) / (yj - yi) + xi);
            if (intersect) inside = !inside;
        }

        return inside;
    }

    // Test if polygon is convex
    bool isConvex() const {
        if (vertices.size() < 3) return false;

        bool hasPositive = false;
        bool hasNegative = false;
        size_t n = vertices.size();

        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            size_t k = (i + 2) % n;

            Point v1 = vertices[j].subtract(vertices[i]);
            Point v2 = vertices[k].subtract(vertices[j]);

            float cross = GeomUtils::cross(v1.x, v1.y, v2.x, v2.y);

            if (cross > 0) hasPositive = true;
            if (cross < 0) hasNegative = true;

            if (hasPositive && hasNegative) return false;
        }

        return true;
    }

    // Random point from vertices
    Point random() const {
        if (vertices.empty()) return Point(0, 0);

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, vertices.size() - 1);

        return vertices[dis(gen)];
    }

    // Find min vertex by function
    Point min(const std::function<float(const Point&)>& fn) const {
        if (vertices.empty()) return Point(0, 0);

        Point minVertex = vertices[0];
        float minValue = fn(vertices[0]);

        for (size_t i = 1; i < vertices.size(); ++i) {
            float value = fn(vertices[i]);
            if (value < minValue) {
                minValue = value;
                minVertex = vertices[i];
            }
        }

        return minVertex;
    }

    // Find max vertex by function
    Point max(const std::function<float(const Point&)>& fn) const {
        if (vertices.empty()) return Point(0, 0);

        Point maxVertex = vertices[0];
        float maxValue = fn(vertices[0]);

        for (size_t i = 1; i < vertices.size(); ++i) {
            float value = fn(vertices[i]);
            if (value > maxValue) {
                maxValue = value;
                maxVertex = vertices[i];
            }
        }

        return maxVertex;
    }
};

} // namespace geom
} // namespace towngenerator
