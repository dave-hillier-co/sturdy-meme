#pragma once

#include "../geom/Polygon.h"
#include "../geom/Point.h"
#include "Patch.h"
#include <vector>
#include <algorithm>

namespace towngenerator {
namespace building {

using geom::Point;
using geom::Polygon;

// Forward declaration
class Model;

/**
 * CurtainWall - represents city walls with gates and towers
 * Ported from Haxe CurtainWall class
 */
class CurtainWall {
public:
    // The wall perimeter polygon
    Polygon shape;

    // Which edges are actual wall (vs gates)
    std::vector<bool> segments;

    // Gate positions (vertices that are entrances)
    std::vector<Point> gates;

    // Tower positions
    std::vector<Point> towers;

    /**
     * Constructor - builds wall from patches
     *
     * @param real If true, uses realistic mode
     * @param model The city model
     * @param patches The patches enclosed by walls
     * @param reserved Vertices that cannot be gates
     *
     * Algorithm:
     * 1. Build shape from border vertices of patches
     * 2. Call buildGates() to identify entrances
     * 3. Call buildTowers() for remaining vertices
     */
    CurtainWall(bool real, const Model& model,
                const std::vector<Patch*>& patches,
                const std::vector<Point>& reserved)
        : shape()
        , segments()
        , gates()
        , towers()
    {
        // Build shape from border vertices of patches
        buildShape(patches);

        // Identify gate positions
        buildGates(model, patches, reserved, real);

        // Place towers at remaining vertices
        buildTowers();
    }

    /**
     * Check if a patch's edge (v0->v1) is on this wall
     *
     * @param patch The patch to check
     * @param v0 First vertex of the edge
     * @param v1 Second vertex of the edge
     * @return true if this edge is on the wall
     */
    bool bordersBy(const Patch& patch, const Point& v0, const Point& v1) const {
        // Check if the edge exists in the wall shape
        int edgeIndex = shape.findEdge(v0, v1);
        return edgeIndex >= 0;
    }

    /**
     * Check if any of patch's edges are on this wall
     *
     * @param patch The patch to check
     * @return true if patch borders the wall
     */
    bool borders(const Patch& patch) const {
        // Check each edge of the patch
        for (size_t i = 0; i < patch.shape.vertices.size(); ++i) {
            size_t j = (i + 1) % patch.shape.vertices.size();
            const Point& v0 = patch.shape.vertices[i];
            const Point& v1 = patch.shape.vertices[j];

            if (bordersBy(patch, v0, v1)) {
                return true;
            }
        }
        return false;
    }

private:
    /**
     * Build wall shape from border vertices of patches
     *
     * @param patches The patches enclosed by walls
     *
     * Algorithm:
     * 1. Collect all vertices from patch boundaries
     * 2. Find the outer perimeter by identifying border edges
     * 3. Order vertices to form a continuous polygon
     */
    void buildShape(const std::vector<Patch*>& patches) {
        if (patches.empty()) {
            return;
        }

        std::vector<Point> borderVertices;

        // Collect all unique border vertices
        // A border edge is one that appears only once across all patches
        for (const auto* patch : patches) {
            for (size_t i = 0; i < patch->shape.vertices.size(); ++i) {
                size_t j = (i + 1) % patch->shape.vertices.size();
                const Point& v0 = patch->shape.vertices[i];
                const Point& v1 = patch->shape.vertices[j];

                // Count how many patches share this edge
                int sharedCount = 0;
                for (const auto* otherPatch : patches) {
                    if (otherPatch == patch) continue;
                    if (otherPatch->shape.findEdge(v0, v1) >= 0) {
                        sharedCount++;
                        break;
                    }
                }

                // If edge is not shared, it's a border edge
                if (sharedCount == 0) {
                    // Add vertices if not already in list
                    auto it0 = std::find_if(borderVertices.begin(), borderVertices.end(),
                        [&v0](const Point& p) { return p.x == v0.x && p.y == v0.y; });
                    if (it0 == borderVertices.end()) {
                        borderVertices.push_back(v0);
                    }

                    auto it1 = std::find_if(borderVertices.begin(), borderVertices.end(),
                        [&v1](const Point& p) { return p.x == v1.x && p.y == v1.y; });
                    if (it1 == borderVertices.end()) {
                        borderVertices.push_back(v1);
                    }
                }
            }
        }

        // Create polygon from border vertices
        // For a proper implementation, we would order these vertices to form a continuous perimeter
        // For now, using the vertices as collected
        shape = Polygon(borderVertices);

        // Initialize segments - all edges are walls initially
        segments.resize(shape.vertices.size(), true);
    }

    /**
     * Build gates - identify entrance positions
     *
     * @param model The city model
     * @param patches The patches enclosed by walls
     * @param reserved Vertices that cannot be gates
     * @param real If true, uses realistic mode with road access
     *
     * Algorithm:
     * 1. For each vertex, check if it has >1 adjacent inner patches (so street can pass)
     * 2. Add to gates if not reserved and has enough spacing from other gates
     * 3. In realistic mode: may split outer patches to create road access
     */
    void buildGates(const Model& model,
                    const std::vector<Patch*>& patches,
                    const std::vector<Point>& reserved,
                    bool real) {
        const float minGateSpacing = 50.0f;  // Minimum distance between gates

        // Check each vertex
        for (size_t i = 0; i < shape.vertices.size(); ++i) {
            const Point& vertex = shape.vertices[i];

            // Skip if reserved
            bool isReserved = std::find_if(reserved.begin(), reserved.end(),
                [&vertex](const Point& p) {
                    return p.x == vertex.x && p.y == vertex.y;
                }) != reserved.end();

            if (isReserved) {
                continue;
            }

            // Count adjacent patches that include this vertex
            int adjacentPatchCount = 0;
            for (const auto* patch : patches) {
                bool hasVertex = std::find_if(
                    patch->shape.vertices.begin(),
                    patch->shape.vertices.end(),
                    [&vertex](const Point& p) {
                        return p.x == vertex.x && p.y == vertex.y;
                    }) != patch->shape.vertices.end();

                if (hasVertex && patch->withinWalls) {
                    adjacentPatchCount++;
                }
            }

            // Vertex needs multiple adjacent patches for a street to pass through
            if (adjacentPatchCount > 1) {
                // Check spacing from existing gates
                bool hasGoodSpacing = true;
                for (const Point& gate : gates) {
                    if (Point::distance(vertex, gate) < minGateSpacing) {
                        hasGoodSpacing = false;
                        break;
                    }
                }

                if (hasGoodSpacing) {
                    gates.push_back(vertex);

                    // Mark the edges touching this gate as non-wall
                    size_t prevIdx = (i + shape.vertices.size() - 1) % shape.vertices.size();
                    segments[prevIdx] = false;  // Edge coming into gate
                    segments[i] = false;         // Edge going out of gate
                }
            }
        }

        // In realistic mode, additional logic could split outer patches
        // to create road access - omitted for initial implementation
    }

    /**
     * Build towers - place towers at remaining vertices
     *
     * Algorithm:
     * For each vertex not in gates, add to towers
     */
    void buildTowers() {
        for (const Point& vertex : shape.vertices) {
            // Check if this vertex is a gate
            bool isGate = std::find_if(gates.begin(), gates.end(),
                [&vertex](const Point& p) {
                    return p.x == vertex.x && p.y == vertex.y;
                }) != gates.end();

            // If not a gate, it's a tower
            if (!isGate) {
                towers.push_back(vertex);
            }
        }
    }
};

} // namespace building
} // namespace towngenerator
