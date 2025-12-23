#include "BranchGenerator.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

uint32_t GeneratedBranch::totalVertices() const {
    uint32_t count = 0;
    for (const auto& branch : branches) {
        // Each section has segmentCount+1 vertices (extra for UV wrap)
        // We have sectionCount+1 sections (or less if broken)
        count += (branch.sectionCount + 1) * (branch.segmentCount + 1);
    }
    return count;
}

uint32_t GeneratedBranch::totalIndices() const {
    uint32_t count = 0;
    for (const auto& branch : branches) {
        // Each section has segmentCount quads, each quad = 6 indices
        count += branch.sectionCount * branch.segmentCount * 6;
    }
    return count;
}

GeneratedBranch BranchGenerator::generate(const BranchConfig& config) {
    // Default: branch starts at origin pointing up
    return generate(config, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
}

GeneratedBranch BranchGenerator::generate(const BranchConfig& config,
                                          const glm::vec3& origin,
                                          const glm::quat& orientation) {
    GeneratedBranch result;
    TreeRNG rng(config.seed);

    processBranch(config, origin, orientation, config.length, config.radius, 0, rng, result);

    return result;
}

void BranchGenerator::processBranch(const BranchConfig& config,
                                    const glm::vec3& origin,
                                    const glm::quat& orientation,
                                    float length,
                                    float radius,
                                    int level,
                                    TreeRNG& rng,
                                    GeneratedBranch& result) {
    BranchData branchData;
    branchData.origin = origin;
    branchData.orientation = orientation;
    branchData.length = length;
    branchData.radius = radius;
    branchData.level = level;
    branchData.segmentCount = config.segmentCount;

    // Calculate effective section count based on break point
    int effectiveSectionCount = config.sectionCount;
    if (config.hasBreak && config.breakPoint < 1.0f) {
        effectiveSectionCount = static_cast<int>(config.sectionCount * config.breakPoint);
        effectiveSectionCount = std::max(2, effectiveSectionCount);  // Need at least 2 sections
    }
    branchData.sectionCount = effectiveSectionCount;

    glm::quat sectionOrientation = orientation;
    glm::vec3 sectionOrigin = origin;

    float sectionLength = length / static_cast<float>(config.sectionCount);

    for (int i = 0; i <= effectiveSectionCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(config.sectionCount);
        float sectionRadius = radius;

        // Apply taper - but if broken, don't taper to point at the end
        if (config.hasBreak) {
            // Taper normally but maintain some radius at break point
            sectionRadius *= 1.0f - config.taper * t * 0.7f;
        } else {
            // Normal taper - can go to a point at the end
            if (i == effectiveSectionCount) {
                sectionRadius = 0.001f;  // Sharp point at end
            } else {
                sectionRadius *= 1.0f - config.taper * t;
            }
        }

        SectionData section;
        section.origin = sectionOrigin;
        section.orientation = sectionOrientation;
        section.radius = sectionRadius;
        branchData.sections.push_back(section);

        // Move origin to next section
        glm::vec3 up(0.0f, sectionLength, 0.0f);
        sectionOrigin += sectionOrientation * up;

        // Apply gnarliness perturbation
        float gnarliness = std::max(1.0f, 1.0f / std::sqrt(sectionRadius)) * config.gnarliness;

        glm::vec3 euler = glm::eulerAngles(sectionOrientation);
        euler.x += rng.random(gnarliness, -gnarliness);
        euler.z += rng.random(gnarliness, -gnarliness);
        sectionOrientation = glm::quat(euler);

        // Apply twist
        if (std::abs(config.twist) > 0.0001f) {
            glm::quat twist = glm::angleAxis(config.twist, glm::vec3(0.0f, 1.0f, 0.0f));
            sectionOrientation = sectionOrientation * twist;
        }

        // Apply force direction
        if (std::abs(config.forceStrength) > 0.0001f) {
            glm::vec3 forceDir = glm::normalize(config.forceDirection);

            glm::vec3 upVec(0.0f, 1.0f, 0.0f);
            float dot = glm::dot(upVec, forceDir);
            glm::quat forceQuat;

            if (dot < -0.999f) {
                forceQuat = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            } else if (dot > 0.999f) {
                forceQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            } else {
                glm::vec3 axis = glm::normalize(glm::cross(upVec, forceDir));
                float angle = std::acos(dot);
                forceQuat = glm::angleAxis(angle, axis);
            }

            float maxAngle = config.forceStrength / sectionRadius;
            float dotProduct = glm::dot(sectionOrientation, forceQuat);
            float angleBetween = 2.0f * std::acos(glm::clamp(std::abs(dotProduct), 0.0f, 1.0f));

            if (angleBetween > 0.0001f) {
                float interpT = glm::clamp(maxAngle / angleBetween, -1.0f, 1.0f);
                sectionOrientation = glm::slerp(sectionOrientation, forceQuat, interpT);
            }
        }
    }

    result.branches.push_back(branchData);

    // Generate child branches if requested and we're not too deep
    if (config.childCount > 0 && level < 2) {
        generateChildBranches(config, branchData.sections, level, rng, result);
    }
}

void BranchGenerator::generateChildBranches(const BranchConfig& config,
                                            const std::vector<SectionData>& parentSections,
                                            int level,
                                            TreeRNG& rng,
                                            GeneratedBranch& result) {
    float radialOffset = rng.random();

    for (int i = 0; i < config.childCount; ++i) {
        // Where along the parent branch this child starts
        float childStart = rng.random(1.0f, config.childStart);

        // Find sections on either side
        int sectionIndex = static_cast<int>(childStart * (parentSections.size() - 1));
        sectionIndex = glm::clamp(sectionIndex, 0, static_cast<int>(parentSections.size()) - 1);

        const SectionData& sectionA = parentSections[sectionIndex];
        const SectionData& sectionB = (sectionIndex < static_cast<int>(parentSections.size()) - 1)
                                       ? parentSections[sectionIndex + 1]
                                       : sectionA;

        // Interpolation factor
        float alpha = (parentSections.size() > 1)
            ? (childStart - static_cast<float>(sectionIndex) / (parentSections.size() - 1)) /
              (1.0f / (parentSections.size() - 1))
            : 0.0f;
        alpha = glm::clamp(alpha, 0.0f, 1.0f);

        // Interpolate origin and radius
        glm::vec3 childOrigin = glm::mix(sectionA.origin, sectionB.origin, alpha);
        float childRadius = config.radius * config.childRadiusRatio *
                           glm::mix(sectionA.radius, sectionB.radius, alpha) / config.radius;

        // Interpolate orientation
        glm::quat parentOrientation = glm::slerp(sectionB.orientation, sectionA.orientation, alpha);

        // Calculate child branch angle and radial position
        float radialAngle = 2.0f * glm::pi<float>() * (radialOffset + static_cast<float>(i) / config.childCount);
        float branchAngle = glm::radians(config.childAngle);

        glm::quat angleRotation = glm::angleAxis(-branchAngle, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat radialRotation = glm::angleAxis(radialAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat childOrientation = parentOrientation * radialRotation * angleRotation;

        float childLength = config.length * config.childLengthRatio;

        // Create child config - simpler than parent
        BranchConfig childConfig;
        childConfig.seed = config.seed + i + 1;
        childConfig.length = childLength;
        childConfig.radius = childRadius;
        childConfig.sectionCount = std::max(3, config.sectionCount / 2);
        childConfig.segmentCount = std::max(3, config.segmentCount - 1);
        childConfig.taper = config.taper;
        childConfig.gnarliness = config.gnarliness * 1.2f;  // Slightly more gnarled
        childConfig.twist = config.twist;
        childConfig.forceDirection = config.forceDirection;
        childConfig.forceStrength = config.forceStrength * 0.5f;
        childConfig.hasBreak = rng.random() < 0.3f;  // 30% chance of broken child branch
        childConfig.breakPoint = rng.random(0.8f, 0.4f);
        childConfig.childCount = 0;  // No grandchildren for detritus

        processBranch(childConfig, childOrigin, childOrientation,
                      childLength, childRadius, level + 1, rng, result);
    }
}
