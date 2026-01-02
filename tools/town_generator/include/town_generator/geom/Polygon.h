#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/GeomUtils.h>
#include <town_generator/utils/MathUtils.h>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <limits>

namespace town_generator {

struct Rectangle {
    float left = 0, top = 0, right = 0, bottom = 0;
    Rectangle() = default;
    Rectangle(float x, float y) : left(x), top(y), right(x), bottom(y) {}
    float width() const { return right - left; }
    float height() const { return bottom - top; }
};

class Polygon {
public:
    static constexpr float DELTA = 0.000001f;

    std::vector<Point> vertices;

    Polygon() = default;
    explicit Polygon(const std::vector<Point>& verts) : vertices(verts) {}

    // Array-like access
    size_t size() const { return vertices.size(); }
    size_t length() const { return vertices.size(); }
    bool empty() const { return vertices.empty(); }
    Point& operator[](size_t i) { return vertices[i]; }
    const Point& operator[](size_t i) const { return vertices[i]; }
    void push(const Point& p) { vertices.push_back(p); }
    void push_back(const Point& p) { vertices.push_back(p); }
    void insert(size_t index, const Point& p) { vertices.insert(vertices.begin() + index, p); }
    void unshift(const Point& p) { vertices.insert(vertices.begin(), p); }
    Point& last() { return vertices.back(); }
    const Point& last() const { return vertices.back(); }
    Point& first() { return vertices.front(); }
    const Point& first() const { return vertices.front(); }

    int indexOf(const Point& p) const {
        for (size_t i = 0; i < vertices.size(); i++) {
            if (vertices[i].equals(p)) return static_cast<int>(i);
        }
        return -1;
    }

    int lastIndexOf(const Point& p) const {
        for (int i = static_cast<int>(vertices.size()) - 1; i >= 0; i--) {
            if (vertices[i].equals(p)) return i;
        }
        return -1;
    }

    void remove(size_t index) {
        if (index < vertices.size()) {
            vertices.erase(vertices.begin() + index);
        }
    }

    void splice(size_t index, size_t count) {
        if (index < vertices.size()) {
            auto end = std::min(index + count, vertices.size());
            vertices.erase(vertices.begin() + index, vertices.begin() + end);
        }
    }

    std::vector<Point> slice(size_t start, size_t end) const {
        if (start >= vertices.size()) return {};
        end = std::min(end, vertices.size());
        return std::vector<Point>(vertices.begin() + start, vertices.begin() + end);
    }

    std::vector<Point> slice(size_t start) const {
        if (start >= vertices.size()) return {};
        return std::vector<Point>(vertices.begin() + start, vertices.end());
    }

    // Iterators
    auto begin() { return vertices.begin(); }
    auto end() { return vertices.end(); }
    auto begin() const { return vertices.begin(); }
    auto end() const { return vertices.end(); }

    // Properties
    float square() const;
    float perimeter() const;
    float compactness() const;
    Point center() const;
    Point centroid() const;
    Rectangle getBounds() const;

    bool contains(const Point& v) const { return indexOf(v) != -1; }

    void forEdge(std::function<void(const Point&, const Point&)> f) const;
    void forSegment(std::function<void(const Point&, const Point&)> f) const;

    void offset(const Point& p);
    void rotate(float a);

    bool isConvexVertexi(int i) const;
    bool isConvexVertex(const Point& v1) const;
    bool isConvex() const;

    Point smoothVertexi(int i, float f = 1.0f) const;
    Point smoothVertex(const Point& v, float f = 1.0f) const;
    Polygon smoothVertexEq(float f = 1.0f) const;

    float distance(const Point& p) const;

    Polygon filterShort(float threshold) const;

    void inset(const Point& p1, float d);
    void insetEq(float d);
    Polygon insetAll(const std::vector<float>& d) const;

    Polygon buffer(const std::vector<float>& d) const;
    Polygon bufferEq(float d) const;

    Polygon shrink(const std::vector<float>& d) const;
    Polygon shrinkEq(float d) const;

    Polygon peel(const Point& v1, float d) const;

    void simplify(int n);

    int findEdge(const Point& a, const Point& b) const;
    Point next(const Point& a) const;
    Point prev(const Point& a) const;
    Point vector(const Point& v) const;
    Point vectori(int i) const;

    bool borders(const Polygon& another) const;

    std::vector<Polygon> split(const Point& p1, const Point& p2) const;
    std::vector<Polygon> spliti(int i1, int i2) const;
    std::vector<Polygon> cut(const Point& p1, const Point& p2, float gap = 0) const;

    std::vector<float> interpolate(const Point& p) const;

    // Static factory methods
    static Polygon rect(float w = 1.0f, float h = 1.0f);
    static Polygon regular(int n = 8, float r = 1.0f);
    static Polygon circle(float r = 1.0f);
};

} // namespace town_generator
