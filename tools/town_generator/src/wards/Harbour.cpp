#include "town_generator/wards/Harbour.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/Patch.h"
#include "town_generator/utils/Random.h"
#include "town_generator/geom/GeomUtils.h"

namespace town_generator {
namespace wards {

void Harbour::createGeometry() {
    if (!patch || !model) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Find edges that border water (neighbors that are waterbody)
    std::vector<std::pair<geom::Point, geom::Point>> waterEdges;

    for (size_t i = 0; i < patch->shape.length(); ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % patch->shape.length()];

        // Check if this edge borders a water patch
        for (auto* neighbor : patch->neighbors) {
            if (neighbor->waterbody) {
                // Check if this edge is shared with the water neighbor
                if (neighbor->shape.contains(v0) && neighbor->shape.contains(v1)) {
                    waterEdges.push_back({v0, v1});
                    break;
                }
            }
        }
    }

    // Create pier structures along water edges
    piers.clear();
    for (const auto& edge : waterEdges) {
        double edgeLen = geom::Point::distance(edge.first, edge.second);
        int numPiers = static_cast<int>(edgeLen / 6.0);  // One pier every ~6 units

        if (numPiers < 1) numPiers = 1;

        geom::Point edgeDir = edge.second.subtract(edge.first);
        edgeDir = edgeDir.norm(1.0);
        geom::Point perpDir(-edgeDir.y, edgeDir.x);

        double spacing = edgeLen / (numPiers + 1);

        for (int p = 1; p <= numPiers; ++p) {
            double t = p * spacing / edgeLen;
            geom::Point pierBase = geom::GeomUtils::interpolate(edge.first, edge.second, t);

            // Pier dimensions
            double pierWidth = 1.0 + utils::Random::floatVal() * 0.5;
            double pierLength = 3.0 + utils::Random::floatVal() * 3.0;

            // Create pier polygon (rectangle extending into water)
            std::vector<geom::Point> pierVerts;
            pierVerts.push_back(pierBase.add(edgeDir.scale(-pierWidth / 2)));
            pierVerts.push_back(pierBase.add(edgeDir.scale(pierWidth / 2)));
            pierVerts.push_back(pierBase.add(edgeDir.scale(pierWidth / 2)).add(perpDir.scale(pierLength)));
            pierVerts.push_back(pierBase.add(edgeDir.scale(-pierWidth / 2)).add(perpDir.scale(pierLength)));

            piers.push_back(geom::Polygon(pierVerts));
        }
    }

    // Create warehouses/buildings in the non-water part
    // Use standard alley creation with harbour-specific parameters
    double minSq = 4 * (30 + 60 * utils::Random::floatVal() * utils::Random::floatVal());
    double gridChaos = 0.4 + utils::Random::floatVal() * 0.2;
    createAlleys(block, minSq, gridChaos, 0.5, 0.1, 1.0);

    // Add piers to geometry
    for (const auto& pier : piers) {
        geometry.push_back(pier);
    }
}

} // namespace wards
} // namespace town_generator
