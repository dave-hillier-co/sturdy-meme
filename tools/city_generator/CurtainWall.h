// CurtainWall: City fortification walls with gates and towers
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// - Wall shape is computed from patches that are "within walls"
// - Gates are placed at vertices that border multiple inner districts
// - Gates maintain minimum distance from each other
// - Towers are placed at wall vertices that aren't gates
// - Wall segments can be disabled (gaps in the wall)

#pragma once

#include "Geometry.h"
#include "Patch.h"
#include <vector>
#include <algorithm>
#include <random>

namespace city {

// Forward declaration
class Model;

class CurtainWall {
public:
    Polygon shape;                    // Wall perimeter
    std::vector<bool> segments;       // Which wall segments are active
    std::vector<Vec2> gates;          // Gate positions
    std::vector<Vec2> towers;         // Tower positions

    // Build wall around given patches
    // smooth: number of smoothing iterations
    void build(const std::vector<Patch*>& innerPatches,
               const std::vector<Patch*>& allPatches,
               int smooth = 2);

    // Place gates at suitable wall vertices
    // minGateDistance: minimum distance between gates
    void buildGates(const std::vector<Patch*>& innerPatches,
                    float minGateDistance,
                    std::mt19937& rng);

    // Place towers at wall vertices (excluding gates)
    void buildTowers();

    // Get wall radius (max distance from center)
    float getRadius() const;

    // Check if wall borders a patch
    bool borders(const Patch& patch) const;

    // Check if a point is inside the wall
    bool contains(const Vec2& p) const {
        return shape.contains(p);
    }

private:
    // Find wall vertices that could be gates
    // (vertices adjacent to multiple inner patches)
    std::vector<size_t> findPotentialGateIndices(
        const std::vector<Patch*>& innerPatches) const;
};

// Implementation

// Find the circumference (outer boundary) of a set of patches
// This matches the original findCircumference() in Model.hx
inline Polygon findCircumference(const std::vector<Patch*>& patches) {
    if (patches.empty()) return Polygon();
    if (patches.size() == 1) return patches[0]->shape;

    // Collect outer edges: an edge (a,b) is outer if no other patch has (b,a)
    std::vector<Vec2> A, B;  // Edge start and end points

    for (const auto* p1 : patches) {
        const auto& verts = p1->shape.vertices;
        size_t len = verts.size();

        for (size_t i = 0; i < len; i++) {
            const Vec2& a = verts[i];
            const Vec2& b = verts[(i + 1) % len];

            // Check if any other patch has the reverse edge (b, a)
            bool isOuter = true;
            for (const auto* p2 : patches) {
                if (p2 == p1) continue;

                const auto& verts2 = p2->shape.vertices;
                size_t len2 = verts2.size();
                for (size_t j = 0; j < len2; j++) {
                    // Looking for edge (b, a) in p2
                    if (verts2[j] == b && verts2[(j + 1) % len2] == a) {
                        isOuter = false;
                        break;
                    }
                }
                if (!isOuter) break;
            }

            if (isOuter) {
                A.push_back(a);
                B.push_back(b);
            }
        }
    }

    if (A.empty()) return Polygon();

    // Chain edges together to form the circumference polygon
    std::vector<Vec2> result;
    size_t index = 0;

    do {
        result.push_back(A[index]);

        // Find the edge that starts where this one ends
        const Vec2& endPoint = B[index];
        size_t nextIndex = 0;
        bool found = false;
        for (size_t i = 0; i < A.size(); i++) {
            if (A[i] == endPoint) {
                nextIndex = i;
                found = true;
                break;
            }
        }

        if (!found) break;  // Broken chain
        index = nextIndex;

    } while (index != 0 && result.size() < A.size() + 1);

    return Polygon(result);
}

inline void CurtainWall::build(const std::vector<Patch*>& innerPatches,
                               const std::vector<Patch*>& /* allPatches */,
                               int smooth) {
    if (innerPatches.empty()) return;

    if (innerPatches.size() == 1) {
        shape = innerPatches[0]->shape;
    } else {
        // Find circumference of inner patches
        shape = findCircumference(innerPatches);

        // Smooth the wall vertices (as in original)
        if (smooth > 0) {
            float smoothFactor = std::min(1.0f, 40.0f / innerPatches.size());
            for (int iter = 0; iter < smooth; iter++) {
                shape.smoothVertices(smoothFactor);
            }
        }
    }

    // Initialize all segments as active
    segments.resize(shape.size(), true);
}

inline void CurtainWall::buildGates(const std::vector<Patch*>& innerPatches,
                                    float minGateDistance,
                                    std::mt19937& rng) {
    gates.clear();

    auto potentialGates = findPotentialGateIndices(innerPatches);

    if (potentialGates.empty()) {
        // No good gate positions, pick random vertices
        if (shape.size() >= 4) {
            std::uniform_int_distribution<size_t> dist(0, shape.size() - 1);
            for (int i = 0; i < 4; i++) {
                size_t idx = dist(rng);
                gates.push_back(shape[idx]);
            }
        }
        return;
    }

    // Shuffle potential gates
    std::shuffle(potentialGates.begin(), potentialGates.end(), rng);

    // Greedily select gates maintaining minimum distance
    for (size_t idx : potentialGates) {
        const Vec2& candidate = shape[idx];

        bool tooClose = false;
        for (const auto& existing : gates) {
            if (Vec2::distance(candidate, existing) < minGateDistance) {
                tooClose = true;
                break;
            }
        }

        if (!tooClose) {
            gates.push_back(candidate);
        }
    }
}

inline void CurtainWall::buildTowers() {
    towers.clear();

    for (size_t i = 0; i < shape.size(); i++) {
        const Vec2& v = shape[i];

        // Skip if this is a gate
        bool isGate = false;
        for (const auto& g : gates) {
            if (v == g) {
                isGate = true;
                break;
            }
        }

        if (!isGate && segments[i]) {
            towers.push_back(v);
        }
    }
}

inline float CurtainWall::getRadius() const {
    float maxDist = 0.0f;
    Vec2 center = shape.centroid();
    for (const auto& v : shape.vertices) {
        maxDist = std::max(maxDist, Vec2::distance(v, center));
    }
    return maxDist;
}

inline bool CurtainWall::borders(const Patch& patch) const {
    // Check if any wall vertex is also a patch vertex
    for (const auto& wv : shape.vertices) {
        for (const auto& pv : patch.shape.vertices) {
            if (wv == pv) return true;
        }
    }
    return false;
}

inline std::vector<size_t> CurtainWall::findPotentialGateIndices(
    const std::vector<Patch*>& innerPatches) const {

    std::vector<size_t> result;

    for (size_t i = 0; i < shape.size(); i++) {
        const Vec2& v = shape[i];

        // Count how many inner patches share this vertex
        int patchCount = 0;
        for (const auto* patch : innerPatches) {
            for (const auto& pv : patch->shape.vertices) {
                if (v == pv) {
                    patchCount++;
                    break;
                }
            }
        }

        // Good gate position if shared by 2+ inner patches
        // (indicates a junction point)
        if (patchCount >= 2) {
            result.push_back(i);
        }
    }

    return result;
}

} // namespace city
