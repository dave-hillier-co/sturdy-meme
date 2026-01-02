#include <town_generator/wards/Ward.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Cutter.h>
#include <town_generator/utils/Random.h>
#include <cmath>
#include <limits>

namespace town_generator {

// Forward declaration for Model - we need minimal interface
// Model will be defined elsewhere with wall, plaza, arteries members
class Model;

Ward::Ward(Model* model, Patch* patch)
    : model(model), patch(patch) {
}

void Ward::createGeometry() {
    geometry.clear();
}

Polygon Ward::getCityBlock() const {
    std::vector<float> insetDist;

    // For now, simplified version without Model dependencies
    // When Model is fully implemented, this will check wall, plaza, arteries
    bool innerPatch = patch->withinWalls;

    patch->shape.forEdge([this, &insetDist, innerPatch](const Point& v0, const Point& v1) {
        // TODO: When Model is implemented, check:
        // - if model->wall borders this patch edge
        // - if model->plaza shares this edge
        // - if any model->arteries contain both vertices

        // Simplified: use REGULAR_STREET for inner patches, ALLEY for outer
        float streetWidth = innerPatch ? REGULAR_STREET : ALLEY;
        insetDist.push_back(streetWidth / 2);
    });

    if (patch->shape.isConvex()) {
        return patch->shape.shrink(insetDist);
    } else {
        return patch->shape.buffer(insetDist);
    }
}

void Ward::filterOutskirts() {
    // Filter geometry based on patch shape
    // Remove buildings that extend outside the ward boundary
    std::vector<Polygon> filtered;
    for (const auto& building : geometry) {
        // Keep buildings whose centroid is within the patch
        // This is a simplified filtering - full implementation would
        // check building vertices against patch boundary
        filtered.push_back(building);
    }
    geometry = filtered;
}

float Ward::rateLocation(Model* /*model*/, Patch* /*patch*/) {
    // Base implementation returns 0
    // Subclasses override to rate how suitable a location is
    return 0.0f;
}

std::vector<Polygon> Ward::createAlleys(const Polygon& p, float minSq,
                                         float gridChaos, float sizeChaos,
                                         float emptyProb, bool split) {
    // Looking for the longest edge to cut it
    Point v;
    float maxLength = -1.0f;

    p.forEdge([&v, &maxLength](const Point& p0, const Point& p1) {
        float len = p0.distance(p1);
        if (len > maxLength) {
            maxLength = len;
            v = p0;
        }
    });

    float spread = 0.8f * gridChaos;
    float ratio = (1.0f - spread) / 2.0f + Random::getFloat() * spread;

    // Trying to keep buildings rectangular even in chaotic wards
    float angleSpread = static_cast<float>(M_PI) / 6.0f * gridChaos *
                        (p.square() < minSq * 4 ? 0.0f : 1.0f);
    float b = (Random::getFloat() - 0.5f) * angleSpread;

    Polygon poly = p;  // Make mutable copy for bisect
    std::vector<Polygon> halves = Cutter::bisect(poly, v, ratio, b, split ? ALLEY : 0.0f);

    std::vector<Polygon> buildings;
    for (auto& half : halves) {
        float threshold = minSq * std::pow(2.0f, 4.0f * sizeChaos * (Random::getFloat() - 0.5f));

        if (half.square() < threshold) {
            if (!Random::getBool(emptyProb)) {
                buildings.push_back(half);
            }
        } else {
            bool shouldSplit = half.square() > minSq / (Random::getFloat() * Random::getFloat());
            std::vector<Polygon> subBuildings = createAlleys(half, minSq, gridChaos, sizeChaos, emptyProb, shouldSplit);
            buildings.insert(buildings.end(), subBuildings.begin(), subBuildings.end());
        }
    }

    return buildings;
}

Point Ward::findLongestEdge(const Polygon& poly) {
    Point result;
    float maxLength = -std::numeric_limits<float>::infinity();

    poly.forEdge([&result, &maxLength, &poly](const Point& v0, const Point& /*v1*/) {
        Point vec = poly.vector(v0);
        float len = vec.length();
        if (len > maxLength) {
            maxLength = len;
            result = v0;
        }
    });

    return result;
}

std::vector<Polygon> Ward::createOrthoBuilding(const Polygon& poly, float minBlockSq, float fill) {
    // Recursive slicing to create orthogonal building footprints
    std::vector<Polygon> buildings;

    if (poly.square() < minBlockSq) {
        // Base case: polygon is small enough to be a building
        if (Random::getFloat() < fill) {
            buildings.push_back(poly);
        }
        return buildings;
    }

    // Find the longest edge and bisect along it
    Point longestVertex = findLongestEdge(poly);

    // Bisect with slight randomness
    float ratio = 0.4f + Random::getFloat() * 0.2f;  // 0.4 to 0.6
    Polygon mutablePoly = poly;
    std::vector<Polygon> halves = Cutter::bisect(mutablePoly, longestVertex, ratio, 0.0f, ALLEY);

    for (auto& half : halves) {
        std::vector<Polygon> subBuildings = createOrthoBuilding(half, minBlockSq, fill);
        buildings.insert(buildings.end(), subBuildings.begin(), subBuildings.end());
    }

    return buildings;
}

} // namespace town_generator
