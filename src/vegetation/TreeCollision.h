#pragma once

#include "TreeGenerator.h"
#include "PhysicsSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

// Generates collision shapes from tree branch data
// Uses simplified capsule shapes for performance
namespace TreeCollision {

// Configuration for collision shape generation
struct Config {
    int maxBranchLevel = 2;          // Only create collision for branches up to this level (0 = trunk only)
    float minBranchRadius = 0.05f;   // Skip branches thinner than this
    float radiusScale = 1.0f;        // Scale factor applied to branch radii
};

// Generate capsule data from tree mesh data
// Returns capsules in local tree space (relative to tree origin)
inline std::vector<PhysicsWorld::CapsuleData> generateCapsules(
    const TreeMeshData& meshData,
    const Config& config = Config{})
{
    std::vector<PhysicsWorld::CapsuleData> capsules;
    capsules.reserve(meshData.branches.size());

    for (const auto& branch : meshData.branches) {
        // Filter by branch level
        if (branch.level > config.maxBranchLevel) {
            continue;
        }

        // Filter by minimum radius
        if (branch.radius < config.minBranchRadius) {
            continue;
        }

        // Skip very short branches
        if (branch.length < 0.01f) {
            continue;
        }

        PhysicsWorld::CapsuleData capsule;

        // The branch origin is at the base of the branch
        // We need to position the capsule at the center of the branch
        // Branches grow along local Y-axis after rotation by orientation
        glm::vec3 branchDir = branch.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        capsule.localPosition = branch.origin + branchDir * (branch.length * 0.5f);

        // The capsule rotation should orient it along the branch direction
        // Jolt capsules are Y-axis aligned by default, so we use the branch orientation directly
        capsule.localRotation = branch.orientation;

        // Half-height is half the cylindrical part of the capsule
        // The total capsule height = length, so halfHeight = length/2 - radius
        // But we want the capsule to match the branch length including end caps
        float halfHeight = branch.length * 0.5f;
        if (halfHeight < branch.radius) {
            // Very short branch - make it a sphere-ish capsule
            halfHeight = 0.001f;
        }
        capsule.halfHeight = halfHeight;

        // Apply radius scaling
        capsule.radius = branch.radius * config.radiusScale;

        capsules.push_back(capsule);
    }

    return capsules;
}

// Calculate how many branches would generate capsules with given config
inline size_t countCollisionBranches(const TreeMeshData& meshData, const Config& config = Config{}) {
    size_t count = 0;
    for (const auto& branch : meshData.branches) {
        if (branch.level <= config.maxBranchLevel &&
            branch.radius >= config.minBranchRadius &&
            branch.length >= 0.01f) {
            ++count;
        }
    }
    return count;
}

} // namespace TreeCollision
