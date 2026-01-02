#include <town_generator/geom/Polygon.h>

namespace town_generator {

float Polygon::square() const {
    if (vertices.empty()) return 0;
    const Point& v1_init = vertices.back();
    const Point& v2_init = vertices[0];
    float s = v1_init.x * v2_init.y - v2_init.x * v1_init.y;
    for (size_t i = 1; i < vertices.size(); i++) {
        const Point& v1 = vertices[i - 1];
        const Point& v2 = vertices[i];
        s += (v1.x * v2.y - v2.x * v1.y);
    }
    return s * 0.5f;
}

float Polygon::perimeter() const {
    float len = 0.0f;
    forEdge([&len](const Point& v0, const Point& v1) {
        len += v0.distance(v1);
    });
    return len;
}

float Polygon::compactness() const {
    float p = perimeter();
    return 4.0f * static_cast<float>(M_PI) * square() / (p * p);
}

Point Polygon::center() const {
    Point c;
    for (const auto& v : vertices) {
        c.addEq(v);
    }
    if (!vertices.empty()) {
        c.scaleEq(1.0f / vertices.size());
    }
    return c;
}

Point Polygon::centroid() const {
    float x = 0.0f, y = 0.0f, a = 0.0f;
    forEdge([&x, &y, &a](const Point& v0, const Point& v1) {
        float f = GeomUtils::cross(v0.x, v0.y, v1.x, v1.y);
        a += f;
        x += (v0.x + v1.x) * f;
        y += (v0.y + v1.y) * f;
    });
    float s6 = 1.0f / (3.0f * a);
    return Point(s6 * x, s6 * y);
}

Rectangle Polygon::getBounds() const {
    if (vertices.empty()) return Rectangle();
    Rectangle rect(vertices[0].x, vertices[0].y);
    for (const auto& v : vertices) {
        rect.left = std::min(rect.left, v.x);
        rect.right = std::max(rect.right, v.x);
        rect.top = std::min(rect.top, v.y);
        rect.bottom = std::max(rect.bottom, v.y);
    }
    return rect;
}

void Polygon::forEdge(std::function<void(const Point&, const Point&)> f) const {
    size_t len = vertices.size();
    for (size_t i = 0; i < len; i++) {
        f(vertices[i], vertices[(i + 1) % len]);
    }
}

void Polygon::forSegment(std::function<void(const Point&, const Point&)> f) const {
    for (size_t i = 0; i + 1 < vertices.size(); i++) {
        f(vertices[i], vertices[i + 1]);
    }
}

void Polygon::offset(const Point& p) {
    for (auto& v : vertices) {
        v.x += p.x;
        v.y += p.y;
    }
}

void Polygon::rotate(float a) {
    float cosA = std::cos(a);
    float sinA = std::sin(a);
    for (auto& v : vertices) {
        float vx = v.x * cosA - v.y * sinA;
        float vy = v.y * cosA + v.x * sinA;
        v.x = vx;
        v.y = vy;
    }
}

bool Polygon::isConvexVertexi(int i) const {
    int len = static_cast<int>(vertices.size());
    const Point& v0 = vertices[(i + len - 1) % len];
    const Point& v1 = vertices[i];
    const Point& v2 = vertices[(i + 1) % len];
    return GeomUtils::cross(v1.x - v0.x, v1.y - v0.y, v2.x - v1.x, v2.y - v1.y) > 0;
}

bool Polygon::isConvexVertex(const Point& v1) const {
    Point v0 = prev(v1);
    Point v2 = next(v1);
    return GeomUtils::cross(v1.x - v0.x, v1.y - v0.y, v2.x - v1.x, v2.y - v1.y) > 0;
}

bool Polygon::isConvex() const {
    for (const auto& v : vertices) {
        if (!isConvexVertex(v)) return false;
    }
    return true;
}

Point Polygon::smoothVertexi(int i, float f) const {
    const Point& v = vertices[i];
    int len = static_cast<int>(vertices.size());
    const Point& prevV = vertices[(i + len - 1) % len];
    const Point& nextV = vertices[(i + 1) % len];
    return Point(
        (prevV.x + v.x * f + nextV.x) / (2.0f + f),
        (prevV.y + v.y * f + nextV.y) / (2.0f + f)
    );
}

Point Polygon::smoothVertex(const Point& v, float f) const {
    Point prevV = prev(v);
    Point nextV = next(v);
    return Point(prevV.x + v.x * f + nextV.x, prevV.y + v.y * f + nextV.y).scale(1.0f / (2.0f + f));
}

Polygon Polygon::smoothVertexEq(float f) const {
    Polygon result;
    int len = static_cast<int>(vertices.size());
    Point v1 = vertices[len - 1];
    Point v2 = vertices[0];
    for (int i = 0; i < len; i++) {
        Point v0 = v1;
        v1 = v2;
        v2 = vertices[(i + 1) % len];
        result.push(Point(
            (v0.x + v1.x * f + v2.x) / (2.0f + f),
            (v0.y + v1.y * f + v2.y) / (2.0f + f)
        ));
    }
    return result;
}

float Polygon::distance(const Point& p) const {
    if (vertices.empty()) return std::numeric_limits<float>::infinity();
    float d = vertices[0].distance(p);
    for (size_t i = 1; i < vertices.size(); i++) {
        float d1 = vertices[i].distance(p);
        if (d1 < d) d = d1;
    }
    return d;
}

Polygon Polygon::filterShort(float threshold) const {
    if (vertices.size() < 2) return Polygon(vertices);

    Polygon result;
    Point v0 = vertices[0];
    result.push(v0);

    size_t i = 1;
    while (i < vertices.size()) {
        Point v1 = vertices[i++];
        while (v0.distance(v1) < threshold && i < vertices.size()) {
            v1 = vertices[i++];
        }
        result.push(v1);
        v0 = v1;
    }
    return result;
}

void Polygon::inset(const Point& p1, float d) {
    int i1 = indexOf(p1);
    if (i1 == -1) return;

    int len = static_cast<int>(vertices.size());
    int i0 = (i1 > 0 ? i1 - 1 : len - 1);
    int i2 = (i1 < len - 1 ? i1 + 1 : 0);
    int i3 = (i2 < len - 1 ? i2 + 1 : 0);

    Point p0 = vertices[i0];
    Point p2 = vertices[i2];
    Point p3 = vertices[i3];

    Point v0 = p1.subtract(p0);
    Point v1 = p2.subtract(p1);
    Point v2 = p3.subtract(p2);

    float cosVal = v0.dot(v1) / v0.length() / v1.length();
    float z = v0.x * v1.y - v0.y * v1.x;
    float sinVal = std::sqrt(1.0f - cosVal * cosVal);
    float t = d / sinVal;
    if (z > 0) {
        t = std::min(t, v0.length() * 0.99f);
    } else {
        t = std::min(t, v1.length() * 0.5f);
    }
    t *= MathUtils::sign(z);
    vertices[i1] = p1.subtract(v0.norm(t));

    cosVal = v1.dot(v2) / v1.length() / v2.length();
    z = v1.x * v2.y - v1.y * v2.x;
    sinVal = std::sqrt(1.0f - cosVal * cosVal);
    t = d / sinVal;
    if (z > 0) {
        t = std::min(t, v2.length() * 0.99f);
    } else {
        t = std::min(t, v1.length() * 0.5f);
    }
    vertices[i2] = p2.add(v2.norm(t));
}

void Polygon::insetEq(float d) {
    for (size_t i = 0; i < vertices.size(); i++) {
        inset(vertices[i], d);
    }
}

Polygon Polygon::insetAll(const std::vector<float>& d) const {
    Polygon p(vertices);
    for (size_t i = 0; i < p.size() && i < d.size(); i++) {
        if (d[i] != 0) p.inset(p[i], d[i]);
    }
    return p;
}

Polygon Polygon::buffer(const std::vector<float>& d) const {
    // Creating a polygon (probably invalid) with offset edges
    Polygon q;
    size_t idx = 0;
    forEdge([&q, &d, &idx](const Point& v0, const Point& v1) {
        float dd = idx < d.size() ? d[idx++] : 0;
        if (dd == 0) {
            q.push(v0);
            q.push(v1);
        } else {
            Point v = v1.subtract(v0);
            Point n = v.rotate90().norm(dd);
            q.push(v0.add(n));
            q.push(v1.add(n));
        }
    });

    // Creating a valid polygon by dealing with self-intersection
    bool wasCut;
    size_t lastEdge = 0;
    do {
        wasCut = false;
        size_t n = q.size();

        for (size_t i = lastEdge; i + 2 < n; i++) {
            lastEdge = i;

            const Point& p11 = q[i];
            const Point& p12 = q[i + 1];
            float x1 = p11.x;
            float y1 = p11.y;
            float dx1 = p12.x - x1;
            float dy1 = p12.y - y1;

            size_t jEnd = (i > 0) ? n : n - 1;
            for (size_t j = i + 2; j < jEnd; j++) {
                const Point& p21 = q[j];
                const Point& p22 = (j < n - 1) ? q[j + 1] : q[0];
                float x2 = p21.x;
                float y2 = p21.y;
                float dx2 = p22.x - x2;
                float dy2 = p22.y - y2;

                auto intResult = GeomUtils::intersectLines(x1, y1, dx1, dy1, x2, y2, dx2, dy2);
                if (intResult && intResult->x > DELTA && intResult->x < 1 - DELTA &&
                    intResult->y > DELTA && intResult->y < 1 - DELTA) {
                    Point pn(x1 + dx1 * intResult->x, y1 + dy1 * intResult->x);
                    q.insert(j + 1, pn);
                    q.insert(i + 1, pn);
                    wasCut = true;
                    break;
                }
            }
            if (wasCut) break;
        }
    } while (wasCut);

    // Checking every part of the polygon to pick the biggest
    std::vector<size_t> regular;
    for (size_t i = 0; i < q.size(); i++) regular.push_back(i);

    Polygon bestPart;
    float bestPartSq = -std::numeric_limits<float>::infinity();

    while (!regular.empty()) {
        std::vector<size_t> indices;
        size_t start = regular[0];
        size_t current = start;

        do {
            indices.push_back(current);
            regular.erase(std::find(regular.begin(), regular.end(), current));

            size_t nextIdx = (current + 1) % q.size();
            const Point& v = q[nextIdx];
            int next1 = q.indexOf(v);
            if (next1 == static_cast<int>(nextIdx)) {
                next1 = q.lastIndexOf(v);
            }
            current = (next1 == -1) ? nextIdx : static_cast<size_t>(next1);
        } while (current != start && !regular.empty());

        Polygon p;
        for (size_t idx : indices) {
            p.push(q[idx]);
        }
        float s = p.square();
        if (s > bestPartSq) {
            bestPart = p;
            bestPartSq = s;
        }
    }

    return bestPart;
}

Polygon Polygon::bufferEq(float d) const {
    std::vector<float> distances(vertices.size(), d);
    return buffer(distances);
}

Polygon Polygon::shrink(const std::vector<float>& d) const {
    Polygon q(vertices);
    size_t idx = 0;
    forEdge([&q, &d, &idx](const Point& v1, const Point& v2) {
        float dd = idx < d.size() ? d[idx++] : 0;
        if (dd > 0) {
            Point v = v2.subtract(v1);
            Point n = v.rotate90().norm(dd);
            auto parts = q.cut(v1.add(n), v2.add(n), 0);
            if (!parts.empty()) q = parts[0];
        }
    });
    return q;
}

Polygon Polygon::shrinkEq(float d) const {
    std::vector<float> distances(vertices.size(), d);
    return shrink(distances);
}

Polygon Polygon::peel(const Point& v1, float d) const {
    int i1 = indexOf(v1);
    if (i1 == -1) return *this;

    int i2 = (i1 == static_cast<int>(vertices.size()) - 1) ? 0 : i1 + 1;
    const Point& v2 = vertices[i2];

    Point v = v2.subtract(v1);
    Point n = v.rotate90().norm(d);

    auto parts = cut(v1.add(n), v2.add(n), 0);
    return parts.empty() ? *this : parts[0];
}

void Polygon::simplify(int n) {
    int len = static_cast<int>(vertices.size());
    while (len > n) {
        int result = 0;
        float minVal = std::numeric_limits<float>::infinity();

        Point b = vertices[len - 1];
        Point c = vertices[0];
        for (int i = 0; i < len; i++) {
            Point a = b;
            b = c;
            c = vertices[(i + 1) % len];
            float measure = std::abs(a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
            if (measure < minVal) {
                result = i;
                minVal = measure;
            }
        }

        vertices.erase(vertices.begin() + result);
        len--;
    }
}

int Polygon::findEdge(const Point& a, const Point& b) const {
    int index = indexOf(a);
    if (index != -1 && vertices[(index + 1) % vertices.size()].equals(b)) {
        return index;
    }
    return -1;
}

Point Polygon::next(const Point& a) const {
    int idx = indexOf(a);
    if (idx == -1) return Point();
    return vertices[(idx + 1) % vertices.size()];
}

Point Polygon::prev(const Point& a) const {
    int idx = indexOf(a);
    if (idx == -1) return Point();
    return vertices[(idx + vertices.size() - 1) % vertices.size()];
}

Point Polygon::vector(const Point& v) const {
    return next(v).subtract(v);
}

Point Polygon::vectori(int i) const {
    int nextIdx = (i == static_cast<int>(vertices.size()) - 1) ? 0 : i + 1;
    return vertices[nextIdx].subtract(vertices[i]);
}

bool Polygon::borders(const Polygon& another) const {
    size_t len1 = vertices.size();
    size_t len2 = another.size();
    for (size_t i = 0; i < len1; i++) {
        int j = another.indexOf(vertices[i]);
        if (j != -1) {
            const Point& nextV = vertices[(i + 1) % len1];
            if (nextV.equals(another[(j + 1) % len2]) ||
                nextV.equals(another[(j + len2 - 1) % len2])) {
                return true;
            }
        }
    }
    return false;
}

std::vector<Polygon> Polygon::split(const Point& p1, const Point& p2) const {
    return spliti(indexOf(p1), indexOf(p2));
}

std::vector<Polygon> Polygon::spliti(int i1, int i2) const {
    if (i1 > i2) std::swap(i1, i2);

    Polygon half1(slice(i1, i2 + 1));

    std::vector<Point> half2Verts = slice(i2);
    std::vector<Point> beginning = slice(0, i1 + 1);
    half2Verts.insert(half2Verts.end(), beginning.begin(), beginning.end());
    Polygon half2(half2Verts);

    return {half1, half2};
}

std::vector<Polygon> Polygon::cut(const Point& p1, const Point& p2, float gap) const {
    float x1 = p1.x;
    float y1 = p1.y;
    float dx1 = p2.x - x1;
    float dy1 = p2.y - y1;

    size_t len = vertices.size();
    int edge1 = 0, edge2 = 0;
    float ratio1 = 0, ratio2 = 0;
    int count = 0;

    for (size_t i = 0; i < len; i++) {
        const Point& v0 = vertices[i];
        const Point& v1 = vertices[(i + 1) % len];

        float x2 = v0.x;
        float y2 = v0.y;
        float dx2 = v1.x - x2;
        float dy2 = v1.y - y2;

        auto t = GeomUtils::intersectLines(x1, y1, dx1, dy1, x2, y2, dx2, dy2);
        if (t && t->y >= 0 && t->y <= 1) {
            if (count == 0) {
                edge1 = static_cast<int>(i);
                ratio1 = t->x;
            } else if (count == 1) {
                edge2 = static_cast<int>(i);
                ratio2 = t->x;
            }
            count++;
        }
    }

    if (count == 2) {
        Point diff = p2.subtract(p1);
        Point point1 = p1.add(diff.scale(ratio1));
        Point point2 = p1.add(diff.scale(ratio2));

        Polygon half1(slice(edge1 + 1, edge2 + 1));
        half1.unshift(point1);
        half1.push(point2);

        std::vector<Point> half2Verts = slice(edge2 + 1);
        std::vector<Point> beginning = slice(0, edge1 + 1);
        half2Verts.insert(half2Verts.end(), beginning.begin(), beginning.end());
        Polygon half2(half2Verts);
        half2.unshift(point2);
        half2.push(point1);

        if (gap > 0) {
            half1 = half1.peel(point2, gap / 2);
            half2 = half2.peel(point1, gap / 2);
        }

        Point v = vectori(edge1);
        if (GeomUtils::cross(dx1, dy1, v.x, v.y) > 0) {
            return {half1, half2};
        } else {
            return {half2, half1};
        }
    }

    return {Polygon(vertices)};
}

std::vector<float> Polygon::interpolate(const Point& p) const {
    float sum = 0.0f;
    std::vector<float> dd;
    for (const auto& v : vertices) {
        float d = 1.0f / v.distance(p);
        sum += d;
        dd.push_back(d);
    }
    for (auto& d : dd) {
        d /= sum;
    }
    return dd;
}

Polygon Polygon::rect(float w, float h) {
    return Polygon({
        Point(-w / 2, -h / 2),
        Point(w / 2, -h / 2),
        Point(w / 2, h / 2),
        Point(-w / 2, h / 2)
    });
}

Polygon Polygon::regular(int n, float r) {
    Polygon p;
    for (int i = 0; i < n; i++) {
        float a = static_cast<float>(i) / n * static_cast<float>(M_PI) * 2;
        p.push(Point(r * std::cos(a), r * std::sin(a)));
    }
    return p;
}

Polygon Polygon::circle(float r) {
    return regular(16, r);
}

} // namespace town_generator
