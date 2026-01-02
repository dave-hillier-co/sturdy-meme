#include <town_generator/building/Model.h>
#include <town_generator/geom/Segment.h>
#include <town_generator/utils/Random.h>
#include <town_generator/utils/MathUtils.h>
#include <town_generator/wards/Ward.h>
#include <town_generator/wards/Castle.h>
#include <town_generator/wards/Cathedral.h>
#include <town_generator/wards/Market.h>
#include <town_generator/wards/CommonWard.h>
#include <town_generator/wards/CraftsmenWard.h>
#include <town_generator/wards/MerchantWard.h>
#include <town_generator/wards/PatriciateWard.h>
#include <town_generator/wards/AdministrationWard.h>
#include <town_generator/wards/MilitaryWard.h>
#include <town_generator/wards/GateWard.h>
#include <town_generator/wards/Slum.h>
#include <town_generator/wards/Farm.h>
#include <town_generator/wards/Park.h>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <iostream>

namespace town_generator {

Model* Model::instance = nullptr;

// Ward type enumeration for random selection
enum class WardType {
    Craftsmen, Merchant, Cathedral, Administration, Slum, Patriciate,
    Market, Military, Park
};

Model::Model(int nPatches, int seed) {
    if (seed > 0) {
        Random::reset(seed);
    }

    this->nPatches = (nPatches != -1) ? nPatches : 15;

    plazaNeeded = Random::getBool();
    citadelNeeded = Random::getBool();
    wallsNeeded = Random::getBool();

    // Keep trying until successful
    bool success = false;
    int attempts = 0;
    const int maxAttempts = 10;
    while (!success && attempts < maxAttempts) {
        try {
            build();
            instance = this;
            success = true;
        } catch (const std::exception& e) {
            std::cerr << "  Build attempt " << (attempts + 1) << " failed: " << e.what() << std::endl;
            // Reset and try again
            for (auto* patch : patches) delete patch;
            patches.clear();
            inner.clear();
            citadel = nullptr;
            plaza = nullptr;
            delete border;
            border = nullptr;
            wall = nullptr;
            for (auto* pt : generatedPoints) delete pt;
            generatedPoints.clear();
            attempts++;
            // Advance random state to get different results
            Random::getFloat();
            Random::getFloat();
            Random::getFloat();
        }
    }
    if (!success) {
        throw std::runtime_error("Failed to generate city after " + std::to_string(maxAttempts) + " attempts");
    }
}

Model::~Model() {
    for (auto* patch : patches) {
        delete patch->ward;
        delete patch;
    }
    delete border;
    // wall points to border if wallsNeeded, don't double delete
    for (auto* pt : generatedPoints) {
        delete pt;
    }
    if (instance == this) {
        instance = nullptr;
    }
}

void Model::build() {
    streets.clear();
    roads.clear();

    std::cerr << "  buildPatches..." << std::endl;
    buildPatches();
    std::cerr << "  optimizeJunctions..." << std::endl;
    optimizeJunctions();
    std::cerr << "  buildWalls..." << std::endl;
    buildWalls();
    std::cerr << "  buildStreets..." << std::endl;
    buildStreets();
    std::cerr << "  createWards..." << std::endl;
    createWards();
    std::cerr << "  buildGeometry..." << std::endl;
    buildGeometry();
    std::cerr << "  Done." << std::endl;
}

void Model::buildPatches() {
    float sa = Random::getFloat() * 2 * static_cast<float>(M_PI);

    std::vector<Point*> points;
    for (int i = 0; i < nPatches * 8; i++) {
        float a = sa + std::sqrt(static_cast<float>(i)) * 5;
        float r = (i == 0) ? 0 : 10 + i * (2 + Random::getFloat());
        auto* pt = new Point(std::cos(a) * r, std::sin(a) * r);
        points.push_back(pt);
        generatedPoints.push_back(pt);
    }

    auto* voronoi = Voronoi::build(points);

    // Relaxing central wards
    for (int i = 0; i < 3; i++) {
        std::vector<Point*> toRelax;
        for (int j = 0; j < 3 && j < static_cast<int>(voronoi->points.size()); j++) {
            toRelax.push_back(voronoi->points[j]);
        }
        if (nPatches < static_cast<int>(voronoi->points.size())) {
            toRelax.push_back(voronoi->points[nPatches]);
        }
        auto* relaxed = Voronoi::relax(voronoi, &toRelax);
        delete voronoi;
        voronoi = relaxed;
    }

    // Sort points by distance from origin
    std::sort(voronoi->points.begin(), voronoi->points.end(),
        [](Point* p1, Point* p2) {
            return p1->length() < p2->length();
        });

    auto regions = voronoi->partitioning();

    patches.clear();
    inner.clear();

    int count = 0;
    for (auto& r : regions) {
        auto* patch = Patch::fromRegion(r);
        patches.push_back(patch);

        if (count == 0) {
            // Find vertex closest to origin
            float minLen = std::numeric_limits<float>::infinity();
            for (const auto& p : patch->shape.vertices) {
                if (p.length() < minLen) {
                    minLen = p.length();
                    center = p;
                }
            }
            if (plazaNeeded) {
                plaza = patch;
            }
        } else if (count == nPatches && citadelNeeded) {
            citadel = patch;
            citadel->withinCity = true;
        }

        if (count < nPatches) {
            patch->withinCity = true;
            patch->withinWalls = wallsNeeded;
            inner.push_back(patch);
        }

        count++;
    }

    delete voronoi;
}

void Model::buildWalls() {
    std::vector<Point> reserved;
    if (citadel) {
        for (const auto& v : citadel->shape.vertices) {
            reserved.push_back(v);
        }
    }

    border = new CurtainWall(wallsNeeded, this, inner, reserved);
    if (wallsNeeded) {
        wall = border;
        wall->buildTowers();
    }

    float radius = border->getRadius();

    // Filter patches by distance
    std::vector<Patch*> filteredPatches;
    for (auto* p : patches) {
        if (p->shape.distance(center) < radius * 3) {
            filteredPatches.push_back(p);
        } else {
            delete p;
        }
    }
    patches = filteredPatches;

    gates = border->gates;

    if (citadel) {
        auto* castle = new Castle(this, citadel);
        castle->wall->buildTowers();
        citadel->ward = castle;

        if (citadel->shape.compactness() < 0.75f) {
            throw std::runtime_error("Bad citadel shape!");
        }

        for (const auto& g : castle->wall->gates) {
            gates.push_back(g);
        }
    }
}

Polygon Model::findCircumference(const std::vector<Patch*>& wards) {
    if (wards.empty()) {
        return Polygon();
    }
    if (wards.size() == 1) {
        return Polygon(wards[0]->shape.vertices);
    }

    std::vector<Point> A;
    std::vector<Point> B;

    for (auto* w1 : wards) {
        w1->shape.forEdge([&A, &B, &wards, w1](const Point& a, const Point& b) {
            bool outerEdge = true;
            for (auto* w2 : wards) {
                if (w2->shape.findEdge(b, a) != -1) {
                    outerEdge = false;
                    break;
                }
            }
            if (outerEdge) {
                A.push_back(a);
                B.push_back(b);
            }
        });
    }

    Polygon result;
    size_t index = 0;
    size_t iterations = 0;
    const size_t maxIterations = A.size() + 10;
    do {
        result.push(A[index]);
        // Find where B[index] appears in A
        auto it = std::find_if(A.begin(), A.end(),
            [&B, index](const Point& p) { return p.equals(B[index]); });
        index = (it != A.end()) ? (it - A.begin()) : 0;
        iterations++;
        if (iterations > maxIterations) {
            // Infinite loop detected - return partial result to allow retry
            break;
        }
    } while (index != 0);

    return result;
}

std::vector<Patch*> Model::patchByVertex(const Point& v) {
    std::vector<Patch*> result;
    for (auto* patch : patches) {
        if (patch->shape.contains(v)) {
            result.push_back(patch);
        }
    }
    return result;
}

void Model::buildStreets() {
    topology = std::make_unique<Topology>(this);

    for (const auto& gate : gates) {
        // Find destination (nearest plaza corner or center)
        Point end;
        if (plaza) {
            float minDist = std::numeric_limits<float>::infinity();
            for (const auto& v : plaza->shape.vertices) {
                float dist = v.distance(gate);
                if (dist < minDist) {
                    minDist = dist;
                    end = v;
                }
            }
        } else {
            end = center;
        }

        // Find gate point in topology
        Point* gatePtr = nullptr;
        Point* endPtr = nullptr;
        for (auto& [pt, node] : topology->pt2node) {
            if (pt->equals(gate)) gatePtr = pt;
            if (pt->equals(end)) endPtr = pt;
        }

        if (gatePtr && endPtr) {
            auto street = topology->buildPath(gatePtr, endPtr, &topology->outer);
            if (!street.empty()) {
                streets.push_back(Polygon(street));

                // Check if this is a border gate and build road
                bool isBorderGate = std::find_if(border->gates.begin(), border->gates.end(),
                    [&gate](const Point& g) { return g.equals(gate); }) != border->gates.end();

                if (isBorderGate) {
                    Point dir = gate.norm(1000);
                    Point* start = nullptr;
                    float minDist = std::numeric_limits<float>::infinity();

                    for (auto& [pt, node] : topology->pt2node) {
                        float d = pt->distance(dir);
                        if (d < minDist) {
                            minDist = d;
                            start = pt;
                        }
                    }

                    if (start) {
                        auto road = topology->buildPath(start, gatePtr, &topology->inner);
                        if (!road.empty()) {
                            roads.push_back(Polygon(road));
                        }
                    }
                }
            } else {
                throw std::runtime_error("Unable to build a street!");
            }
        }
    }

    tidyUpRoads();

    // Smooth arteries
    for (auto& a : arteries) {
        if (a.size() > 2) {
            auto smoothed = a.smoothVertexEq(3);
            for (size_t i = 1; i + 1 < a.size(); i++) {
                a[i] = smoothed[i];
            }
        }
    }
}

void Model::tidyUpRoads() {
    std::vector<Segment> segments;

    auto cut2segments = [this, &segments](const Street& street) {
        for (size_t i = 1; i < street.size(); i++) {
            const Point& v0 = street[i - 1];
            const Point& v1 = street[i];

            // Skip segments along plaza
            if (plaza && plaza->shape.contains(v0) && plaza->shape.contains(v1)) {
                continue;
            }

            // Check if segment already exists
            bool exists = false;
            for (const auto& seg : segments) {
                if (seg.start.equals(v0) && seg.end.equals(v1)) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                segments.push_back(Segment(v0, v1));
            }
        }
    };

    for (const auto& street : streets) {
        cut2segments(street);
    }
    for (const auto& road : roads) {
        cut2segments(road);
    }

    arteries.clear();
    while (!segments.empty()) {
        Segment seg = segments.back();
        segments.pop_back();

        bool attached = false;
        for (auto& a : arteries) {
            if (a.first().equals(seg.end)) {
                a.unshift(seg.start);
                attached = true;
                break;
            } else if (a.last().equals(seg.start)) {
                a.push(seg.end);
                attached = true;
                break;
            }
        }

        if (!attached) {
            arteries.push_back(Polygon({seg.start, seg.end}));
        }
    }
}

void Model::optimizeJunctions() {
    std::vector<Patch*> patchesToOptimize = inner;
    if (citadel) {
        patchesToOptimize.push_back(citadel);
    }

    std::vector<Patch*> wards2clean;

    for (auto* w : patchesToOptimize) {
        size_t index = 0;
        while (index < w->shape.size()) {
            Point& v0 = w->shape[index];
            Point& v1 = w->shape[(index + 1) % w->shape.size()];

            if (!v0.equals(v1) && v0.distance(v1) < 8) {
                for (auto* w1 : patchByVertex(v1)) {
                    if (w1 != w) {
                        int idx = w1->shape.indexOf(v1);
                        if (idx != -1) {
                            w1->shape[idx] = v0;
                            wards2clean.push_back(w1);
                        }
                    }
                }

                v0.addEq(v1);
                v0.scaleEq(0.5f);

                // Remove v1 from shape
                int removeIdx = w->shape.indexOf(v1);
                if (removeIdx != -1) {
                    w->shape.remove(removeIdx);
                }
            }
            index++;
        }
    }

    // Remove duplicate vertices
    for (auto* w : wards2clean) {
        for (size_t i = 0; i < w->shape.size(); i++) {
            const Point& v = w->shape[i];
            for (size_t j = i + 1; j < w->shape.size(); ) {
                if (w->shape[j].equals(v)) {
                    w->shape.remove(j);
                } else {
                    j++;
                }
            }
        }
    }
}

void Model::createWards() {
    std::vector<Patch*> unassigned = inner;

    if (plaza) {
        plaza->ward = new Market(this, plaza);
        unassigned.erase(std::find(unassigned.begin(), unassigned.end(), plaza));
    }

    // Assign gate wards
    for (const auto& gate : border->gates) {
        for (auto* patch : patchByVertex(gate)) {
            if (patch->withinCity && !patch->ward) {
                float chance = (wall == nullptr) ? 0.2f : 0.5f;
                if (Random::getBool(chance)) {
                    patch->ward = new GateWard(this, patch);
                    auto it = std::find(unassigned.begin(), unassigned.end(), patch);
                    if (it != unassigned.end()) {
                        unassigned.erase(it);
                    }
                }
            }
        }
    }

    // Ward type distribution (matching original)
    std::vector<int> wardTypes = {
        0, 0, 1, 0, 0, 2,  // Craftsmen, Craftsmen, Merchant, Craftsmen, Craftsmen, Cathedral
        0, 0, 0, 0, 0,     // Craftsmen x5
        0, 0, 0, 3, 0,     // Craftsmen, Craftsmen, Craftsmen, Administration, Craftsmen
        4, 0, 4, 5, 6,     // Slum, Craftsmen, Slum, Patriciate, Market
        4, 0, 0, 0, 4,     // Slum, Craftsmen x3, Slum
        0, 0, 0, 7, 4,     // Craftsmen x3, Military, Slum
        0, 8, 5, 6, 1      // Craftsmen, Park, Patriciate, Market, Merchant
    };

    // Shuffle slightly
    for (size_t i = 0; i < wardTypes.size() / 10; i++) {
        int idx = Random::getInt(0, static_cast<int>(wardTypes.size()) - 1);
        std::swap(wardTypes[idx], wardTypes[idx + 1]);
    }

    size_t wardIdx = 0;
    while (!unassigned.empty()) {
        Patch* bestPatch = nullptr;
        int wardType = (wardIdx < wardTypes.size()) ? wardTypes[wardIdx++] : 4; // Default to Slum

        // Simple random selection for now
        // (Full implementation would use rateLocation functions)
        do {
            int idx = Random::getInt(0, static_cast<int>(unassigned.size()));
            bestPatch = unassigned[idx];
        } while (bestPatch->ward != nullptr && unassigned.size() > 1);

        // Create ward based on type
        switch (wardType) {
            case 0: bestPatch->ward = new CraftsmenWard(this, bestPatch); break;
            case 1: bestPatch->ward = new MerchantWard(this, bestPatch); break;
            case 2: bestPatch->ward = new Cathedral(this, bestPatch); break;
            case 3: bestPatch->ward = new AdministrationWard(this, bestPatch); break;
            case 4: bestPatch->ward = new Slum(this, bestPatch); break;
            case 5: bestPatch->ward = new PatriciateWard(this, bestPatch); break;
            case 6: bestPatch->ward = new Market(this, bestPatch); break;
            case 7: bestPatch->ward = new MilitaryWard(this, bestPatch); break;
            case 8: bestPatch->ward = new Park(this, bestPatch); break;
            default: bestPatch->ward = new CraftsmenWard(this, bestPatch); break;
        }

        auto it = std::find(unassigned.begin(), unassigned.end(), bestPatch);
        if (it != unassigned.end()) {
            unassigned.erase(it);
        }
    }

    // Outskirts - gate wards outside walls
    if (wall) {
        for (const auto& gate : wall->gates) {
            if (!Random::getBool(1.0f / (nPatches - 5))) {
                for (auto* patch : patchByVertex(gate)) {
                    if (!patch->ward) {
                        patch->withinCity = true;
                        patch->ward = new GateWard(this, patch);
                    }
                }
            }
        }
    }

    // Calculate city radius and process countryside
    cityRadius = 0;
    for (auto* patch : patches) {
        if (patch->withinCity) {
            for (const auto& v : patch->shape.vertices) {
                cityRadius = std::max(cityRadius, v.length());
            }
        } else if (!patch->ward) {
            if (Random::getBool(0.2f) && patch->shape.compactness() >= 0.7f) {
                patch->ward = new Farm(this, patch);
            } else {
                patch->ward = new Ward(this, patch);
            }
        }
    }
}

void Model::buildGeometry() {
    for (auto* patch : patches) {
        if (patch->ward) {
            patch->ward->createGeometry();
        }
    }
}

Patch* Model::getNeighbour(Patch* patch, const Point& v) {
    Point next = patch->shape.next(v);
    for (auto* p : patches) {
        if (p->shape.findEdge(next, v) != -1) {
            return p;
        }
    }
    return nullptr;
}

std::vector<Patch*> Model::getNeighbours(Patch* patch) {
    std::vector<Patch*> result;
    for (auto* p : patches) {
        if (p != patch && p->shape.borders(patch->shape)) {
            result.push_back(p);
        }
    }
    return result;
}

bool Model::isEnclosed(Patch* patch) {
    if (!patch->withinCity) return false;
    if (patch->withinWalls) return true;

    auto neighbours = getNeighbours(patch);
    for (auto* n : neighbours) {
        if (!n->withinCity) return false;
    }
    return true;
}

void Model::replacePatches(Patch* old, const std::vector<Patch*>& newPatches) {
    auto it = std::find(patches.begin(), patches.end(), old);
    if (it != patches.end()) {
        *it = newPatches[0];
        for (size_t i = 1; i < newPatches.size(); i++) {
            patches.insert(it + i, newPatches[i]);
        }
    }
}

} // namespace town_generator
