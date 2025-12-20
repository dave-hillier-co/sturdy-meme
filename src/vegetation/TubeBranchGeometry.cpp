#include "TubeBranchGeometry.h"
#include <SDL3/SDL.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void TubeBranchGeometry::generate(const TreeStructure& tree,
                                  const TreeParameters& params,
                                  std::vector<Vertex>& outVertices,
                                  std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    // Visit all branches and generate geometry
    tree.forEachBranch([this, &params, &outVertices, &outIndices](const Branch& branch) {
        generateBranchGeometry(const_cast<Branch&>(branch), params, outVertices, outIndices);
    });

    SDL_Log("TubeBranchGeometry: Generated %zu vertices, %zu indices",
            outVertices.size(), outIndices.size());
}

void TubeBranchGeometry::generateBranchGeometry(Branch& branch,
                                                 const TreeParameters& params,
                                                 std::vector<Vertex>& outVertices,
                                                 std::vector<uint32_t>& outIndices) {
    const auto& props = branch.getProperties();

    // Skip degenerate branches
    if (props.length < 0.0001f) return;
    if (props.startRadius < 0.0001f && props.endRadius < 0.0001f) return;

    int radialSegments = props.radialSegments;

    // Texture scale
    glm::vec2 texScale = params.barkTextureScale;

    uint32_t baseVertexIndex = static_cast<uint32_t>(outVertices.size());

    // Use pre-computed section data from RecursiveBranchingStrategy
    // This contains the curved path with gnarliness, twist, and growth force applied
    const auto& sections = branch.getSectionData();

    if (sections.empty()) {
        // Fallback: generate straight branch if no section data
        // This shouldn't happen with the updated RecursiveBranchingStrategy
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TubeBranchGeometry: No section data for branch at level %d, using straight geometry",
                    props.level);

        int lengthSegments = props.lengthSegments;
        int levelIdx = std::min(props.level, 3);
        const auto& levelParams = params.branchParams[levelIdx];
        float taper = levelParams.taper;

        glm::vec3 direction = branch.getOrientation() * glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 up = branch.getOrientation() * glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 right = branch.getOrientation() * glm::vec3(1.0f, 0.0f, 0.0f);
        right = glm::normalize(right);
        up = glm::normalize(up);

        glm::vec3 startPos = branch.getStartPosition();
        float baseRadius = props.startRadius;

        for (int ring = 0; ring <= lengthSegments; ++ring) {
            float t = static_cast<float>(ring) / static_cast<float>(lengthSegments);
            glm::vec3 center = startPos + direction * (t * props.length);

            float sectionRadius = baseRadius;
            if (params.treeType == TreeType::Deciduous) {
                sectionRadius *= (1.0f - taper * t);
            } else {
                sectionRadius *= (1.0f - t);
            }

            for (int i = 0; i <= radialSegments; ++i) {
                float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(radialSegments);
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);

                glm::vec3 radialDir = right * cosA + up * sinA;
                glm::vec3 pos = center + radialDir * sectionRadius;
                glm::vec3 normal = radialDir;

                float u = static_cast<float>(i) / static_cast<float>(radialSegments);
                float v = (ring % 2 == 0) ? 0.0f : 1.0f;
                glm::vec2 uv(u * texScale.x, v * texScale.y);

                glm::vec3 tangentDir = -right * sinA + up * cosA;
                glm::vec4 tangent(tangentDir, 1.0f);
                glm::vec4 color(params.barkTint, 1.0f);

                outVertices.push_back({pos, normal, uv, tangent, color});
            }
        }

        // Generate indices for fallback
        for (int ring = 0; ring < lengthSegments; ++ring) {
            for (int i = 0; i < radialSegments; ++i) {
                uint32_t current = baseVertexIndex + ring * (radialSegments + 1) + i;
                uint32_t next = current + 1;
                uint32_t below = current + (radialSegments + 1);
                uint32_t belowNext = below + 1;

                outIndices.push_back(current);
                outIndices.push_back(next);
                outIndices.push_back(below);

                outIndices.push_back(next);
                outIndices.push_back(belowNext);
                outIndices.push_back(below);
            }
        }
        return;
    }

    // Generate vertices using pre-computed section data
    int numSections = static_cast<int>(sections.size());

    for (int ring = 0; ring < numSections; ++ring) {
        const auto& section = sections[ring];

        // Build coordinate frame from section orientation
        glm::vec3 up = section.orientation * glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 right = section.orientation * glm::vec3(1.0f, 0.0f, 0.0f);
        right = glm::normalize(right);
        up = glm::normalize(up);

        float sectionRadius = section.radius;

        // Generate vertices around the ring
        for (int i = 0; i <= radialSegments; ++i) {
            float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(radialSegments);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // Position on ring
            glm::vec3 radialDir = right * cosA + up * sinA;
            glm::vec3 pos = section.origin + radialDir * sectionRadius;

            // Normal points outward
            glm::vec3 normal = radialDir;

            // UV coordinates - alternating pattern like ez-tree
            float u = static_cast<float>(i) / static_cast<float>(radialSegments);
            float v = (ring % 2 == 0) ? 0.0f : 1.0f;
            glm::vec2 uv(u * texScale.x, v * texScale.y);

            // Tangent along circumference
            glm::vec3 tangentDir = -right * sinA + up * cosA;
            glm::vec4 tangent(tangentDir, 1.0f);

            // Color: apply bark tint
            glm::vec4 color(params.barkTint, 1.0f);

            outVertices.push_back({pos, normal, uv, tangent, color});
        }
    }

    // Generate indices
    for (int ring = 0; ring < numSections - 1; ++ring) {
        for (int i = 0; i < radialSegments; ++i) {
            uint32_t current = baseVertexIndex + ring * (radialSegments + 1) + i;
            uint32_t next = current + 1;
            uint32_t below = current + (radialSegments + 1);
            uint32_t belowNext = below + 1;

            // Two triangles per quad
            outIndices.push_back(current);
            outIndices.push_back(next);
            outIndices.push_back(below);

            outIndices.push_back(next);
            outIndices.push_back(belowNext);
            outIndices.push_back(below);
        }
    }
}

float TubeBranchGeometry::randomFloat(float min, float max) {
    if (!rngPtr) return (min + max) * 0.5f;
    std::uniform_real_distribution<float> dist(min, max);
    return dist(*rngPtr);
}

glm::quat TubeBranchGeometry::rotateTowards(const glm::quat& from, const glm::quat& to, float maxAngle) {
    // Ensure quaternions are in same hemisphere
    glm::quat target = to;
    float dotProduct = glm::dot(from, target);
    if (dotProduct < 0.0f) {
        target = -target;
        dotProduct = -dotProduct;
    }

    // If already very close, return target
    if (dotProduct > 0.9999f) {
        return target;
    }

    // Calculate angle between quaternions
    float angle = std::acos(std::clamp(dotProduct, -1.0f, 1.0f)) * 2.0f;

    // If max angle is greater than actual angle, just return target
    if (maxAngle >= angle) {
        return target;
    }

    // Interpolate by the fraction that maxAngle represents
    float t = maxAngle / angle;
    return glm::normalize(glm::slerp(from, target, t));
}

glm::quat TubeBranchGeometry::quatFromDirection(const glm::vec3& direction) {
    glm::vec3 dir = glm::normalize(direction);
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    // Handle case where direction is parallel to up
    if (std::abs(glm::dot(dir, up)) > 0.999f) {
        // Direction is up or down, use identity or 180-degree rotation
        if (dir.y > 0.0f) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        } else {
            return glm::angleAxis(static_cast<float>(M_PI), glm::vec3(1.0f, 0.0f, 0.0f));
        }
    }

    // Create rotation from Y-up to direction
    glm::vec3 axis = glm::normalize(glm::cross(up, dir));
    float angle = std::acos(std::clamp(glm::dot(up, dir), -1.0f, 1.0f));
    return glm::angleAxis(angle, axis);
}
