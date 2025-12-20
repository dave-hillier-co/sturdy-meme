#pragma once

#include "IBranchGeometryGenerator.h"
#include <random>
#include <glm/gtc/quaternion.hpp>

// Generates cylindrical tube geometry for branches
// Matches ez-tree's approach: applies gnarliness, twist, and growth force
// per-section to create naturally curved branches
class TubeBranchGeometry : public IBranchGeometryGenerator {
public:
    void generate(const TreeStructure& tree,
                 const TreeParameters& params,
                 std::vector<Vertex>& outVertices,
                 std::vector<uint32_t>& outIndices) override;

    const char* getName() const override { return "Tube Geometry"; }

    // Set the RNG for reproducible curvature
    void setRng(std::mt19937* rng) { rngPtr = rng; }

private:
    // Generate geometry for a single branch with per-section curvature
    // Also populates the branch's sectionData for child placement
    void generateBranchGeometry(Branch& branch,
                                const TreeParameters& params,
                                std::vector<Vertex>& outVertices,
                                std::vector<uint32_t>& outIndices);

    // Random float in range [min, max]
    float randomFloat(float min, float max);

    // Rotate quaternion 'from' towards 'to' by at most 'maxAngle' radians
    // Used for applying growth force gradually per-section
    static glm::quat rotateTowards(const glm::quat& from, const glm::quat& to, float maxAngle);

    // Create quaternion that rotates Y-up to given direction
    static glm::quat quatFromDirection(const glm::vec3& direction);

    std::mt19937* rngPtr = nullptr;
};
