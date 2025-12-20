#include "EzTreeGenerator.h"
#include "TreePresets.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void EzTreeGenerator::generate(const TreeParameters& params,
                                std::vector<Vertex>& outBranchVertices,
                                std::vector<uint32_t>& outBranchIndices,
                                std::vector<LeafInstance>& outLeaves) {
    outBranchVertices.clear();
    outBranchIndices.clear();
    outLeaves.clear();

    rng.seed(params.seed);

    // Clear the queue
    while (!branchQueue.empty()) branchQueue.pop();

    // Create trunk (level 0)
    const auto& level0 = params.branchParams[0];
    BranchJob trunk;
    trunk.origin = glm::vec3(0.0f);
    trunk.euler = glm::vec3(0.0f);
    trunk.length = level0.length;
    trunk.radius = level0.radius;
    trunk.level = 0;
    trunk.sectionCount = level0.sections;
    trunk.segmentCount = level0.segments;

    branchQueue.push(trunk);

    // Process all branches in queue
    while (!branchQueue.empty()) {
        BranchJob branch = branchQueue.front();
        branchQueue.pop();
        generateBranch(branch, params, outBranchVertices, outBranchIndices, outLeaves);
    }

    SDL_Log("EzTreeGenerator: Generated %zu vertices, %zu indices, %zu leaves",
            outBranchVertices.size(), outBranchIndices.size(), outLeaves.size());
}

void EzTreeGenerator::generateBranch(const BranchJob& branch,
                                      const TreeParameters& params,
                                      std::vector<Vertex>& outVertices,
                                      std::vector<uint32_t>& outIndices,
                                      std::vector<LeafInstance>& outLeaves) {
    // Index offset for this branch's geometry
    uint32_t indexOffset = static_cast<uint32_t>(outVertices.size());

    glm::vec3 sectionOrientation = branch.euler;
    glm::vec3 sectionOrigin = branch.origin;

    // Section length calculation - matches ez-tree exactly
    // ez-tree: branch.length / branch.sectionCount / (type === 'Deciduous' ? levels - 1 : 1)
    float sectionLength = branch.length / static_cast<float>(branch.sectionCount);
    if (params.treeType == TreeType::Deciduous) {
        // Deciduous trees divide section length by (levels - 1) to accommodate terminal branches
        // This spreads the branch length across levels
        float divisor = std::max(1.0f, static_cast<float>(params.branchLevels - 1));
        sectionLength /= divisor;
    }

    // Get level parameters
    int levelIdx = std::min(branch.level, 3);
    const auto& levelParams = params.branchParams[levelIdx];
    float taper = levelParams.taper;
    float gnarliness = levelParams.gnarliness;
    float twist = levelParams.twist;

    // Track sections for child branch spawning
    std::vector<SectionInfo> sections;

    // Texture scale
    glm::vec2 texScale = params.barkTextureScale;

    // Generate geometry section by section
    for (int i = 0; i <= branch.sectionCount; ++i) {
        float sectionRadius = branch.radius;

        // Calculate taper - matches ez-tree exactly
        if (i == branch.sectionCount && branch.level == params.branchLevels) {
            sectionRadius = 0.001f;
        } else if (params.treeType == TreeType::Deciduous) {
            sectionRadius *= 1.0f - taper * (static_cast<float>(i) / static_cast<float>(branch.sectionCount));
        } else {
            // Evergreen: full taper
            sectionRadius *= 1.0f - (static_cast<float>(i) / static_cast<float>(branch.sectionCount));
        }

        // Get rotation matrix for current orientation
        glm::mat3 rotMat = eulerToMatrix(sectionOrientation);

        // Generate vertices for this section ring
        Vertex firstVertex;
        for (int j = 0; j < branch.segmentCount; ++j) {
            float angle = (2.0f * static_cast<float>(M_PI) * static_cast<float>(j)) / static_cast<float>(branch.segmentCount);

            // Create vertex in local space: (cos, 0, sin) * radius
            // Then apply rotation and add origin
            glm::vec3 localPos(std::cos(angle) * sectionRadius, 0.0f, std::sin(angle) * sectionRadius);
            glm::vec3 pos = rotMat * localPos + sectionOrigin;

            // Normal in local space, rotated
            glm::vec3 localNormal(std::cos(angle), 0.0f, std::sin(angle));
            glm::vec3 normal = glm::normalize(rotMat * localNormal);

            // texCoord - alternating pattern like ez-tree
            float u = static_cast<float>(j) / static_cast<float>(branch.segmentCount);
            float v = (i % 2 == 0) ? 0.0f : 1.0f;
            glm::vec2 texCoord(u * texScale.x, v * texScale.y);

            // Tangent
            glm::vec3 localTangent(-std::sin(angle), 0.0f, std::cos(angle));
            glm::vec3 tangentDir = glm::normalize(rotMat * localTangent);
            glm::vec4 tangent(tangentDir, 1.0f);

            // Color
            glm::vec4 color(params.barkTint, 1.0f);

            Vertex vert = {pos, normal, texCoord, tangent, color};
            outVertices.push_back(vert);

            if (j == 0) {
                firstVertex = vert;
            }
        }

        // Duplicate first vertex for UV continuity (like ez-tree)
        firstVertex.texCoord.x = 1.0f * texScale.x;
        outVertices.push_back(firstVertex);

        // Store section info for child branch spawning
        SectionInfo section;
        section.origin = sectionOrigin;
        section.euler = sectionOrientation;
        section.radius = sectionRadius;
        sections.push_back(section);

        // Advance origin along current up direction
        glm::vec3 advance(0.0f, sectionLength, 0.0f);
        sectionOrigin += rotMat * advance;

        // Scale radius back to ez-tree scale for gnarliness and force calculations
        float scaledRadius = sectionRadius / TreePresets::SCALE_FACTOR;

        // Apply gnarliness - random perturbation scaled by 1/sqrt(radius)
        // ez-tree formula: max(1, 1/sqrt(radius)) * gnarliness
        float gnarlScale = std::max(1.0f, 1.0f / std::sqrt(std::max(0.01f, scaledRadius))) * std::abs(gnarliness);
        sectionOrientation.x += randomFloat(-gnarlScale, gnarlScale);
        sectionOrientation.z += randomFloat(-gnarlScale, gnarlScale);

        // Convert to quaternion for twist and force application (matches ez-tree exactly)
        glm::quat qSection = glm::quat(sectionOrientation);

        // Apply twist as quaternion multiplication (rotation around local Y axis)
        glm::quat qTwist = glm::angleAxis(twist, glm::vec3(0.0f, 1.0f, 0.0f));
        qSection = qSection * qTwist;

        // Apply growth force via rotateTowards (like ez-tree)
        // Target: quaternion that aligns Y-up with force direction
        glm::vec3 forceDir = glm::normalize(params.growthDirection);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        float dot = glm::dot(up, forceDir);

        glm::quat qForce;
        if (dot > 0.9999f) {
            qForce = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
        } else if (dot < -0.9999f) {
            qForce = glm::angleAxis(static_cast<float>(M_PI), glm::vec3(1.0f, 0.0f, 0.0f));
        } else {
            glm::vec3 axis = glm::normalize(glm::cross(up, forceDir));
            float forceAngle = std::acos(dot);
            qForce = glm::angleAxis(forceAngle, axis);
        }

        // Three.js rotateTowards: step = strength / radius, t = min(1, step / angle)
        // Use UNSCALED radius here to match ez-tree behavior (force is in original scale)
        if (std::abs(params.growthInfluence) > 0.0001f && sectionRadius > 0.001f) {
            float step = params.growthInfluence / scaledRadius;
            float angleBetween = glm::angle(qSection * glm::inverse(qForce));
            if (angleBetween > 0.0001f) {
                float t = std::min(1.0f, std::abs(step) / angleBetween);
                if (step < 0.0f) {
                    // Negative strength: slerp with negative t rotates away
                    qSection = glm::slerp(qSection, qForce, -t);
                } else {
                    qSection = glm::slerp(qSection, qForce, t);
                }
            }
        }

        // Convert back to Euler for next iteration
        sectionOrientation = glm::eulerAngles(qSection);
    }

    // Generate indices for this branch
    generateBranchIndices(indexOffset, branch.sectionCount, branch.segmentCount, outIndices);

    // For deciduous trees, add terminal branch from end of parent
    if (params.treeType == TreeType::Deciduous && branch.level < params.branchLevels) {
        const SectionInfo& lastSection = sections.back();
        int nextLevel = branch.level + 1;
        int nextLevelIdx = std::min(nextLevel, 3);
        const auto& nextLevelParams = params.branchParams[nextLevelIdx];

        BranchJob terminal;
        terminal.origin = lastSection.origin;
        terminal.euler = lastSection.euler;
        terminal.length = nextLevelParams.length;
        terminal.radius = lastSection.radius;
        terminal.level = nextLevel;
        // IMPORTANT: Terminal branch continues from parent end, so it must use
        // the SAME section/segment count as parent for geometry continuity
        // (matches ez-tree behavior - see tree.js line 236-238)
        terminal.sectionCount = branch.sectionCount;
        terminal.segmentCount = branch.segmentCount;
        branchQueue.push(terminal);
    }

    // Generate leaves at final level
    if (branch.level == params.branchLevels) {
        generateLeaves(sections, params, outLeaves);
    } else if (branch.level < params.branchLevels) {
        // Generate child branches
        generateChildBranches(levelParams.children, branch.level + 1, sections, params);
    }
}

void EzTreeGenerator::generateBranchIndices(uint32_t indexOffset,
                                             int sectionCount,
                                             int segmentCount,
                                             std::vector<uint32_t>& outIndices) {
    // Vertices per ring = segmentCount + 1 (for UV wrap)
    int vertsPerRing = segmentCount + 1;

    for (int i = 0; i < sectionCount; ++i) {
        for (int j = 0; j < segmentCount; ++j) {
            uint32_t current = indexOffset + i * vertsPerRing + j;
            uint32_t next = current + 1;
            uint32_t below = current + vertsPerRing;
            uint32_t belowNext = below + 1;

            // Two triangles per quad - reversed winding for correct face orientation
            outIndices.push_back(current);
            outIndices.push_back(below);
            outIndices.push_back(next);

            outIndices.push_back(next);
            outIndices.push_back(below);
            outIndices.push_back(belowNext);
        }
    }
}

void EzTreeGenerator::generateChildBranches(int count,
                                             int level,
                                             const std::vector<SectionInfo>& sections,
                                             const TreeParameters& params) {
    if (sections.empty()) return;

    int levelIdx = std::min(level, 3);
    const auto& levelParams = params.branchParams[levelIdx];

    float radialOffset = randomFloat(0.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        // Determine position along parent branch (0 to 1)
        float childBranchStart = randomFloat(levelParams.start, 1.0f);

        // Find which sections are on either side
        int sectionIndex = static_cast<int>(childBranchStart * (sections.size() - 1));
        sectionIndex = std::min(sectionIndex, static_cast<int>(sections.size()) - 1);

        const SectionInfo& sectionA = sections[sectionIndex];
        const SectionInfo& sectionB = (sectionIndex < static_cast<int>(sections.size()) - 1)
                                       ? sections[sectionIndex + 1]
                                       : sectionA;

        // Interpolation factor
        float alpha = 0.0f;
        if (sections.size() > 1) {
            float sectionT = static_cast<float>(sectionIndex) / static_cast<float>(sections.size() - 1);
            float nextT = static_cast<float>(sectionIndex + 1) / static_cast<float>(sections.size() - 1);
            if (nextT > sectionT) {
                alpha = (childBranchStart - sectionT) / (nextT - sectionT);
            }
        }

        // Interpolate origin
        glm::vec3 childOrigin = glm::mix(sectionA.origin, sectionB.origin, alpha);

        // Interpolate radius
        float childRadius = levelParams.radius * glm::mix(sectionA.radius, sectionB.radius, alpha);

        // Interpolate orientation via quaternion slerp
        // Note: glm::quat(vec3) constructs quaternion from Euler angles
        glm::quat qA = glm::quat(sectionA.euler);
        glm::quat qB = glm::quat(sectionB.euler);
        // Handle quaternion double-cover: ensure we slerp the short way
        if (glm::dot(qA, qB) < 0.0f) {
            qB = -qB;
        }
        glm::quat parentQuat = glm::slerp(qA, qB, alpha);

        // Calculate branch angle and radial position
        float radialAngle = 2.0f * static_cast<float>(M_PI) * (radialOffset + static_cast<float>(i) / static_cast<float>(count));
        float branchAngle = glm::radians(levelParams.angle);

        // Build child orientation: parent * radial rotation * tilt
        glm::quat q1 = glm::angleAxis(branchAngle, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat q2 = glm::angleAxis(radialAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat q3 = parentQuat;

        glm::quat childQuat = q3 * q2 * q1;
        glm::vec3 childEuler = glm::eulerAngles(childQuat);

        // Calculate length (for evergreen, scale by position)
        float childLength = levelParams.length;
        if (params.treeType == TreeType::Evergreen) {
            childLength *= (1.0f - childBranchStart);
        }

        BranchJob child;
        child.origin = childOrigin;
        child.euler = childEuler;
        child.length = childLength;
        child.radius = childRadius;
        child.level = level;
        child.sectionCount = levelParams.sections;
        child.segmentCount = levelParams.segments;

        branchQueue.push(child);
    }
}

void EzTreeGenerator::generateLeaves(const std::vector<SectionInfo>& sections,
                                      const TreeParameters& params,
                                      std::vector<LeafInstance>& outLeaves) {
    // Generate leaves along the branch sections
    for (const auto& section : sections) {
        // Add some leaves around this section
        int leavesPerSection = 2;
        for (int i = 0; i < leavesPerSection; ++i) {
            float angle = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float tiltAngle = randomFloat(0.3f, 1.2f);

            glm::vec3 leafEuler = section.euler;
            leafEuler.y += angle;
            leafEuler.x += tiltAngle;

            generateLeaf(section.origin, leafEuler, params, outLeaves);
        }
    }
}

void EzTreeGenerator::generateLeaf(const glm::vec3& origin,
                                    const glm::vec3& euler,
                                    const TreeParameters& params,
                                    std::vector<LeafInstance>& outLeaves) {
    LeafInstance leaf;
    leaf.position = origin;
    leaf.orientation = glm::quat(euler);
    leaf.size = params.leafSize * randomFloat(0.8f, 1.2f);
    outLeaves.push_back(leaf);
}

glm::vec3 EzTreeGenerator::applyEuler(const glm::vec3& v, const glm::vec3& euler) {
    return eulerToMatrix(euler) * v;
}

glm::mat3 EzTreeGenerator::eulerToMatrix(const glm::vec3& euler) {
    // Build rotation matrix from Euler angles (XYZ order like Three.js)
    glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), euler.x, glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, euler.y, glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, euler.z, glm::vec3(0, 0, 1));
    return glm::mat3(rotMat);
}

float EzTreeGenerator::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng);
}
