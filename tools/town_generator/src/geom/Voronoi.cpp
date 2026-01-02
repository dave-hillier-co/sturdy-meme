#include <town_generator/geom/Voronoi.h>
#include <cmath>

namespace town_generator {

// Triangle implementation
Triangle::Triangle(Point* p1, Point* p2, Point* p3) {
    float s = (p2->x - p1->x) * (p2->y + p1->y) +
              (p3->x - p2->x) * (p3->y + p2->y) +
              (p1->x - p3->x) * (p1->y + p3->y);

    this->p1 = p1;
    // CCW ordering
    this->p2 = s > 0 ? p2 : p3;
    this->p3 = s > 0 ? p3 : p2;

    float x1 = (p1->x + p2->x) / 2;
    float y1 = (p1->y + p2->y) / 2;
    float x2 = (p2->x + p3->x) / 2;
    float y2 = (p2->y + p3->y) / 2;

    float dx1 = p1->y - p2->y;
    float dy1 = p2->x - p1->x;
    float dx2 = p2->y - p3->y;
    float dy2 = p3->x - p2->x;

    float tg1 = dy1 / dx1;
    float t2 = ((y1 - y2) - (x1 - x2) * tg1) / (dy2 - dx2 * tg1);

    c = Point(x2 + dx2 * t2, y2 + dy2 * t2);
    r = c.distance(*p1);
}

// Region implementation
Region& Region::sortVertices() {
    std::sort(vertices.begin(), vertices.end(), [this](Triangle* v1, Triangle* v2) {
        return compareAngles(v1, v2) < 0;
    });
    return *this;
}

Point Region::center() const {
    Point c;
    for (auto* v : vertices) {
        c.addEq(v->c);
    }
    if (!vertices.empty()) {
        c.scaleEq(1.0f / vertices.size());
    }
    return c;
}

bool Region::borders(const Region& r) const {
    size_t len1 = vertices.size();
    size_t len2 = r.vertices.size();
    for (size_t i = 0; i < len1; i++) {
        auto it = std::find(r.vertices.begin(), r.vertices.end(), vertices[i]);
        if (it != r.vertices.end()) {
            size_t j = it - r.vertices.begin();
            return vertices[(i + 1) % len1] == r.vertices[(j + len2 - 1) % len2];
        }
    }
    return false;
}

int Region::compareAngles(Triangle* v1, Triangle* v2) const {
    float x1 = v1->c.x - seed->x;
    float y1 = v1->c.y - seed->y;
    float x2 = v2->c.x - seed->x;
    float y2 = v2->c.y - seed->y;

    if (x1 >= 0 && x2 < 0) return 1;
    if (x2 >= 0 && x1 < 0) return -1;
    if (x1 == 0 && x2 == 0) {
        return y2 > y1 ? 1 : -1;
    }

    return MathUtils::sign(x2 * y1 - x1 * y2);
}

// Voronoi implementation
Voronoi::Voronoi(float minx, float miny, float maxx, float maxy)
    : _regionsDirty(false) {

    _c1 = new Point(minx, miny);
    _c2 = new Point(minx, maxy);
    _c3 = new Point(maxx, miny);
    _c4 = new Point(maxx, maxy);

    frame = {_c1, _c2, _c3, _c4};
    points = {_c1, _c2, _c3, _c4};

    triangles.push_back(new Triangle(_c1, _c2, _c3));
    triangles.push_back(new Triangle(_c2, _c3, _c4));

    // Build initial regions
    for (auto* p : points) {
        _regions.emplace(p, buildRegion(p));
    }
}

Voronoi::~Voronoi() {
    for (auto* tri : triangles) {
        delete tri;
    }
    delete _c1;
    delete _c2;
    delete _c3;
    delete _c4;
    // Note: other points are not owned by Voronoi
}

void Voronoi::addPoint(Point* p) {
    std::vector<Triangle*> toSplit;
    for (auto* tr : triangles) {
        if (p->distance(tr->c) < tr->r) {
            toSplit.push_back(tr);
        }
    }

    if (!toSplit.empty()) {
        points.push_back(p);

        std::vector<Point*> a;
        std::vector<Point*> b;

        for (auto* t1 : toSplit) {
            bool e1 = true, e2 = true, e3 = true;
            for (auto* t2 : toSplit) {
                if (t2 != t1) {
                    // If triangles have a common edge, it goes in opposite directions
                    if (e1 && t2->hasEdge(t1->p2, t1->p1)) e1 = false;
                    if (e2 && t2->hasEdge(t1->p3, t1->p2)) e2 = false;
                    if (e3 && t2->hasEdge(t1->p1, t1->p3)) e3 = false;
                    if (!(e1 || e2 || e3)) break;
                }
            }
            if (e1) { a.push_back(t1->p1); b.push_back(t1->p2); }
            if (e2) { a.push_back(t1->p2); b.push_back(t1->p3); }
            if (e3) { a.push_back(t1->p3); b.push_back(t1->p1); }
        }

        size_t index = 0;
        size_t iterations = 0;
        size_t maxIterations = a.size() + 1;
        do {
            triangles.push_back(new Triangle(p, a[index], b[index]));
            auto it = std::find(a.begin(), a.end(), b[index]);
            index = (it != a.end()) ? (it - a.begin()) : 0;
            iterations++;
            if (iterations > maxIterations) break;  // Safeguard against infinite loop
        } while (index != 0);

        for (auto* tr : toSplit) {
            auto it = std::find(triangles.begin(), triangles.end(), tr);
            if (it != triangles.end()) {
                triangles.erase(it);
            }
            delete tr;
        }

        _regionsDirty = true;
    }
}

Region Voronoi::buildRegion(Point* p) {
    Region r(p);
    for (auto* tr : triangles) {
        if (tr->p1 == p || tr->p2 == p || tr->p3 == p) {
            r.vertices.push_back(tr);
        }
    }
    return r.sortVertices();
}

std::map<Point*, Region>& Voronoi::getRegions() {
    if (_regionsDirty) {
        _regions.clear();
        _regionsDirty = false;
        for (auto* p : points) {
            _regions.emplace(p, buildRegion(p));
        }
    }
    return _regions;
}

bool Voronoi::isReal(Triangle* tr) const {
    auto inFrame = [this](Point* p) {
        return std::find(frame.begin(), frame.end(), p) != frame.end();
    };
    return !(inFrame(tr->p1) || inFrame(tr->p2) || inFrame(tr->p3));
}

std::vector<Triangle*> Voronoi::triangulation() const {
    std::vector<Triangle*> result;
    for (auto* tr : triangles) {
        if (isReal(tr)) {
            result.push_back(tr);
        }
    }
    return result;
}

std::vector<Region> Voronoi::partitioning() {
    std::vector<Region> result;
    auto& regions = getRegions();

    for (auto* p : points) {
        auto it = regions.find(p);
        if (it == regions.end()) continue;

        Region& r = it->second;
        bool allReal = true;
        for (auto* v : r.vertices) {
            if (!isReal(v)) {
                allReal = false;
                break;
            }
        }

        if (allReal) {
            result.push_back(r);
        }
    }
    return result;
}

std::vector<Region> Voronoi::getNeighbours(const Region& r1) {
    std::vector<Region> result;
    auto& regions = getRegions();
    for (auto& [p, r2] : regions) {
        if (r1.borders(r2)) {
            result.push_back(r2);
        }
    }
    return result;
}

Voronoi* Voronoi::relax(Voronoi* voronoi, const std::vector<Point*>* toRelax) {
    auto regions = voronoi->partitioning();

    std::vector<Point*> newPoints;
    for (auto* p : voronoi->points) {
        if (std::find(voronoi->frame.begin(), voronoi->frame.end(), p) == voronoi->frame.end()) {
            newPoints.push_back(p);
        }
    }

    const std::vector<Point*>* relaxSet = toRelax ? toRelax : &voronoi->points;

    for (auto& r : regions) {
        if (std::find(relaxSet->begin(), relaxSet->end(), r.seed) != relaxSet->end()) {
            auto it = std::find(newPoints.begin(), newPoints.end(), r.seed);
            if (it != newPoints.end()) {
                newPoints.erase(it);
            }
            Point c = r.center();
            newPoints.push_back(new Point(c.x, c.y));  // Note: caller must manage this memory
        }
    }

    return build(newPoints);
}

Voronoi* Voronoi::build(const std::vector<Point*>& vertices) {
    float minx = 1e+10f;
    float miny = 1e+10f;
    float maxx = -1e+9f;
    float maxy = -1e+9f;

    for (auto* v : vertices) {
        if (v->x < minx) minx = v->x;
        if (v->y < miny) miny = v->y;
        if (v->x > maxx) maxx = v->x;
        if (v->y > maxy) maxy = v->y;
    }

    float dx = (maxx - minx) * 0.5f;
    float dy = (maxy - miny) * 0.5f;

    auto* voronoi = new Voronoi(minx - dx / 2, miny - dy / 2, maxx + dx / 2, maxy + dy / 2);
    for (auto* v : vertices) {
        voronoi->addPoint(v);
    }

    return voronoi;
}

} // namespace town_generator
