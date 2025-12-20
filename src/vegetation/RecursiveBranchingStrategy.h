#pragma once

#include "ITreeGenerationStrategy.h"
#include "Branch.h"
#include <random>
#include <vector>

// Recursive branching tree generation strategy
// Creates trees by recursively spawning child branches from parent branches
// Uses ez-tree style per-section curvature with gnarliness, twist, and growth force
class RecursiveBranchingStrategy : public ITreeGenerationStrategy {
public:
    void generate(const TreeParameters& params,
                 std::mt19937& rng,
                 TreeStructure& outTree) override;

    const char* getName() const override { return "Recursive Branching"; }

private:
    // Compute section data for a branch (ez-tree style curvature)
    // This pre-computes the curved path so children can spawn from correct positions
    std::vector<SectionData> computeSectionData(const TreeParameters& params,
                                                 const glm::vec3& startPos,
                                                 const glm::quat& orientation,
                                                 float length,
                                                 float radius,
                                                 int level);

    // Recursive branch generation using section data for child placement
    void generateBranch(const TreeParameters& params,
                       Branch& parentBranch,
                       const std::vector<SectionData>& parentSections,
                       int level);

    // Random utilities
    float randomFloat(float min, float max);

    std::mt19937* rngPtr = nullptr;
};
