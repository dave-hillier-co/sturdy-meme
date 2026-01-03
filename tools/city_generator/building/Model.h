#pragma once

#include "../geom/Point.h"
#include "../geom/Polygon.h"
#include "../geom/Voronoi.h"
#include "../geom/Spline.h"
#include "../utils/Random.h"
#include "Patch.h"
#include "Topology.h"
#include "CurtainWall.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>

// Forward declarations for wards (full includes after Model class)
namespace towngenerator { namespace wards { class Ward; } }

namespace towngenerator {
namespace building {

using geom::Point;
using geom::Polygon;
using geom::Voronoi;
using geom::Region;
using geom::Spline;
using utils::Random;

// Forward declarations
class CurtainWall;
class Ward;

/**
 * Model - Main orchestrator for medieval city generation
 * Ported from Haxe Model class
 *
 * Generates a complete medieval city with:
 * - Voronoi-based patches (districts)
 * - City walls with gates
 * - Street network (arteries from gates to center)
 * - Ward assignments (residential, commercial, etc.)
 */
class Model {
public:
    // ====================
    // PARAMETERS (set before generation)
    // ====================

    int nPatches;           // Number of patches to generate
    bool plazaNeeded;       // Generate market plaza
    bool citadelNeeded;     // Generate castle/citadel
    bool wallsNeeded;       // Generate city walls
    bool templeNeeded;      // Generate temple
    int seed;               // Random seed

    // ====================
    // GENERATED DATA
    // ====================

    std::vector<Patch> patches;         // All patches (Voronoi regions)
    std::vector<Patch*> inner;          // Patches within city
    Patch* citadel;                     // Castle patch (if citadelNeeded)
    Patch* plaza;                       // Market plaza (if plazaNeeded)
    Point center;                       // City center point
    std::unique_ptr<CurtainWall> wall;  // City walls (if wallsNeeded)
    std::vector<Point> gates;           // Wall gate positions
    std::vector<std::vector<Point>> arteries;  // Main street paths from gates to center
    std::unique_ptr<Topology> topology; // Pathfinding graph
    std::vector<std::unique_ptr<wards::Ward>> wardStorage; // Ward object storage

    // ====================
    // WARD TYPES
    // ====================

    /**
     * Static weighted list of ward class types
     * 35 elements total, with weights determining probability of assignment
     *
     * Weight distribution:
     * - Residential wards: most common (multiple entries)
     * - Commercial/Market: common
     * - Craftsmen/Guild: moderate
     * - Religious/Temple: rare
     * - Noble/Castle: very rare (handled by citadelNeeded flag)
     */
    struct WardType {
        const char* name;
        int weight;
    };

    static constexpr int WARD_COUNT = 35;
    static const WardType WARDS[WARD_COUNT];

    // ====================
    // CONSTRUCTOR
    // ====================

    /**
     * Constructor - initializes parameters and generates city
     * @param nPatches Number of patches to generate
     * @param seed Random seed for reproducible generation
     */
    Model(int nPatches, int seed)
        : nPatches(nPatches)
        , plazaNeeded(true)
        , citadelNeeded(true)
        , wallsNeeded(true)
        , templeNeeded(true)
        , seed(seed)
        , citadel(nullptr)
        , plaza(nullptr)
        , center(0, 0)
    {
        // Initialize random generator with seed
        Random::reset(seed);

        // Generate the city
        generate();
    }

    // ====================
    // MAIN GENERATION
    // ====================

    /**
     * Assign ward types to patches and create geometry
     * Ported from Haxe Model.createWards()
     * Implementation below after ward includes
     */
    void createWards();

    /**
     * Orchestrates full city generation
     *
     * Algorithm:
     * 1. buildPatches() - Generate Voronoi diagram and convert to patches
     * 2. buildWalls() - Create curtain walls around inner patches
     * 3. buildStreets() - Route arteries from gates to center
     * 4. createWards() - Assign ward types to patches
     */
    void generate() {
        buildPatches();
        if (wallsNeeded) {
            buildWalls();
        }
        buildStreets();
        createWards();
    }

    /**
     * Generate patches from Voronoi diagram
     */
    void buildPatches() {
        // Calculate city radius based on patch count
        float radius = std::sqrt(static_cast<float>(nPatches)) * 8.0f;
        float width = radius * 2.5f;
        float height = radius * 2.5f;

        // Create seed points using Fermat spiral
        std::vector<Point> seeds;
        float goldenAngle = 3.14159265f * (3.0f - std::sqrt(5.0f)); // Golden angle in radians

        for (int i = 0; i < nPatches; ++i) {
            float r = radius * std::sqrt(static_cast<float>(i) / nPatches);
            float theta = i * goldenAngle;
            float x = width / 2.0f + r * std::cos(theta);
            float y = height / 2.0f + r * std::sin(theta);
            seeds.push_back(Point(x, y));
        }

        // Build Voronoi diagram with Lloyd relaxation
        Voronoi voronoi = Voronoi::build(seeds, width, height, 2);

        // Convert regions to patches
        patches.clear();
        for (const auto& region : voronoi.getRegions()) {
            patches.push_back(Patch::fromRegion(*region));
        }

        // Set city center
        center = Point(width / 2.0f, height / 2.0f);

        // Identify inner patches (within city radius)
        inner.clear();
        for (auto& patch : patches) {
            float dist = Point::distance(patch.shape.centroid(), center);
            if (dist < radius) {
                patch.withinCity = true;
                inner.push_back(&patch);
            }
        }

        // Find citadel (most central patch)
        if (citadelNeeded && !inner.empty()) {
            citadel = findMostCentral(inner, center);
        }

        // Find plaza (adjacent to citadel or most central)
        if (plazaNeeded && !inner.empty()) {
            if (citadel != nullptr) {
                // Find patch adjacent to citadel
                for (auto* candidate : inner) {
                    if (candidate != citadel && candidate->shape.borders(citadel->shape)) {
                        plaza = candidate;
                        break;
                    }
                }
            }
            if (plaza == nullptr) {
                // Use second most central patch
                plaza = findMostCentral(inner, center);
                if (plaza == citadel && inner.size() > 1) {
                    // Find next best
                    float bestDist = std::numeric_limits<float>::max();
                    for (auto* candidate : inner) {
                        if (candidate == citadel) continue;
                        float dist = Point::distance(candidate->shape.centroid(), center);
                        if (dist < bestDist) {
                            bestDist = dist;
                            plaza = candidate;
                        }
                    }
                }
            }
        }
    }

    /**
     * Create city walls around inner patches
     */
    void buildWalls() {
        if (inner.empty()) return;

        // Mark patches within walls
        for (auto* patch : inner) {
            patch->withinWalls = true;
        }

        // Get border of inner patches for gates
        auto border = getBorder(inner);
        gates = border; // Use border vertices as potential gate positions

        // Keep only a subset of gates (evenly spaced)
        if (gates.size() > 4) {
            std::vector<Point> selectedGates;
            size_t step = gates.size() / 4;
            for (size_t i = 0; i < gates.size(); i += step) {
                selectedGates.push_back(gates[i]);
                if (selectedGates.size() >= 4) break;
            }
            gates = selectedGates;
        }

        // Create the curtain wall around inner patches
        wall = std::make_unique<CurtainWall>(true, *this, inner, std::vector<Point>());

        // Update gates from wall
        gates = wall->gates;
    }

    /**
     * Route main streets from gates to center
     */
    void buildStreets() {
        arteries.clear();

        // For each gate, create a path to the center
        for (const auto& gate : gates) {
            std::vector<Point> artery;
            artery.push_back(gate);

            // Simple path: just go toward center
            Point current = gate;
            Point target = center;

            // Create intermediate points
            int steps = 5;
            for (int i = 1; i <= steps; ++i) {
                float t = static_cast<float>(i) / steps;
                Point next(
                    current.x + (target.x - current.x) * t,
                    current.y + (target.y - current.y) * t
                );
                artery.push_back(next);
            }

            // Smooth the path
            artery = smoothPath(artery);
            arteries.push_back(artery);
        }
    }

    // ====================
    // HELPER METHODS
    // ====================

    /**
     * Find all patches that contain a given vertex
     * @param v The vertex to search for
     * @return Vector of patches containing this vertex
     */
    std::vector<Patch*> patchByVertex(const Point& v) {
        std::vector<Patch*> result;

        for (auto& patch : patches) {
            // Check if vertex is in patch shape
            for (const auto& vertex : patch.shape.vertices) {
                if (Point::distance(vertex, v) < 0.01f) {
                    result.push_back(&patch);
                    break;
                }
            }
        }

        return result;
    }

    /**
     * Get neighboring patch across a shared vertex
     * @param patch The patch to find neighbor of
     * @param vertex The shared vertex
     * @return Pointer to neighboring patch, or nullptr if not found
     */
    Patch* getNeighbour(Patch* patch, const Point& vertex) {
        if (patch == nullptr) return nullptr;

        auto candidates = patchByVertex(vertex);

        // Return first neighbor that isn't the input patch
        for (auto* candidate : candidates) {
            if (candidate != patch) {
                return candidate;
            }
        }

        return nullptr;
    }

    /**
     * Check if all neighbors of a patch are within the city
     * @param patch The patch to check
     * @return true if patch is fully enclosed by city patches
     */
    bool isEnclosed(Patch* patch) const {
        if (patch == nullptr) return false;

        // Check each vertex
        for (const auto& vertex : patch->shape.vertices) {
            // Find neighbors through this vertex
            bool hasExternalNeighbor = false;

            for (const auto& other : patches) {
                if (&other == patch) continue;

                // Check if other patch shares this vertex
                bool sharesVertex = false;
                for (const auto& otherVertex : other.shape.vertices) {
                    if (Point::distance(vertex, otherVertex) < 0.01f) {
                        sharesVertex = true;
                        break;
                    }
                }

                if (sharesVertex && !other.withinCity) {
                    hasExternalNeighbor = true;
                    break;
                }
            }

            if (hasExternalNeighbor) {
                return false;
            }
        }

        return true;
    }

private:
    /**
     * Find most central patch in a set
     * @param candidates Vector of patches to search
     * @param centerPoint Point to measure distance from
     * @return Pointer to most central patch, or nullptr if candidates empty
     */
    Patch* findMostCentral(const std::vector<Patch*>& candidates, const Point& centerPoint) {
        if (candidates.empty()) return nullptr;

        Patch* mostCentral = candidates[0];
        float minDist = Point::distance(mostCentral->shape.centroid(), centerPoint);

        for (auto* candidate : candidates) {
            float dist = Point::distance(candidate->shape.centroid(), centerPoint);
            if (dist < minDist) {
                minDist = dist;
                mostCentral = candidate;
            }
        }

        return mostCentral;
    }

    /**
     * Get border vertices of a set of patches
     * @param patchSet Vector of patches
     * @return Vector of points forming the border
     */
    std::vector<Point> getBorder(const std::vector<Patch*>& patchSet) {
        std::vector<Point> border;

        // Find edges that are only on one patch (border edges)
        for (auto* patch : patchSet) {
            for (size_t i = 0; i < patch->shape.vertices.size(); ++i) {
                size_t j = (i + 1) % patch->shape.vertices.size();
                Point v1 = patch->shape.vertices[i];
                Point v2 = patch->shape.vertices[j];

                // Check if this edge is shared with another patch in the set
                bool isShared = false;
                for (auto* other : patchSet) {
                    if (other == patch) continue;

                    if (other->shape.findEdge(v1, v2) >= 0) {
                        isShared = true;
                        break;
                    }
                }

                // If not shared, it's a border edge
                if (!isShared) {
                    // Add vertex if not already in border
                    bool v1Exists = false;
                    for (const auto& p : border) {
                        if (Point::distance(p, v1) < 0.01f) {
                            v1Exists = true;
                            break;
                        }
                    }
                    if (!v1Exists) {
                        border.push_back(v1);
                    }
                }
            }
        }

        return border;
    }

    /**
     * Smooth a path using spline curves
     * @param path Input path as vector of points
     * @return Smoothed path
     */
    std::vector<Point> smoothPath(const std::vector<Point>& path) {
        if (path.size() < 3) return path;

        std::vector<Point> smoothed;

        // Add start curve
        auto startCurve = Spline::startCurve(path[0], path[1], path[2]);
        smoothed.insert(smoothed.end(), startCurve.begin(), startCurve.end());

        // Add mid curves
        for (size_t i = 1; i < path.size() - 2; ++i) {
            auto midCurve = Spline::midCurve(path[i-1], path[i], path[i+1], path[i+2]);
            smoothed.insert(smoothed.end(), midCurve.begin(), midCurve.end());
        }

        // Add end curve
        if (path.size() >= 3) {
            auto endCurve = Spline::endCurve(path[path.size()-3], path[path.size()-2], path[path.size()-1]);
            smoothed.insert(smoothed.end(), endCurve.begin(), endCurve.end());
        }

        return smoothed;
    }
};

// Define static WARDS array
// Weight distribution creates realistic medieval city
inline const Model::WardType Model::WARDS[Model::WARD_COUNT] = {
    // Residential - most common
    {"Residential", 1}, {"Residential", 1}, {"Residential", 1}, {"Residential", 1},
    {"Residential", 1}, {"Residential", 1}, {"Residential", 1}, {"Residential", 1},
    {"Residential", 1}, {"Residential", 1}, {"Residential", 1}, {"Residential", 1},

    // Commercial/Market - common
    {"Market", 2}, {"Market", 2}, {"Market", 2}, {"Market", 2},
    {"Merchant", 2}, {"Merchant", 2},

    // Craftsmen - moderate
    {"Craftsmen", 3}, {"Craftsmen", 3}, {"Craftsmen", 3},
    {"Smithy", 3}, {"Smithy", 3},

    // Guilds - moderate
    {"Guild", 4}, {"Guild", 4},

    // Religious - rare
    {"Temple", 5}, {"Temple", 5},
    {"Church", 5}, {"Church", 5},

    // Noble - rare
    {"Noble", 6}, {"Noble", 6},

    // Special - very rare
    {"Barracks", 7},
    {"Garden", 8},
    {"Slums", 9}
};

} // namespace building
} // namespace towngenerator

// Include ward headers after Model is fully defined to avoid circular dependencies
#include "../wards/Ward.h"
#include "../wards/CommonWard.h"
#include "../wards/Castle.h"
#include "../wards/Cathedral.h"
#include "../wards/Farm.h"
#include "../wards/Park.h"
#include "../wards/Market.h"
#include "../wards/MilitaryWard.h"
#include "../wards/PatriciateWard.h"
#include "../wards/CraftsmenWard.h"
#include "../wards/MerchantWard.h"
#include "../wards/AdministrationWard.h"
#include "../wards/GateWard.h"
#include "../wards/Slum.h"

// Implementation of Model::createWards() after ward includes
inline void towngenerator::building::Model::createWards() {
    wardStorage.clear();

    for (auto* patch : inner) {
        if (patch == nullptr) continue;

        std::unique_ptr<wards::Ward> ward;

        // Special patches: citadel and plaza
        if (patch == citadel && citadelNeeded) {
            ward = std::make_unique<wards::Castle>(this, patch);
        }
        else if (patch == plaza && plazaNeeded) {
            ward = std::make_unique<wards::Market>(this, patch);
        }
        else if (templeNeeded && patch != citadel && patch != plaza) {
            // First non-special inner patch becomes cathedral
            bool hasCathedral = false;
            for (const auto& w : wardStorage) {
                if (dynamic_cast<wards::Cathedral*>(w.get())) {
                    hasCathedral = true;
                    break;
                }
            }
            if (!hasCathedral) {
                ward = std::make_unique<wards::Cathedral>(this, patch);
            }
        }

        // Assign ward types based on position and randomness
        if (!ward) {
            float distFromCenter = geom::Point::distance(patch->shape.centroid(), center);
            float cityRadius = std::sqrt(static_cast<float>(nPatches)) * 8.0f;
            float relDist = distFromCenter / cityRadius;

            // Check if patch borders the wall (for GateWard)
            bool bordersWall = false;
            if (wall) {
                bordersWall = wall->borders(*patch);
            }

            // Outer city (farms and slums)
            if (relDist > 0.7f) {
                if (utils::Random::randomFloat() < 0.3f) {
                    ward = std::make_unique<wards::Slum>(this, patch);
                } else {
                    ward = std::make_unique<wards::Farm>(this, patch);
                }
            }
            // Gate wards near walls
            else if (bordersWall && utils::Random::randomFloat() < 0.4f) {
                ward = std::make_unique<wards::GateWard>(this, patch);
            }
            // Inner city variety
            else {
                float roll = utils::Random::randomFloat();

                if (roll < 0.08f) {
                    // Parks (8%)
                    ward = std::make_unique<wards::Park>(this, patch);
                }
                else if (roll < 0.16f) {
                    // Military (8%)
                    ward = std::make_unique<wards::MilitaryWard>(this, patch);
                }
                else if (roll < 0.26f) {
                    // Craftsmen (10%)
                    ward = std::make_unique<wards::CraftsmenWard>(this, patch);
                }
                else if (roll < 0.36f) {
                    // Merchant (10%)
                    ward = std::make_unique<wards::MerchantWard>(this, patch);
                }
                else if (roll < 0.44f) {
                    // Patriciate/wealthy (8%)
                    ward = std::make_unique<wards::PatriciateWard>(this, patch);
                }
                else if (roll < 0.50f) {
                    // Administration (6%)
                    ward = std::make_unique<wards::AdministrationWard>(this, patch);
                }
                else {
                    // CommonWard residential (50%)
                    float chaos = 0.2f + utils::Random::randomFloat() * 0.3f;
                    ward = std::make_unique<wards::CommonWard>(this, patch, 8.0f, chaos, chaos);
                }
            }
        }

        // Generate building geometry
        ward->createGeometry();

        // Assign ward to patch
        patch->ward = ward.get();
        wardStorage.push_back(std::move(ward));
    }
}
