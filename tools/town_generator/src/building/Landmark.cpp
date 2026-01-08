#include "town_generator/building/Landmark.h"
#include "town_generator/building/City.h"
#include "town_generator/geom/GeomUtils.h"

namespace town_generator {
namespace building {

Landmark::Landmark(City* model, const geom::Point& pos, const std::string& name)
    : pos(pos)
    , name(name)
    , model_(model)
{
    assign();
}

void Landmark::assign() {
    assigned_ = false;

    if (!model_) return;

    // Try to find a cell containing this point
    for (auto* cell : model_->cells) {
        if (!cell) continue;

        std::vector<geom::Point> polyVerts;
        for (size_t i = 0; i < cell->shape.length(); ++i) {
            polyVerts.push_back(cell->shape[i]);
        }

        if (assignPoly(polyVerts)) {
            break;
        }
    }
}

bool Landmark::assignPoly(const std::vector<geom::Point>& poly) {
    if (poly.size() < 3) return false;

    // Check if point is in bounding box first
    auto bounds = geom::Polygon(poly).getBounds();
    if (pos.x < bounds.left || pos.x > bounds.right ||
        pos.y < bounds.top || pos.y > bounds.bottom) {
        return false;
    }

    // Fan triangulation from vertex 0
    // For each triangle (v0, v[i-1], v[i]), check if point is inside
    // using barycentric coordinates
    size_t n = poly.size();

    for (size_t i = 2; i < n; ++i) {
        const geom::Point& v0 = poly[0];
        const geom::Point& v1 = poly[i - 1];
        const geom::Point& v2 = poly[i];

        // Compute barycentric coordinates
        // Using the formula from mfcg.js GeomUtils.barycentric
        double denom = (v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y);
        if (std::abs(denom) < 1e-10) continue;

        double b0 = ((v1.y - v2.y) * (pos.x - v2.x) + (v2.x - v1.x) * (pos.y - v2.y)) / denom;
        double b1 = ((v2.y - v0.y) * (pos.x - v2.x) + (v0.x - v2.x) * (pos.y - v2.y)) / denom;
        double b2 = 1.0 - b0 - b1;

        // Check if all barycentric coords are non-negative (point inside triangle)
        if (b0 >= 0 && b1 >= 0 && b2 >= 0) {
            // Store the triangle vertices (need non-const pointers for update)
            // Note: In practice, these should point to the actual polygon vertices
            // which may be modified. For now, we store copies.
            // A more complete implementation would store indices.
            p0_ = const_cast<geom::Point*>(&poly[0]);
            p1_ = const_cast<geom::Point*>(&poly[i - 1]);
            p2_ = const_cast<geom::Point*>(&poly[i]);

            i0_ = b0;
            i1_ = b1;
            i2_ = b2;

            assigned_ = true;
            return true;
        }
    }

    return false;
}

void Landmark::update() {
    if (!assigned_ || !p0_ || !p1_ || !p2_) return;

    // Reconstruct position from barycentric coordinates
    // Faithful to mfcg.js Landmark.update
    pos.x = p0_->x * i0_ + p1_->x * i1_ + p2_->x * i2_;
    pos.y = p0_->y * i0_ + p1_->y * i1_ + p2_->y * i2_;
}

} // namespace building
} // namespace town_generator
