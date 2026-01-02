#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/utils/MathUtils.h>
#include <vector>
#include <map>
#include <algorithm>

namespace town_generator {

class Triangle {
public:
    Point* p1;
    Point* p2;
    Point* p3;
    Point c;  // circumcenter
    float r;  // circumradius

    Triangle(Point* p1, Point* p2, Point* p3);

    bool hasEdge(Point* a, Point* b) const {
        return (p1 == a && p2 == b) ||
               (p2 == a && p3 == b) ||
               (p3 == a && p1 == b);
    }
};

class Region {
public:
    Point* seed;
    std::vector<Triangle*> vertices;

    explicit Region(Point* seed) : seed(seed) {}

    Region& sortVertices();
    Point center() const;
    bool borders(const Region& r) const;

    // Get the polygon shape of this region
    Polygon getPolygon() const {
        Polygon p;
        for (auto* tri : vertices) {
            p.push(tri->c);
        }
        return p;
    }

private:
    int compareAngles(Triangle* v1, Triangle* v2) const;
};

class Voronoi {
public:
    std::vector<Triangle*> triangles;
    std::vector<Point*> points;
    std::vector<Point*> frame;

    Voronoi(float minx, float miny, float maxx, float maxy);
    ~Voronoi();

    void addPoint(Point* p);

    std::map<Point*, Region>& getRegions();

    std::vector<Triangle*> triangulation() const;
    std::vector<Region> partitioning();
    std::vector<Region> getNeighbours(const Region& r1);

    static Voronoi* relax(Voronoi* voronoi, const std::vector<Point*>* toRelax = nullptr);
    static Voronoi* build(const std::vector<Point*>& vertices);

private:
    bool _regionsDirty;
    std::map<Point*, Region> _regions;

    // Owned corner points
    Point* _c1;
    Point* _c2;
    Point* _c3;
    Point* _c4;

    Region buildRegion(Point* p);
    bool isReal(Triangle* tr) const;
};

} // namespace town_generator
