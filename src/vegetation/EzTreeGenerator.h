#pragma once

#include "TreeParameters.h"
#include "Mesh.h"  // For Vertex struct
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <random>
#include <queue>

// Faithful port of ez-tree's tree generation algorithm
// Combines structure generation and geometry in a single pass,
// so child branches spawn from actual perturbed section positions
class EzTreeGenerator {
public:
    struct LeafInstance {
        glm::vec3 position;
        glm::quat orientation;
        float size;
    };

    void generate(const TreeParameters& params,
                  std::vector<Vertex>& outBranchVertices,
                  std::vector<uint32_t>& outBranchIndices,
                  std::vector<LeafInstance>& outLeaves);

    void setSeed(uint32_t seed) { rng.seed(seed); }

private:
    // Branch in the processing queue
    struct BranchJob {
        glm::vec3 origin;
        glm::vec3 euler;  // Euler angles (x=pitch, y=yaw, z=roll) like Three.js
        float length;
        float radius;
        int level;
        int sectionCount;
        int segmentCount;
    };

    // Section info for spawning children
    struct SectionInfo {
        glm::vec3 origin;
        glm::vec3 euler;
        float radius;
    };

    void generateBranch(const BranchJob& branch,
                        const TreeParameters& params,
                        std::vector<Vertex>& outVertices,
                        std::vector<uint32_t>& outIndices,
                        std::vector<LeafInstance>& outLeaves);

    void generateBranchIndices(uint32_t indexOffset,
                               int sectionCount,
                               int segmentCount,
                               std::vector<uint32_t>& outIndices);

    void generateChildBranches(int count,
                               int level,
                               const std::vector<SectionInfo>& sections,
                               const TreeParameters& params);

    void generateLeaves(const std::vector<SectionInfo>& sections,
                        const TreeParameters& params,
                        std::vector<LeafInstance>& outLeaves);

    void generateLeaf(const glm::vec3& origin,
                      const glm::vec3& euler,
                      const TreeParameters& params,
                      std::vector<LeafInstance>& outLeaves);

    // Apply euler rotation to a vector (matching Three.js applyEuler)
    glm::vec3 applyEuler(const glm::vec3& v, const glm::vec3& euler);

    // Get rotation matrix from euler angles
    glm::mat3 eulerToMatrix(const glm::vec3& euler);

    float randomFloat(float min, float max);

    std::mt19937 rng;
    std::queue<BranchJob> branchQueue;
};
