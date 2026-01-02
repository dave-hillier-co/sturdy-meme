// Model: Main city generation orchestrator
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// 1. Generate seed points in spiral pattern
// 2. Create Voronoi tessellation -> patches
// 3. Optionally add city walls (citadel + main wall)
// 4. Build street network from gates to center
// 5. Assign wards to patches based on location ratings
// 6. Generate building geometry for each ward

#pragma once

#include "Geometry.h"
#include "Voronoi.h"
#include "Patch.h"
#include "Ward.h"
#include "CurtainWall.h"
#include "Graph.h"
#include "WaterFeatures.h"
#include <vector>
#include <memory>
#include <random>
#include <optional>
#include <cmath>
#include <set>

namespace city {

// City generation parameters
struct CityParams {
    // Size and scale
    float radius = 100.0f;          // City radius in units
    int numPatches = 30;            // Number of Voronoi patches
    int relaxIterations = 3;        // Lloyd relaxation iterations

    // Feature flags
    bool hasWalls = true;           // Generate city walls
    bool hasCitadel = false;        // Generate inner citadel
    bool hasPlaza = true;           // Generate central plaza
    bool hasTemple = true;          // Generate cathedral/temple
    bool hasCastle = true;          // Generate castle

    // Street parameters
    float mainStreetWidth = 2.0f;
    float streetWidth = 1.0f;
    float alleyWidth = 0.6f;

    // Wall parameters
    float wallRadius = 0.7f;        // Wall at 70% of city radius
    float citadelRadius = 0.3f;     // Citadel at 30% of city radius
    float minGateDistance = 30.0f;  // Minimum distance between gates

    // Water parameters
    bool hasRiver = false;          // City has a river flowing through
    bool hasCoast = false;          // City is coastal
    bool hasShantyTown = true;      // Buildings outside walls
    float coastDirection = 0.0f;    // Direction to coast (radians, 0=east)
    float riverWidth = 5.0f;        // Base river width
    int numPiers = 3;               // Number of piers for coastal cities

    // Random seed (0 = random)
    uint32_t seed = 0;
};

// Street segment
struct Street {
    std::vector<Vec2> path;
    float width = 1.0f;
    bool isMainStreet = false;
};

class Model {
public:
    CityParams params;

    // City boundary
    Polygon border;

    // Voronoi patches
    std::vector<std::unique_ptr<Patch>> patches;
    std::vector<Patch*> innerPatches;     // Patches within city
    std::vector<Patch*> wallPatches;      // Patches within walls

    // Fortifications
    std::optional<CurtainWall> wall;
    std::optional<CurtainWall> citadel;
    std::vector<Vec2> gates;

    // Streets
    std::vector<Street> streets;
    std::vector<Street> roads;            // Roads to outer patches

    // Wards
    std::vector<std::unique_ptr<Ward>> wards;

    // Plaza (if any)
    std::optional<Polygon> plaza;
    Vec2 plazaCenter{0, 0};

    // Water features
    WaterFeatures water;

    // Generate the city
    void generate(const CityParams& params);

    // Get the center of the city
    Vec2 getCenter() const { return {0, 0}; }

    // Get all building polygons
    std::vector<Polygon> getAllBuildings() const;

    // Get patches by ward type
    std::vector<Patch*> getPatchesByWardType(WardType type) const;

private:
    std::mt19937 rng;

    // Generation steps
    void generateBorder();
    void generatePatches();
    void generateWater();
    void buildWalls();
    void buildStreets();
    void assignWards();
    void createGeometry();

    // Helper methods
    std::vector<Vec2> generateSpiralPoints(int count, float radius);
    void findNeighbors();
    void classifyPatches();
    void optimizeJunctions();
    void smoothStreets();
    Ward* createWard(Patch* patch, WardType type);
    WardType selectWardType(Patch* patch);
};

// Implementation

inline void Model::generate(const CityParams& p) {
    params = p;

    // Initialize RNG
    if (params.seed == 0) {
        std::random_device rd;
        rng.seed(rd());
    } else {
        rng.seed(params.seed);
    }

    generateBorder();
    generatePatches();
    findNeighbors();
    generateWater();
    buildWalls();
    classifyPatches();
    buildStreets();
    smoothStreets();
    assignWards();
    createGeometry();
}

inline void Model::generateWater() {
    if (!params.hasRiver && !params.hasCoast) return;

    WaterConfig config;
    config.hasRiver = params.hasRiver;
    config.hasCoast = params.hasCoast;
    config.hasPonds = true;  // Always allow small ponds
    config.riverWidth = params.riverWidth;
    config.coastDirection = params.coastDirection;
    config.numPiers = params.hasCoast ? params.numPiers : 0;

    // Collect patch pointers
    std::vector<Patch*> patchPtrs;
    for (auto& p : patches) {
        patchPtrs.push_back(p.get());
    }

    water.generate(config, params.radius, patchPtrs, rng);

    // Mark water patches as not within city (can't build on water)
    auto waterPatches = water.getWaterPatches(patchPtrs);
    for (auto* patch : waterPatches) {
        patch->withinCity = false;
        patch->withinWalls = false;
    }
}

inline void Model::generateBorder() {
    // Create circular city boundary
    border = Polygon::regular(32, params.radius, {0, 0});
}

inline std::vector<Vec2> Model::generateSpiralPoints(int count, float /* radius */) {
    std::vector<Vec2> points;
    points.reserve(count);

    // Match original algorithm: sqrt(i) * 5 angle, increasing radius
    // Original: a = sa + sqrt(i) * 5, r = (i == 0 ? 0 : 10 + i * (2 + random))
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float startAngle = dist(rng) * 2.0f * 3.14159265f;

    for (int i = 0; i < count; i++) {
        float a = startAngle + std::sqrt(static_cast<float>(i)) * 5.0f;
        float r = (i == 0) ? 0.0f : (10.0f + i * (2.0f + dist(rng)));

        points.push_back({
            std::cos(a) * r,
            std::sin(a) * r
        });
    }

    return points;
}

inline void Model::generatePatches() {
    // Generate seed points - original uses 8x numPatches
    auto seeds = generateSpiralPoints(params.numPatches * 8, params.radius);

    // Build Voronoi diagram
    Voronoi voronoi = Voronoi::build(seeds);

    // Selective relaxation: only relax central wards (first 3 points + nPatches)
    // This matches the original algorithm
    for (int iter = 0; iter < 3; iter++) {
        std::vector<int> toRelax = {0, 1, 2};
        if (params.numPatches < static_cast<int>(voronoi.points.size())) {
            toRelax.push_back(params.numPatches);
        }
        voronoi = Voronoi::relaxSelected(voronoi, toRelax);
    }

    // Sort points by distance from origin (as in original)
    std::sort(voronoi.points.begin(), voronoi.points.end(),
        [](const Vec2& a, const Vec2& b) {
            return a.length() < b.length();
        });

    // Create patches from Voronoi regions
    auto regions = voronoi.getInteriorRegions();

    // Sort regions by distance from origin (as in original)
    std::sort(regions.begin(), regions.end(),
        [](Region* a, Region* b) {
            return a->seed.length() < b->seed.length();
        });

    int count = 0;
    for (auto* region : regions) {
        auto patch = std::make_unique<Patch>(*region);

        // First nPatches regions are inner city (matching original algorithm)
        if (count < params.numPatches) {
            patch->withinCity = true;
            patch->withinWalls = params.hasWalls;
            innerPatches.push_back(patch.get());
        }

        // Set center from first patch (closest to origin)
        if (count == 0) {
            // Find vertex closest to origin
            float minDist = std::numeric_limits<float>::max();
            for (const auto& v : patch->shape.vertices) {
                float dist = v.length();
                if (dist < minDist) {
                    minDist = dist;
                    plazaCenter = v;
                }
            }
        }

        patches.push_back(std::move(patch));
        count++;
    }

    // Optimize junctions - merge close vertices
    optimizeJunctions();
}

inline void Model::findNeighbors() {
    // Find neighboring patches (those that share edges)
    for (size_t i = 0; i < patches.size(); i++) {
        for (size_t j = i + 1; j < patches.size(); j++) {
            if (patches[i]->borders(*patches[j])) {
                patches[i]->neighbors.push_back(patches[j].get());
                patches[j]->neighbors.push_back(patches[i].get());
            }
        }
    }
}

inline void Model::optimizeJunctions() {
    // Merge vertices that are very close together
    // This matches the original optimizeJunctions() function
    const float MIN_EDGE_LENGTH = 8.0f;

    // Collect patches to optimize (inner + citadel)
    std::vector<Patch*> patchesToOptimize;
    for (auto& p : patches) {
        if (p->withinCity) {
            patchesToOptimize.push_back(p.get());
        }
    }

    std::set<Patch*> patchesToClean;

    for (auto* w : patchesToOptimize) {
        size_t index = 0;
        while (index < w->shape.vertices.size()) {
            Vec2& v0 = w->shape.vertices[index];
            size_t nextIdx = (index + 1) % w->shape.vertices.size();
            Vec2& v1 = w->shape.vertices[nextIdx];

            if (!(v0 == v1) && Vec2::distance(v0, v1) < MIN_EDGE_LENGTH) {
                // Find all patches that contain v1 and update them
                for (auto& p : patches) {
                    if (p.get() == w) continue;

                    for (auto& v : p->shape.vertices) {
                        if (v == v1) {
                            v = v0;  // Will be updated to midpoint below
                            patchesToClean.insert(p.get());
                        }
                    }
                }

                // Move v0 to midpoint
                v0 = (v0 + v1) * 0.5f;

                // Update v1 references to point to the new midpoint
                for (auto* p : patchesToClean) {
                    for (auto& v : p->shape.vertices) {
                        if (v == v1) {
                            v = v0;
                        }
                    }
                }

                // Remove v1 from this patch
                w->shape.vertices.erase(w->shape.vertices.begin() + nextIdx);
            }
            index++;
        }
    }

    // Remove duplicate vertices from patches that were modified
    for (auto* p : patchesToClean) {
        auto& verts = p->shape.vertices;
        for (size_t i = 0; i < verts.size(); ) {
            bool foundDup = false;
            for (size_t j = i + 1; j < verts.size(); ) {
                if (verts[i] == verts[j]) {
                    verts.erase(verts.begin() + j);
                    foundDup = true;
                } else {
                    j++;
                }
            }
            if (!foundDup) i++;
        }
    }
}

inline void Model::smoothStreets() {
    // Smooth street paths by averaging with neighbors
    // This matches the original smoothStreet() function with factor 3
    for (auto& street : streets) {
        if (street.path.size() < 3) continue;

        std::vector<Vec2> smoothed;
        smoothed.reserve(street.path.size());

        // Keep first and last vertices fixed
        smoothed.push_back(street.path[0]);

        for (size_t i = 1; i < street.path.size() - 1; i++) {
            const Vec2& prev = street.path[i - 1];
            const Vec2& curr = street.path[i];
            const Vec2& next = street.path[i + 1];

            // smoothVertexEq with factor 3: (prev + curr*3 + next) / 5
            float f = 3.0f;
            Vec2 sm = {
                (prev.x + curr.x * f + next.x) / (2.0f + f),
                (prev.y + curr.y * f + next.y) / (2.0f + f)
            };
            smoothed.push_back(sm);
        }

        smoothed.push_back(street.path.back());
        street.path = std::move(smoothed);
    }
}

inline void Model::buildWalls() {
    // The wall is built around the inner patches (first nPatches)
    // innerPatches was populated during generatePatches()

    if (innerPatches.empty()) return;

    // Build the border (wall or just outline)
    wall.emplace();

    std::vector<Patch*> allPatches;
    for (auto& p : patches) allPatches.push_back(p.get());

    // Reserved points are citadel vertices (if any)
    std::vector<Vec2> reserved;

    wall->build(innerPatches, allPatches, params.hasWalls ? 2 : 0);

    if (params.hasWalls) {
        wall->buildGates(innerPatches, params.minGateDistance, rng);
        wall->buildTowers();
    }

    // Copy gates
    gates = wall->gates;

    // Filter patches to only keep those within 3x wall radius
    float wallRadius = wall->getRadius();

    std::vector<std::unique_ptr<Patch>> filteredPatches;
    std::vector<Patch*> newInnerPatches;

    for (auto& patch : patches) {
        // Check minimum distance from any vertex to center
        float minDist = std::numeric_limits<float>::max();
        for (const auto& v : patch->shape.vertices) {
            minDist = std::min(minDist, v.length());
        }

        if (minDist < wallRadius * 3) {
            if (patch->withinCity) {
                newInnerPatches.push_back(patch.get());
            }
            filteredPatches.push_back(std::move(patch));
        }
    }
    patches = std::move(filteredPatches);
    innerPatches = std::move(newInnerPatches);

    // Build citadel if requested (at position nPatches in the original)
    if (params.hasCitadel && patches.size() > static_cast<size_t>(params.numPatches)) {
        // The citadel patch is at index nPatches (just outside inner city)
        // In our case, find a compact patch near center for citadel
        Patch* citadelPatch = nullptr;
        for (auto& patch : patches) {
            if (!patch->withinCity && patch->shape.compactness() >= 0.7f) {
                citadelPatch = patch.get();
                break;
            }
        }

        if (citadelPatch) {
            citadelPatch->withinCity = true;
            std::vector<Patch*> citadelPatches = {citadelPatch};
            citadel.emplace();
            citadel->build(citadelPatches, allPatches, 1);
            citadel->buildGates(citadelPatches, params.minGateDistance / 2, rng);
            citadel->buildTowers();

            // Add citadel gates to main gates list
            gates.insert(gates.end(), citadel->gates.begin(), citadel->gates.end());
        }
    }
}

inline void Model::classifyPatches() {
    // wallPatches = innerPatches for walled cities
    // For unwalled cities, all innerPatches are also wallPatches
    wallPatches.clear();
    for (auto* patch : innerPatches) {
        if (patch->withinWalls || !params.hasWalls) {
            wallPatches.push_back(patch);
        }
    }
}

inline void Model::buildStreets() {
    // Build street network using pathfinding
    // Streets connect gates to city center and to each other

    if (gates.empty()) {
        // No gates, create streets from center outward
        if (params.hasPlaza) {
            // Find central patch for plaza
            Patch* centralPatch = nullptr;
            float minDist = std::numeric_limits<float>::max();

            for (auto* patch : wallPatches) {
                float dist = Vec2::distance(patch->seed, getCenter());
                if (dist < minDist) {
                    minDist = dist;
                    centralPatch = patch;
                }
            }

            if (centralPatch) {
                plazaCenter = centralPatch->seed;
                plaza = centralPatch->shape.inset(REGULAR_STREET);
            }
        }
        return;
    }

    // Create topology for pathfinding
    Topology topology;
    std::vector<std::vector<Vec2>*> shapes;
    std::vector<bool> withinCity;

    for (auto& patch : patches) {
        shapes.push_back(&patch->shape.vertices);
        withinCity.push_back(patch->withinCity);
    }

    // Blocked points are wall vertices (except gates)
    std::vector<Vec2> blocked;
    if (wall) {
        for (const auto& v : wall->shape.vertices) {
            bool isGate = false;
            for (const auto& g : gates) {
                if (v == g) {
                    isGate = true;
                    break;
                }
            }
            if (!isGate) {
                blocked.push_back(v);
            }
        }
    }

    // Pass wall shape as border (for categorizing inner/outer nodes)
    const Polygon* borderShape = wall ? &wall->shape : nullptr;
    topology.build(shapes, withinCity, blocked, borderShape);

    // Find or create plaza center
    if (params.hasPlaza) {
        Patch* centralPatch = nullptr;
        float minDist = std::numeric_limits<float>::max();

        for (auto* patch : wallPatches) {
            float dist = Vec2::distance(patch->seed, getCenter());
            if (dist < minDist) {
                minDist = dist;
                centralPatch = patch;
            }
        }

        if (centralPatch) {
            plazaCenter = centralPatch->seed;
            plaza = centralPatch->shape.inset(REGULAR_STREET);
        }
    }

    // Build main streets from gates to center
    for (const auto& gate : gates) {
        // Find the vertex in topology closest to gate
        Vec2* gateVertex = nullptr;
        float minDist = std::numeric_limits<float>::max();

        for (auto& [pt, node] : topology.pointToNode) {
            float dist = Vec2::distance(*pt, gate);
            if (dist < minDist) {
                minDist = dist;
                gateVertex = pt;
            }
        }

        // Find vertex closest to plaza center
        Vec2* centerVertex = nullptr;
        float minCenterDist = std::numeric_limits<float>::max();

        for (auto& [pt, node] : topology.pointToNode) {
            float dist = Vec2::distance(*pt, plazaCenter);
            if (dist < minCenterDist) {
                minCenterDist = dist;
                centerVertex = pt;
            }
        }

        if (gateVertex && centerVertex) {
            auto path = topology.buildPath(gateVertex, centerVertex);
            if (!path.empty()) {
                Street street;
                street.path = path;
                street.width = params.mainStreetWidth;
                street.isMainStreet = true;
                streets.push_back(street);
            }
        }
    }
}

inline void Model::assignWards() {
    // Original WARDS array sequence (matching Model.hx)
    // This provides a specific distribution of ward types
    static const std::vector<WardType> WARDS_SEQUENCE = {
        WardType::Craftsmen, WardType::Craftsmen, WardType::Merchants, WardType::Craftsmen, WardType::Craftsmen, WardType::Cathedral,
        WardType::Craftsmen, WardType::Craftsmen, WardType::Craftsmen, WardType::Craftsmen, WardType::Craftsmen,
        WardType::Craftsmen, WardType::Craftsmen, WardType::Craftsmen, WardType::Administration, WardType::Craftsmen,
        WardType::Slum, WardType::Craftsmen, WardType::Slum, WardType::Patriciate, WardType::Market,
        WardType::Slum, WardType::Craftsmen, WardType::Craftsmen, WardType::Craftsmen, WardType::Slum,
        WardType::Craftsmen, WardType::Craftsmen, WardType::Craftsmen, WardType::Military, WardType::Slum,
        WardType::Craftsmen, WardType::Park, WardType::Patriciate, WardType::Market, WardType::Merchants
    };

    // Copy and shuffle slightly (as in original)
    std::vector<WardType> availableTypes = WARDS_SEQUENCE;
    int shuffleCount = static_cast<int>(availableTypes.size()) / 10;
    for (int i = 0; i < shuffleCount; i++) {
        std::uniform_int_distribution<size_t> dist(0, availableTypes.size() - 2);
        size_t idx = dist(rng);
        std::swap(availableTypes[idx], availableTypes[idx + 1]);
    }

    // Collect unassigned inner patches
    std::vector<Patch*> unassigned;
    for (auto* patch : innerPatches) {
        unassigned.push_back(patch);
    }

    // First, assign plaza to central patch (if enabled)
    if (params.hasPlaza && !unassigned.empty()) {
        // Find the most central patch
        Patch* centralPatch = nullptr;
        float minDist = std::numeric_limits<float>::max();
        for (auto* patch : unassigned) {
            float dist = patch->seed.length();
            if (dist < minDist) {
                minDist = dist;
                centralPatch = patch;
            }
        }
        if (centralPatch) {
            Ward* ward = createWard(centralPatch, WardType::Market);
            centralPatch->ward = ward;
            plaza = centralPatch->shape;
            unassigned.erase(std::remove(unassigned.begin(), unassigned.end(), centralPatch), unassigned.end());
        }
    }

    // Assign gate wards to patches touching gates
    std::uniform_real_distribution<float> gateProbDist(0.0f, 1.0f);
    float gateProb = params.hasWalls ? 0.5f : 0.2f;

    for (const auto& gate : gates) {
        if (gateProbDist(rng) > gateProb) continue;

        for (auto* patch : unassigned) {
            bool touchesGate = false;
            for (const auto& v : patch->shape.vertices) {
                if (v == gate) {
                    touchesGate = true;
                    break;
                }
            }

            if (touchesGate && !patch->ward) {
                Ward* ward = createWard(patch, WardType::Gate);
                patch->ward = ward;
                unassigned.erase(std::remove(unassigned.begin(), unassigned.end(), patch), unassigned.end());
                break;
            }
        }
    }

    // Assign wards from the sequence using rateLocation
    size_t wardIdx = 0;
    while (!unassigned.empty() && wardIdx < availableTypes.size()) {
        WardType type = availableTypes[wardIdx++];

        // Find best patch for this ward type
        Patch* bestPatch = nullptr;
        float bestRating = std::numeric_limits<float>::infinity();

        for (Patch* patch : unassigned) {
            if (patch->ward) continue;
            float rating = Ward::rateLocation(*this, *patch, type);
            if (rating < bestRating) {
                bestRating = rating;
                bestPatch = patch;
            }
        }

        if (bestPatch && bestRating < std::numeric_limits<float>::infinity()) {
            Ward* ward = createWard(bestPatch, type);
            bestPatch->ward = ward;
            unassigned.erase(std::remove(unassigned.begin(), unassigned.end(), bestPatch), unassigned.end());
        }
    }

    // Assign remaining unassigned inner patches as slums
    for (Patch* patch : unassigned) {
        if (!patch->ward) {
            Ward* ward = createWard(patch, WardType::Slum);
            patch->ward = ward;
        }
    }

    // Outskirts: assign gate wards to outer patches touching wall gates
    if (params.hasWalls) {
        float outskirtProb = 1.0f / std::max(1, params.numPatches - 5);
        for (const auto& gate : wall->gates) {
            if (gateProbDist(rng) < outskirtProb) continue;

            for (auto& patch : patches) {
                if (patch->ward) continue;

                bool touchesGate = false;
                for (const auto& v : patch->shape.vertices) {
                    if (v == gate) {
                        touchesGate = true;
                        break;
                    }
                }

                if (touchesGate) {
                    patch->withinCity = true;
                    Ward* ward = createWard(patch.get(), WardType::Gate);
                    patch->ward = ward;
                }
            }
        }
    }

    // Assign farms/empty wards to outer patches
    std::uniform_real_distribution<float> farmDist(0.0f, 1.0f);
    for (auto& patch : patches) {
        if (patch->ward) continue;

        // 20% chance of farm if compact enough, otherwise empty ward
        if (farmDist(rng) < 0.2f && patch->shape.compactness() >= 0.7f) {
            Ward* ward = createWard(patch.get(), WardType::Farm);
            patch->ward = ward;
        } else {
            // Empty ward (just the patch shape, no buildings)
            Ward* ward = createWard(patch.get(), WardType::Craftsmen);
            ward->geometry.clear();  // Clear any generated buildings
            patch->ward = ward;
        }
    }
}

inline WardType Model::selectWardType(Patch* patch) {
    // Select appropriate ward type based on location
    float distFromCenter = Vec2::distance(patch->seed, getCenter());

    // Near center: merchants, patriciate
    if (distFromCenter < params.radius * 0.3f) {
        std::uniform_int_distribution<int> dist(0, 2);
        switch (dist(rng)) {
            case 0: return WardType::Merchants;
            case 1: return WardType::Patriciate;
            default: return WardType::Administration;
        }
    }

    // Middle: craftsmen, merchants
    if (distFromCenter < params.radius * 0.6f) {
        std::uniform_int_distribution<int> dist(0, 2);
        switch (dist(rng)) {
            case 0: return WardType::Craftsmen;
            case 1: return WardType::Merchants;
            default: return WardType::Craftsmen;
        }
    }

    // Outer: slums, craftsmen
    std::uniform_int_distribution<int> dist(0, 2);
    switch (dist(rng)) {
        case 0: return WardType::Slum;
        case 1: return WardType::Craftsmen;
        default: return WardType::Slum;
    }
}

inline Ward* Model::createWard(Patch* patch, WardType type) {
    std::unique_ptr<Ward> ward;

    switch (type) {
        case WardType::Castle:
            ward = std::make_unique<CastleWard>(this, patch);
            break;
        case WardType::Cathedral:
            ward = std::make_unique<CathedralWard>(this, patch);
            break;
        case WardType::Market:
            ward = std::make_unique<MarketWard>(this, patch);
            break;
        case WardType::Patriciate:
            ward = std::make_unique<PatriciateWard>(this, patch);
            break;
        case WardType::Craftsmen:
            ward = std::make_unique<CraftsmenWard>(this, patch);
            break;
        case WardType::Merchants:
            ward = std::make_unique<MerchantsWard>(this, patch);
            break;
        case WardType::Administration:
            ward = std::make_unique<AdministrationWard>(this, patch);
            break;
        case WardType::Military:
            ward = std::make_unique<MilitaryWard>(this, patch);
            break;
        case WardType::Slum:
            ward = std::make_unique<SlumWard>(this, patch);
            break;
        case WardType::Farm:
            ward = std::make_unique<FarmWard>(this, patch);
            break;
        case WardType::Park:
            ward = std::make_unique<ParkWard>(this, patch);
            break;
        case WardType::Gate:
            ward = std::make_unique<GateWard>(this, patch);
            break;
        default:
            ward = std::make_unique<CraftsmenWard>(this, patch);
            break;
    }

    Ward* ptr = ward.get();
    wards.push_back(std::move(ward));
    return ptr;
}

inline void Model::createGeometry() {
    // Generate building geometry for all wards
    for (auto& ward : wards) {
        ward->createGeometry(rng);
    }
}

inline std::vector<Polygon> Model::getAllBuildings() const {
    std::vector<Polygon> buildings;
    for (const auto& ward : wards) {
        buildings.insert(buildings.end(),
                        ward->geometry.begin(),
                        ward->geometry.end());
    }
    return buildings;
}

inline std::vector<Patch*> Model::getPatchesByWardType(WardType type) const {
    std::vector<Patch*> result;
    for (const auto& patch : patches) {
        if (patch->ward && patch->ward->type == type) {
            result.push_back(patch.get());
        }
    }
    return result;
}

} // namespace city
