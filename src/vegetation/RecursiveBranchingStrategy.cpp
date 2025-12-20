#include "RecursiveBranchingStrategy.h"
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void RecursiveBranchingStrategy::generate(const TreeParameters& params,
                                          std::mt19937& rng,
                                          TreeStructure& outTree) {
    rngPtr = &rng;

    // Create trunk as root branch
    glm::vec3 trunkStart(0.0f, 0.0f, 0.0f);
    glm::quat trunkOrientation(1.0f, 0.0f, 0.0f, 0.0f);

    const auto& trunkLevelParams = params.branchParams[0];
    float trunkLength = trunkLevelParams.length;
    float trunkRadius = trunkLevelParams.radius;

    Branch::Properties trunkProps;
    trunkProps.length = trunkLength;
    trunkProps.startRadius = trunkRadius;
    trunkProps.endRadius = trunkRadius * trunkLevelParams.taper;
    trunkProps.level = 0;
    trunkProps.radialSegments = trunkLevelParams.segments;
    trunkProps.lengthSegments = trunkLevelParams.sections;

    Branch trunk(trunkStart, trunkOrientation, trunkProps);

    // Compute section data for trunk (ez-tree style curvature)
    auto trunkSections = computeSectionData(params, trunkStart, trunkOrientation,
                                            trunkLength, trunkRadius, 0);
    trunk.setSectionData(std::vector<SectionData>(trunkSections));

    // Generate child branches recursively
    if (params.branchLevels > 0) {
        generateBranch(params, trunk, trunkSections, 0);
    }

    outTree.setRoot(std::move(trunk));

    SDL_Log("RecursiveBranchingStrategy: Generated tree with %zu branches",
            outTree.getTotalBranchCount());
}

std::vector<SectionData> RecursiveBranchingStrategy::computeSectionData(
    const TreeParameters& params,
    const glm::vec3& startPos,
    const glm::quat& orientation,
    float length,
    float radius,
    int level) {

    int levelIdx = std::min(level, 3);
    const auto& levelParams = params.branchParams[levelIdx];
    int lengthSegments = levelParams.sections;
    float taper = levelParams.taper;
    float gnarliness = levelParams.gnarliness;
    float twist = levelParams.twist;

    float sectionLength = length / static_cast<float>(lengthSegments);

    std::vector<SectionData> sections;
    sections.reserve(lengthSegments + 1);

    glm::quat sectionOrientation = orientation;
    glm::vec3 center = startPos;

    // Prepare growth force quaternion
    glm::vec3 growthDir = glm::normalize(params.growthDirection);
    glm::quat forceQuat;

    // Create quaternion from Y-up to growth direction
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(growthDir, up)) > 0.999f) {
        forceQuat = (growthDir.y > 0.0f) ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
                                          : glm::angleAxis(static_cast<float>(M_PI), glm::vec3(1.0f, 0.0f, 0.0f));
    } else {
        glm::vec3 axis = glm::normalize(glm::cross(up, growthDir));
        float angle = std::acos(std::clamp(glm::dot(up, growthDir), -1.0f, 1.0f));
        forceQuat = glm::angleAxis(angle, axis);
    }

    for (int ring = 0; ring <= lengthSegments; ++ring) {
        float t = static_cast<float>(ring) / static_cast<float>(lengthSegments);

        // Calculate radius with taper
        float sectionRadius = radius;
        if (ring == lengthSegments && level == params.branchLevels) {
            sectionRadius = 0.001f;
        } else if (params.treeType == TreeType::Deciduous) {
            sectionRadius *= (1.0f - taper * t);
        } else {
            sectionRadius *= (1.0f - t);
        }

        sections.push_back({center, sectionOrientation, sectionRadius});

        if (ring < lengthSegments) {
            // Advance center
            glm::vec3 sectionDir = sectionOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
            center += sectionDir * sectionLength;

            // Apply gnarliness
            if (std::abs(gnarliness) > 0.0001f && sectionRadius > 0.001f) {
                float gnarlScale = std::max(1.0f, 1.0f / std::sqrt(sectionRadius));
                float gnarlAmount = gnarliness * gnarlScale;
                float rx = randomFloat(-gnarlAmount, gnarlAmount);
                float rz = randomFloat(-gnarlAmount, gnarlAmount);
                glm::quat gnarlRot = glm::quat(glm::vec3(rx, 0.0f, rz));
                sectionOrientation = glm::normalize(sectionOrientation * gnarlRot);
            }

            // Apply twist
            if (std::abs(twist) > 0.0001f) {
                glm::quat twistRot = glm::angleAxis(twist, glm::vec3(0.0f, 1.0f, 0.0f));
                sectionOrientation = glm::normalize(sectionOrientation * twistRot);
            }

            // Apply growth force
            if (std::abs(params.growthInfluence) > 0.0001f && sectionRadius > 0.001f) {
                float forceStrength = std::abs(params.growthInfluence) / sectionRadius;

                // rotateTowards implementation
                glm::quat target = forceQuat;
                float dotProduct = glm::dot(sectionOrientation, target);
                if (dotProduct < 0.0f) {
                    target = -target;
                    dotProduct = -dotProduct;
                }

                if (dotProduct < 0.9999f) {
                    float angle = std::acos(std::clamp(dotProduct, -1.0f, 1.0f)) * 2.0f;
                    if (forceStrength < angle) {
                        float interpT = forceStrength / angle;
                        sectionOrientation = glm::normalize(glm::slerp(sectionOrientation, target, interpT));
                    }
                }
            }
        }
    }

    return sections;
}

void RecursiveBranchingStrategy::generateBranch(const TreeParameters& params,
                                                 Branch& parentBranch,
                                                 const std::vector<SectionData>& parentSections,
                                                 int level) {
    if (level >= params.branchLevels) return;
    if (parentSections.empty()) return;

    int levelIdx = std::min(level, 3);
    const auto& levelParams = params.branchParams[levelIdx];

    int nextLevelIdx = std::min(level + 1, 3);
    const auto& nextLevelParams = params.branchParams[nextLevelIdx];

    float childStartT = nextLevelParams.start;
    int numChildren = levelParams.children;

    // Get last section for terminal branch
    const auto& lastSection = parentSections.back();

    // For deciduous trees, add a terminal branch from the end
    if (params.treeType == TreeType::Deciduous && level < params.branchLevels) {
        glm::vec3 terminalStart = lastSection.origin;
        glm::quat terminalOrientation = lastSection.orientation;

        // Apply gnarliness to terminal branch
        float gnarlAmount = levelParams.gnarliness;
        if (std::abs(gnarlAmount) > 0.0f) {
            float maxAngle = gnarlAmount * 0.25f; // Smaller variation for terminal
            float rx = randomFloat(-maxAngle, maxAngle);
            float rz = randomFloat(-maxAngle, maxAngle);
            glm::quat variation = glm::quat(glm::vec3(rx, 0.0f, rz));
            terminalOrientation = glm::normalize(terminalOrientation * variation);
        }

        float terminalRadius = lastSection.radius;
        float terminalLength = nextLevelParams.length;

        Branch::Properties terminalProps;
        terminalProps.length = terminalLength;
        terminalProps.startRadius = terminalRadius;

        bool isTerminal = (level + 1 >= params.branchLevels);
        terminalProps.endRadius = isTerminal ? 0.001f : terminalRadius * nextLevelParams.taper;
        terminalProps.level = level + 1;
        terminalProps.radialSegments = nextLevelParams.segments;
        terminalProps.lengthSegments = nextLevelParams.sections;

        Branch& terminalBranch = parentBranch.addChild(terminalStart, terminalOrientation, terminalProps);

        // Compute section data for terminal branch
        auto terminalSections = computeSectionData(params, terminalStart, terminalOrientation,
                                                   terminalLength, terminalRadius, level + 1);
        terminalBranch.setSectionData(std::vector<SectionData>(terminalSections));

        generateBranch(params, terminalBranch, terminalSections, level + 1);
    }

    // Random radial offset for child distribution (ez-tree style)
    float radialOffset = randomFloat(0.0f, 1.0f);

    // Spawn child branches (side branches)
    for (int i = 0; i < numChildren; ++i) {
        // ez-tree style: random position between start and end
        float t = randomFloat(childStartT, 1.0f);

        // Find adjacent sections for interpolation
        int numSections = static_cast<int>(parentSections.size());
        float sectionPos = t * static_cast<float>(numSections - 1);
        int sectionIdx = static_cast<int>(sectionPos);
        sectionIdx = std::clamp(sectionIdx, 0, numSections - 2);

        const auto& sectionA = parentSections[sectionIdx];
        const auto& sectionB = parentSections[sectionIdx + 1];

        // Interpolation factor between sections
        float alpha = sectionPos - static_cast<float>(sectionIdx);
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        // Interpolate position, orientation, and radius
        glm::vec3 childStart = glm::mix(sectionA.origin, sectionB.origin, alpha);
        glm::quat parentOrient = glm::slerp(sectionA.orientation, sectionB.orientation, alpha);
        float parentRadius = glm::mix(sectionA.radius, sectionB.radius, alpha);

        // Calculate child radius as multiplier on parent radius
        float childRadius = nextLevelParams.radius * parentRadius;
        float childLength = nextLevelParams.length;

        // Evergreen length scaling (ez-tree style)
        if (params.treeType == TreeType::Evergreen) {
            childLength *= (1.0f - t);
        }

        // Calculate child orientation (ez-tree style radial distribution)
        float radialAngle = 2.0f * static_cast<float>(M_PI) * (radialOffset + static_cast<float>(i) / static_cast<float>(std::max(1, numChildren)));

        float branchAngleRad = glm::radians(nextLevelParams.angle);

        // Build child orientation relative to interpolated parent
        glm::quat radialRot = glm::angleAxis(radialAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat tiltRot = glm::angleAxis(branchAngleRad, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat childOrientation = parentOrient * radialRot * tiltRot;

        // Apply gnarliness to initial orientation
        float gnarlAmount = levelParams.gnarliness;
        if (std::abs(gnarlAmount) > 0.0f) {
            float maxAngle = std::abs(gnarlAmount) * 0.5f;
            float rx = randomFloat(-maxAngle, maxAngle);
            float ry = randomFloat(-maxAngle, maxAngle);
            float rz = randomFloat(-maxAngle, maxAngle);
            glm::quat variation = glm::quat(glm::vec3(rx, ry, rz));
            childOrientation = glm::normalize(childOrientation * variation);
        }

        Branch::Properties childProps;
        childProps.length = childLength;
        childProps.startRadius = childRadius;

        bool isTerminal = (level + 1 >= params.branchLevels);
        childProps.endRadius = isTerminal ? 0.001f : childRadius * nextLevelParams.taper;
        childProps.level = level + 1;
        childProps.radialSegments = nextLevelParams.segments;
        childProps.lengthSegments = nextLevelParams.sections;

        Branch& childBranch = parentBranch.addChild(childStart, childOrientation, childProps);

        // Compute section data for child branch
        auto childSections = computeSectionData(params, childStart, childOrientation,
                                                childLength, childRadius, level + 1);
        childBranch.setSectionData(std::vector<SectionData>(childSections));

        generateBranch(params, childBranch, childSections, level + 1);
    }
}

float RecursiveBranchingStrategy::randomFloat(float min, float max) {
    if (!rngPtr) return (min + max) * 0.5f;
    std::uniform_real_distribution<float> dist(min, max);
    return dist(*rngPtr);
}
