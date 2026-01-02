#include <town_generator/building/Cutter.h>

namespace town_generator {

std::vector<Polygon> Cutter::bisect(Polygon& poly, const Point& vertex,
                                     float ratio, float angle, float gap) {
    Point next = poly.next(vertex);
    Point p1 = GeomUtils::interpolate(vertex, next, ratio);
    Point d = next.subtract(vertex);

    float cosB = std::cos(angle);
    float sinB = std::sin(angle);
    float vx = d.x * cosB - d.y * sinB;
    float vy = d.y * cosB + d.x * sinB;
    Point p2(p1.x - vy, p1.y + vx);

    return poly.cut(p1, p2, gap);
}

std::vector<Polygon> Cutter::radial(const Polygon& poly, const Point* center, float gap) {
    Point c = center ? *center : poly.centroid();

    std::vector<Polygon> sectors;
    poly.forEdge([&sectors, &c, gap](const Point& v0, const Point& v1) {
        Polygon sector({c, v0, v1});
        if (gap > 0) {
            sector = sector.shrink({gap / 2, 0, gap / 2});
        }
        sectors.push_back(sector);
    });
    return sectors;
}

std::vector<Polygon> Cutter::semiRadial(const Polygon& poly, const Point* centerPtr, float gap) {
    Point center;
    if (centerPtr) {
        center = *centerPtr;
    } else {
        Point centroid = poly.centroid();
        float minDist = std::numeric_limits<float>::infinity();
        for (const auto& v : poly.vertices) {
            float dist = v.distance(centroid);
            if (dist < minDist) {
                minDist = dist;
                center = v;
            }
        }
    }

    float halfGap = gap / 2;

    std::vector<Polygon> sectors;
    poly.forEdge([&sectors, &poly, &center, halfGap](const Point& v0, const Point& v1) {
        if (!v0.equals(center) && !v1.equals(center)) {
            Polygon sector({center, v0, v1});
            if (halfGap > 0) {
                std::vector<float> d = {
                    poly.findEdge(center, v0) == -1 ? halfGap : 0.0f,
                    0.0f,
                    poly.findEdge(v1, center) == -1 ? halfGap : 0.0f
                };
                sector = sector.shrink(d);
            }
            sectors.push_back(sector);
        }
    });
    return sectors;
}

std::vector<Polygon> Cutter::ring(const Polygon& poly, float thickness) {
    struct Slice {
        Point p1, p2;
        float len;
    };

    std::vector<Slice> slices;
    poly.forEdge([&slices, thickness](const Point& v1, const Point& v2) {
        Point v = v2.subtract(v1);
        Point n = v.rotate90().norm(thickness);
        slices.push_back({v1.add(n), v2.add(n), v.length()});
    });

    // Short sides should be sliced first
    std::sort(slices.begin(), slices.end(), [](const Slice& s1, const Slice& s2) {
        return s1.len < s2.len;
    });

    std::vector<Polygon> peel;
    Polygon p = poly;

    for (const auto& slice : slices) {
        auto halves = p.cut(slice.p1, slice.p2);
        p = halves[0];
        if (halves.size() == 2) {
            peel.push_back(halves[1]);
        }
    }

    return peel;
}

} // namespace town_generator
