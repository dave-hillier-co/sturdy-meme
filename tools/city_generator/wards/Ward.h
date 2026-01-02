#pragma once

#include "../geom/Point.h"
#include "../geom/Polygon.h"
#include "../geom/GeomUtils.h"
#include "../utils/Random.h"
#include "../building/Cutter.h"
#include "../building/Patch.h"
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <cmath>

namespace towngenerator {

// Forward declarations
namespace building {
class Model;
class Patch;
}

namespace wards {

using geom::Point;
using geom::Polygon;
using geom::GeomUtils;
using utils::Random;
using building::Cutter;
using building::Patch;
using building::Model;

/**
 * Ward - Base class for city districts/wards
 * Ported from Haxe Ward class
 *
 * Manages building generation within a patch based on ward type
 */
class Ward {
public:
    // ====================
    // CONSTANTS - Street widths
    // ====================

    static constexpr float MAIN_STREET = 2.0f;
    static constexpr float REGULAR_STREET = 1.0f;
    static constexpr float ALLEY = 0.6f;

    // ====================
    // MEMBER VARIABLES
    // ====================

    Model* model;
    Patch* patch;
    std::vector<Polygon> geometry;

    // ====================
    // CONSTRUCTOR
    // ====================

    Ward(Model* model, Patch* patch)
        : model(model)
        , patch(patch)
    {}

    virtual ~Ward() = default;

    // ====================
    // VIRTUAL METHODS - Override in subclasses
    // ====================

    /**
     * Create building geometry for this ward
     * Override in subclasses to implement ward-specific building generation
     */
    virtual void createGeometry() {
        geometry.clear();
    }

    /**
     * Get display label for this ward type
     * Override in subclasses to return ward name
     * @return Ward type label or nullptr for base class
     */
    virtual const char* getLabel() const {
        return nullptr;
    }

    // ====================
    // STATIC METHOD - Ward rating
    // ====================

    /**
     * Rate how suitable a patch is for this ward type
     * Override in subclasses to implement ward-specific rating logic
     *
     * @param model The city model
     * @param patch The patch to rate
     * @return Suitability score (higher is better)
     */
    static float rateLocation(Model* model, Patch* patch) {
        return 0.0f;
    }

    // ====================
    // PUBLIC METHODS
    // ====================

    /**
     * Get the buildable city block area
     * Insets the patch shape based on adjacent streets/walls
     *
     * Returns the buildable area after accounting for:
     * - Wall borders: inset by MAIN_STREET
     * - Street borders: inset by REGULAR_STREET
     * - Alley borders: inset by ALLEY
     *
     * @return Polygon representing buildable area
     */
    Polygon getCityBlock() {
        if (patch == nullptr) {
            return Polygon();
        }

        // Calculate inset distances for each edge
        std::vector<float> insets;
        insets.reserve(patch->shape.vertices.size());

        for (size_t i = 0; i < patch->shape.vertices.size(); ++i) {
            // Default inset for alleys
            float inset = ALLEY;

            // Check if edge borders a wall or street
            // For now, use simple heuristic:
            // - If patch is on city edge (withinCity but not withinWalls), use REGULAR_STREET
            // - If patch is within walls, use ALLEY
            // - If patch is outside city, use MAIN_STREET

            if (!patch->withinCity) {
                inset = MAIN_STREET;
            } else if (!patch->withinWalls) {
                inset = REGULAR_STREET;
            } else {
                inset = ALLEY;
            }

            insets.push_back(inset);
        }

        // Apply insets to create buildable block
        return patch->shape.shrink(insets);
    }

    // ====================
    // STATIC METHODS - Building subdivision
    // ====================

    /**
     * Recursively subdivide polygon into building footprints
     *
     * Algorithm:
     * 1. Find longest edge
     * 2. Bisect polygon along that edge
     * 3. Recursively subdivide each half until minSq reached
     * 4. Randomly leave some lots empty based on emptyProb
     *
     * @param p Polygon to subdivide
     * @param minSq Minimum area threshold (stop subdividing below this)
     * @param gridChaos Randomness in cut position (0=regular, 1=chaotic)
     * @param sizeChaos Randomness in cut rotation (0=perpendicular, 1=chaotic)
     * @param emptyProb Probability of leaving a lot empty (default 0.04)
     * @param split Whether to perform split (default true)
     * @return Array of building footprint polygons
     */
    static std::vector<Polygon> createAlleys(
        const Polygon& p,
        float minSq,
        float gridChaos,
        float sizeChaos,
        float emptyProb = 0.04f,
        bool split = true
    ) {
        std::vector<Polygon> result;

        // Base case: if area is below threshold, return as-is (maybe)
        float area = p.square();
        if (area < minSq) {
            // Randomly decide if this lot should be empty
            if (Random::randomFloat() > emptyProb) {
                result.push_back(p);
            }
            return result;
        }

        // If split is false, return the polygon
        if (!split) {
            result.push_back(p);
            return result;
        }

        // Find longest edge
        size_t longestEdge = 0;
        float longestLength = 0.0f;

        for (size_t i = 0; i < p.vertices.size(); ++i) {
            size_t j = (i + 1) % p.vertices.size();
            float length = Point::distance(p.vertices[i], p.vertices[j]);

            if (length > longestLength) {
                longestLength = length;
                longestEdge = i;
            }
        }

        // Calculate bisection parameters
        // ratio: where along the edge to start the cut (0.5 + chaos)
        float ratio = 0.5f + (Random::randomFloat() - 0.5f) * gridChaos;
        ratio = std::max(0.2f, std::min(0.8f, ratio)); // Clamp to reasonable range

        // rotation: angle for the cut (perpendicular + chaos)
        // Perpendicular to edge is PI/2
        float rotation = static_cast<float>(M_PI / 2.0);
        rotation += (Random::randomFloat() - 0.5f) * sizeChaos;

        // Gap between resulting polygons (alley width)
        float gap = ALLEY;

        // Bisect the polygon
        auto parts = Cutter::bisect(p, longestEdge, ratio, rotation, gap);

        // If bisection failed, return original
        if (parts.size() < 2) {
            result.push_back(p);
            return result;
        }

        // Recursively subdivide both halves
        auto parts1 = createAlleys(parts[0], minSq, gridChaos, sizeChaos, emptyProb, split);
        auto parts2 = createAlleys(parts[1], minSq, gridChaos, sizeChaos, emptyProb, split);

        // Combine results
        result.insert(result.end(), parts1.begin(), parts1.end());
        result.insert(result.end(), parts2.begin(), parts2.end());

        return result;
    }

    /**
     * Create orthogonal building subdivisions
     * Slices polygon along longest edge with orthogonal cuts
     *
     * Algorithm:
     * 1. Find longest edge
     * 2. Make orthogonal cuts parallel to that edge
     * 3. Fill ratio determines how much of polygon is subdivided
     *
     * @param poly Polygon to subdivide
     * @param minBlockSq Minimum block area
     * @param fill Fill ratio (0-1) - how much to subdivide
     * @return Array of building polygons
     */
    static std::vector<Polygon> createOrthoBuilding(
        const Polygon& poly,
        float minBlockSq,
        float fill
    ) {
        std::vector<Polygon> result;

        // If polygon is too small, return as-is
        if (poly.square() < minBlockSq) {
            result.push_back(poly);
            return result;
        }

        // Find longest edge
        size_t longestEdge = 0;
        float longestLength = 0.0f;

        for (size_t i = 0; i < poly.vertices.size(); ++i) {
            size_t j = (i + 1) % poly.vertices.size();
            float length = Point::distance(poly.vertices[i], poly.vertices[j]);

            if (length > longestLength) {
                longestLength = length;
                longestEdge = i;
            }
        }

        // Make orthogonal cuts along the longest edge
        // Number of slices based on area and fill ratio
        float area = poly.square();
        int numSlices = static_cast<int>(std::sqrt(area / minBlockSq) * fill);
        numSlices = std::max(1, std::min(numSlices, 10)); // Clamp to reasonable range

        // Create slices by bisecting repeatedly
        std::vector<Polygon> currentPolys = {poly};

        for (int i = 0; i < numSlices && !currentPolys.empty(); ++i) {
            std::vector<Polygon> nextPolys;

            for (const auto& current : currentPolys) {
                // Find longest edge of current polygon
                size_t edge = 0;
                float maxLen = 0.0f;

                for (size_t e = 0; e < current.vertices.size(); ++e) {
                    size_t next = (e + 1) % current.vertices.size();
                    float len = Point::distance(current.vertices[e], current.vertices[next]);
                    if (len > maxLen) {
                        maxLen = len;
                        edge = e;
                    }
                }

                // Bisect orthogonally (perpendicular to edge)
                float ratio = 0.5f;
                float rotation = static_cast<float>(M_PI / 2.0); // Perpendicular
                float gap = ALLEY;

                auto parts = Cutter::bisect(current, edge, ratio, rotation, gap);

                if (parts.size() >= 2) {
                    nextPolys.insert(nextPolys.end(), parts.begin(), parts.end());
                } else {
                    nextPolys.push_back(current);
                }
            }

            currentPolys = nextPolys;
        }

        // Filter out polygons that are too small
        for (const auto& p : currentPolys) {
            if (p.square() >= minBlockSq * 0.5f) { // Allow slightly smaller than min
                result.push_back(p);
            }
        }

        // If no valid polygons, return original
        if (result.empty()) {
            result.push_back(poly);
        }

        return result;
    }

protected:
    /**
     * Filter buildings in outer patches based on population density
     *
     * For patches on the city edge, removes some buildings to create
     * sparser development at the outskirts
     */
    void filterOutskirts() {
        if (patch == nullptr || patch->withinWalls) {
            // Don't filter if within walls
            return;
        }

        // Calculate density based on distance from city center
        // Patches on the outskirts have lower density
        float density = 1.0f;

        if (!patch->withinCity) {
            // Outside city entirely - very sparse (20% density)
            density = 0.2f;
        } else if (!patch->withinWalls) {
            // Between city and walls - moderate (60% density)
            density = 0.6f;
        }

        // Randomly remove buildings based on density
        std::vector<Polygon> filtered;
        for (const auto& building : geometry) {
            if (Random::randomFloat() < density) {
                filtered.push_back(building);
            }
        }

        geometry = filtered;
    }
};

} // namespace wards
} // namespace towngenerator
