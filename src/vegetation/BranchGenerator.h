#pragma once

#include "TreeGenerator.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>

// Configuration for generating a standalone branch (for detritus, etc.)
struct BranchConfig {
    uint32_t seed = 0;

    // Branch geometry
    float length = 2.0f;
    float radius = 0.15f;
    int sectionCount = 8;
    int segmentCount = 6;

    // Appearance
    float taper = 0.8f;           // Radius reduction along length (0 = no taper, 1 = full taper to point)
    float gnarliness = 0.2f;       // Random twist/curl amount
    float twist = 0.0f;            // Spiral rotation per section

    // Force/growth direction
    glm::vec3 forceDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float forceStrength = 0.0f;

    // Break point - fraction along branch (0-1) where it appears broken off
    // 0.0 = no break (pointed tip), 1.0 = full length with flat end
    float breakPoint = 0.0f;
    bool hasBreak = false;        // If true, end abruptly at breakPoint with flat cap

    // Child branches (sub-branches growing off this one)
    int childCount = 0;           // Number of child branches
    float childStart = 0.3f;      // Where children start (0-1 along parent)
    float childAngle = 45.0f;     // Angle of child branches (degrees)
    float childLengthRatio = 0.5f; // Child length as ratio of parent
    float childRadiusRatio = 0.5f; // Child radius as ratio of parent
};

// Result of generating a branch
struct GeneratedBranch {
    std::vector<BranchData> branches;  // Main branch + any children

    // Get total vertex/index counts for buffer allocation
    uint32_t totalVertices() const;
    uint32_t totalIndices() const;
};

// Standalone branch generator - extracts core logic from TreeGenerator
class BranchGenerator {
public:
    BranchGenerator() = default;

    // Generate a standalone branch with optional children
    GeneratedBranch generate(const BranchConfig& config);

    // Generate a branch at a specific position and orientation
    GeneratedBranch generate(const BranchConfig& config,
                             const glm::vec3& origin,
                             const glm::quat& orientation);

private:
    void processBranch(const BranchConfig& config,
                       const glm::vec3& origin,
                       const glm::quat& orientation,
                       float length,
                       float radius,
                       int level,
                       TreeRNG& rng,
                       GeneratedBranch& result);

    void generateChildBranches(const BranchConfig& config,
                               const std::vector<SectionData>& parentSections,
                               int level,
                               TreeRNG& rng,
                               GeneratedBranch& result);
};
