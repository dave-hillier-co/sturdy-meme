#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/MathUtils.h"
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace town_generator {
namespace geom {

/**
 * Rectangle - Simple bounding rectangle
 */
struct Rectangle {
    double left, top, right, bottom;

    Rectangle() : left(0), top(0), right(0), bottom(0) {}
    Rectangle(double x, double y) : left(x), top(y), right(x), bottom(y) {}

    double width() const { return right - left; }
    double height() const { return bottom - top; }

    bool operator==(const Rectangle& other) const {
        return left == other.left && top == other.top &&
               right == other.right && bottom == other.bottom;
    }
    bool operator!=(const Rectangle& other) const { return !(*this == other); }
};

/**
 * Polygon - Core polygon class, faithful port from Haxe TownGeneratorOS
 * In Haxe this is abstract Polygon(Array<Point>) - here we wrap std::vector<Point>
 */
class Polygon {
private:
    static constexpr double DELTA = 0.000001;
    std::vector<Point> vertices_;

public:
    // Constructors
    Polygon() : vertices_() {}

    explicit Polygon(const std::vector<Point>& vertices) : vertices_(vertices) {}

    Polygon(std::initializer_list<Point> init) : vertices_(init) {}

    // Copy and assignment
    Polygon(const Polygon& other) : vertices_(other.vertices_) {}

    Polygon& operator=(const Polygon& other) {
        if (this != &other) {
            vertices_ = other.vertices_;
        }
        return *this;
    }

    // Move semantics
    Polygon(Polygon&& other) noexcept : vertices_(std::move(other.vertices_)) {}

    Polygon& operator=(Polygon&& other) noexcept {
        if (this != &other) {
            vertices_ = std::move(other.vertices_);
        }
        return *this;
    }

    // Equality
    bool operator==(const Polygon& other) const {
        return vertices_ == other.vertices_;
    }
    bool operator!=(const Polygon& other) const { return !(*this == other); }

    // Array-like access
    size_t length() const { return vertices_.size(); }
    size_t size() const { return vertices_.size(); }
    bool empty() const { return vertices_.empty(); }

    Point& operator[](size_t i) { return vertices_[i]; }
    const Point& operator[](size_t i) const { return vertices_[i]; }

    // Set all vertices from another polygon (Haxe's polygon.set())
    void set(const Polygon& p) {
        for (size_t i = 0; i < p.length() && i < vertices_.size(); ++i) {
            vertices_[i].set(p[i]);
        }
    }

    // Vector operations
    void push(const Point& p) { vertices_.push_back(p); }
    void push_back(const Point& p) { vertices_.push_back(p); }

    void unshift(const Point& p) { vertices_.insert(vertices_.begin(), p); }

    void insert(size_t index, const Point& p) {
        if (index >= vertices_.size()) {
            vertices_.push_back(p);
        } else {
            vertices_.insert(vertices_.begin() + index, p);
        }
    }

    void splice(size_t start, size_t deleteCount) {
        if (start < vertices_.size()) {
            auto end = std::min(start + deleteCount, vertices_.size());
            vertices_.erase(vertices_.begin() + start, vertices_.begin() + end);
        }
    }

    bool remove(const Point& p) {
        auto it = std::find(vertices_.begin(), vertices_.end(), p);
        if (it != vertices_.end()) {
            vertices_.erase(it);
            return true;
        }
        return false;
    }

    Point& last() { return vertices_.back(); }
    const Point& last() const { return vertices_.back(); }

    std::vector<Point> slice(size_t start) const {
        if (start >= vertices_.size()) return {};
        return std::vector<Point>(vertices_.begin() + start, vertices_.end());
    }

    std::vector<Point> slice(size_t start, size_t end) const {
        if (start >= vertices_.size()) return {};
        end = std::min(end, vertices_.size());
        return std::vector<Point>(vertices_.begin() + start, vertices_.begin() + end);
    }

    Polygon copy() const { return Polygon(vertices_); }

    // Iterator support
    auto begin() { return vertices_.begin(); }
    auto end() { return vertices_.end(); }
    auto begin() const { return vertices_.begin(); }
    auto end() const { return vertices_.end(); }

    // Find vertex index
    int indexOf(const Point& v) const {
        for (size_t i = 0; i < vertices_.size(); ++i) {
            if (vertices_[i] == v) return static_cast<int>(i);
        }
        return -1;
    }

    int lastIndexOf(const Point& v) const {
        for (int i = static_cast<int>(vertices_.size()) - 1; i >= 0; --i) {
            if (vertices_[i] == v) return i;
        }
        return -1;
    }

    bool contains(const Point& v) const {
        return indexOf(v) != -1;
    }

    // Computed properties
    double square() const {
        if (vertices_.size() < 3) return 0;

        const Point& v1_init = last();
        const Point& v2_init = vertices_[0];
        double s = v1_init.x * v2_init.y - v2_init.x * v1_init.y;

        for (size_t i = 1; i < vertices_.size(); ++i) {
            const Point& v1 = vertices_[i - 1];
            const Point& v2 = vertices_[i];
            s += (v1.x * v2.y - v2.x * v1.y);
        }
        return s * 0.5;
    }

    double perimeter() const {
        double len = 0.0;
        forEdge([&len](const Point& v0, const Point& v1) {
            len += Point::distance(v0, v1);
        });
        return len;
    }

    // Compactness: circle=1.0, square=0.79, triangle=0.60
    double compactness() const {
        double p = perimeter();
        return 4 * M_PI * square() / (p * p);
    }

    // Faster approximation of centroid
    Point center() const {
        Point c;
        for (const auto& v : vertices_) {
            c.addEq(v);
        }
        c.scaleEq(1.0 / vertices_.size());
        return c;
    }

    Point centroid() const {
        double x = 0.0, y = 0.0, a = 0.0;
        forEdge([&x, &y, &a](const Point& v0, const Point& v1) {
            double f = GeomUtils::cross(v0.x, v0.y, v1.x, v1.y);
            a += f;
            x += (v0.x + v1.x) * f;
            y += (v0.y + v1.y) * f;
        });
        double s6 = 1.0 / (3.0 * a);
        return Point(s6 * x, s6 * y);
    }

    // Iterate over edges
    void forEdge(std::function<void(const Point&, const Point&)> f) const {
        size_t len = vertices_.size();
        for (size_t i = 0; i < len; ++i) {
            f(vertices_[i], vertices_[(i + 1) % len]);
        }
    }

    // Similar to forEdge but doesn't iterate over last->first
    void forSegment(std::function<void(const Point&, const Point&)> f) const {
        for (size_t i = 0; i < vertices_.size() - 1; ++i) {
            f(vertices_[i], vertices_[i + 1]);
        }
    }

    // Offset all vertices
    void offset(const Point& p) {
        for (auto& v : vertices_) {
            v.offset(p.x, p.y);
        }
    }

    // Rotate all vertices around origin
    void rotate(double a) {
        double cosA = std::cos(a);
        double sinA = std::sin(a);
        for (auto& v : vertices_) {
            double vx = v.x * cosA - v.y * sinA;
            double vy = v.y * cosA + v.x * sinA;
            v.setTo(vx, vy);
        }
    }

    // Check if vertex at index is convex
    bool isConvexVertexi(int i) const {
        int len = static_cast<int>(vertices_.size());
        const Point& v0 = vertices_[(i + len - 1) % len];
        const Point& v1 = vertices_[i];
        const Point& v2 = vertices_[(i + 1) % len];
        return GeomUtils::cross(v1.x - v0.x, v1.y - v0.y, v2.x - v1.x, v2.y - v1.y) > 0;
    }

    bool isConvexVertex(const Point& v1) const {
        const Point& v0 = prev(v1);
        const Point& v2 = next(v1);
        return GeomUtils::cross(v1.x - v0.x, v1.y - v0.y, v2.x - v1.x, v2.y - v1.y) > 0;
    }

    bool isConvex() const {
        for (const auto& v : vertices_) {
            if (!isConvexVertex(v)) return false;
        }
        return true;
    }

    // Smooth vertex at index
    Point smoothVertexi(int i, double f = 1.0) const {
        const Point& v = vertices_[i];
        int len = static_cast<int>(vertices_.size());
        const Point& prevV = vertices_[(i + len - 1) % len];
        const Point& nextV = vertices_[(i + 1) % len];
        return Point(
            (prevV.x + v.x * f + nextV.x) / (2 + f),
            (prevV.y + v.y * f + nextV.y) / (2 + f)
        );
    }

    Point smoothVertex(const Point& v, double f = 1.0) const {
        const Point& prevV = prev(v);
        const Point& nextV = next(v);
        return Point(
            prevV.x + v.x * f + nextV.x,
            prevV.y + v.y * f + nextV.y
        ).scale(1.0 / (2 + f));
    }

    // Smooth all vertices
    Polygon smoothVertexEq(double f = 1.0) const {
        size_t len = vertices_.size();
        std::vector<Point> result;
        result.reserve(len);

        Point v1 = vertices_[len - 1];
        Point v2 = vertices_[0];

        for (size_t i = 0; i < len; ++i) {
            Point v0 = v1;
            v1 = v2;
            v2 = vertices_[(i + 1) % len];
            result.emplace_back(
                (v0.x + v1.x * f + v2.x) / (2 + f),
                (v0.y + v1.y * f + v2.y) / (2 + f)
            );
        }
        return Polygon(result);
    }

    // Filter out short edges
    Polygon filterShort(double threshold) const {
        if (vertices_.empty()) return Polygon();

        std::vector<Point> result;
        result.push_back(vertices_[0]);

        size_t i = 1;
        Point v0 = vertices_[0];

        while (i < vertices_.size()) {
            Point v1;
            do {
                v1 = vertices_[i++];
            } while (Point::distance(v0, v1) < threshold && i < vertices_.size());
            result.push_back(v1);
            v0 = v1;
        }

        return Polygon(result);
    }

    // Minimal distance from any vertex to a point
    double distance(const Point& p) const {
        if (vertices_.empty()) return std::numeric_limits<double>::infinity();

        double d = Point::distance(vertices_[0], p);
        for (size_t i = 1; i < vertices_.size(); ++i) {
            double d1 = Point::distance(vertices_[i], p);
            if (d1 < d) d = d1;
        }
        return d;
    }

    // Find edge index starting at vertex a going to b
    int findEdge(const Point& a, const Point& b) const {
        int index = indexOf(a);
        if (index != -1 && vertices_[(index + 1) % vertices_.size()] == b) {
            return index;
        }
        return -1;
    }

    // Get next/prev vertex
    const Point& next(const Point& a) const {
        return vertices_[(indexOf(a) + 1) % vertices_.size()];
    }

    const Point& prev(const Point& a) const {
        int idx = indexOf(a);
        return vertices_[(idx + vertices_.size() - 1) % vertices_.size()];
    }

    // Vector from vertex to next vertex
    Point vector(const Point& v) const {
        return next(v).subtract(v);
    }

    Point vectori(int i) const {
        int nextIdx = (i == static_cast<int>(vertices_.size()) - 1) ? 0 : i + 1;
        return vertices_[nextIdx].subtract(vertices_[i]);
    }

    // Check if this polygon borders another
    bool borders(const Polygon& another) const {
        size_t len1 = vertices_.size();
        size_t len2 = another.length();

        for (size_t i = 0; i < len1; ++i) {
            int j = another.indexOf(vertices_[i]);
            if (j != -1) {
                const Point& nextP = vertices_[(i + 1) % len1];
                if (nextP == another[(j + 1) % len2] ||
                    nextP == another[(j + len2 - 1) % len2]) {
                    return true;
                }
            }
        }
        return false;
    }

    // Get bounding rectangle
    Rectangle getBounds() const {
        if (vertices_.empty()) return Rectangle();

        Rectangle rect(vertices_[0].x, vertices_[0].y);
        for (const auto& v : vertices_) {
            rect.left = std::min(rect.left, v.x);
            rect.right = std::max(rect.right, v.x);
            rect.top = std::min(rect.top, v.y);
            rect.bottom = std::max(rect.bottom, v.y);
        }
        return rect;
    }

    // Split polygon at two vertices
    std::vector<Polygon> split(const Point& p1, const Point& p2) const {
        return spliti(indexOf(p1), indexOf(p2));
    }

    std::vector<Polygon> spliti(int i1, int i2) const {
        if (i1 > i2) std::swap(i1, i2);

        std::vector<Polygon> result;

        // First half: i1 to i2+1
        result.emplace_back(slice(i1, i2 + 1));

        // Second half: i2 to end + 0 to i1+1
        auto second = slice(i2);
        auto first = slice(0, i1 + 1);
        second.insert(second.end(), first.begin(), first.end());
        result.emplace_back(second);

        return result;
    }

    // Cut polygon with a line
    std::vector<Polygon> cut(const Point& p1, const Point& p2, double gap = 0.0) const;

    // Inset one edge
    void inset(const Point& p1, double d);

    // Inset all edges
    void insetEq(double d) {
        for (size_t i = 0; i < vertices_.size(); ++i) {
            inset(vertices_[i], d);
        }
    }

    // Buffer (offset) polygon
    Polygon buffer(const std::vector<double>& d) const;

    Polygon bufferEq(double d) const {
        std::vector<double> distances(vertices_.size(), d);
        return buffer(distances);
    }

    // Shrink polygon
    Polygon shrink(const std::vector<double>& d) const;

    Polygon shrinkEq(double d) const {
        std::vector<double> distances(vertices_.size(), d);
        return shrink(distances);
    }

    // Peel one edge
    Polygon peel(const Point& v1, double d) const;

    // Simplify polygon to n vertices
    void simplify(int n);

    // Interpolation weights for a point
    std::vector<double> interpolate(const Point& p) const {
        double sum = 0.0;
        std::vector<double> dd;
        dd.reserve(vertices_.size());

        for (const auto& v : vertices_) {
            double d = 1.0 / Point::distance(v, p);
            sum += d;
            dd.push_back(d);
        }

        for (auto& d : dd) {
            d /= sum;
        }
        return dd;
    }

    // Filter vertices
    Polygon filter(std::function<bool(const Point&)> pred) const {
        std::vector<Point> result;
        for (const auto& v : vertices_) {
            if (pred(v)) result.push_back(v);
        }
        return Polygon(result);
    }

    // Find min/max by function
    template<typename F>
    const Point& min(F f) const {
        size_t resultIdx = 0;
        double minVal = f(vertices_[0]);
        for (size_t i = 1; i < vertices_.size(); ++i) {
            double val = f(vertices_[i]);
            if (val < minVal) {
                resultIdx = i;
                minVal = val;
            }
        }
        return vertices_[resultIdx];
    }

    template<typename F>
    const Point& max(F f) const {
        size_t resultIdx = 0;
        double maxVal = f(vertices_[0]);
        for (size_t i = 1; i < vertices_.size(); ++i) {
            double val = f(vertices_[i]);
            if (val > maxVal) {
                resultIdx = i;
                maxVal = val;
            }
        }
        return vertices_[resultIdx];
    }

    // Count vertices matching predicate
    int count(std::function<bool(const Point&)> test) const {
        int cnt = 0;
        for (const auto& v : vertices_) {
            if (test(v)) cnt++;
        }
        return cnt;
    }

    // Static factory methods
    static Polygon rect(double w = 1.0, double h = 1.0) {
        return Polygon({
            Point(-w / 2, -h / 2),
            Point(w / 2, -h / 2),
            Point(w / 2, h / 2),
            Point(-w / 2, h / 2)
        });
    }

    static Polygon regular(int n = 8, double r = 1.0) {
        std::vector<Point> points;
        points.reserve(n);
        for (int i = 0; i < n; ++i) {
            double a = static_cast<double>(i) / n * M_PI * 2;
            points.emplace_back(r * std::cos(a), r * std::sin(a));
        }
        return Polygon(points);
    }

    static Polygon circle(double r = 1.0) {
        return regular(16, r);
    }

    // Access underlying vector
    std::vector<Point>& vertices() { return vertices_; }
    const std::vector<Point>& vertices() const { return vertices_; }
};

} // namespace geom
} // namespace town_generator
