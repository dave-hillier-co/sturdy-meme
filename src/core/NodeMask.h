#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

// Domain-agnostic per-node weighting for any hierarchical structure.
// Used by both skeletal animation (bone masks) and tree animation (branch masks).
// Weight of 1.0 = fully affected, 0.0 = not affected.
class NodeMask {
public:
    NodeMask() = default;

    // Create a mask for a given node count (all weights = defaultWeight)
    explicit NodeMask(size_t nodeCount, float defaultWeight = 1.0f);

    // Access weights
    float getWeight(size_t nodeIndex) const;
    void setWeight(size_t nodeIndex, float weight);

    // Get all weights (for use with blendMasked)
    const std::vector<float>& getWeights() const { return weights_; }
    std::vector<float>& getWeights() { return weights_; }

    // Resize the mask
    void resize(size_t count, float defaultWeight = 1.0f);

    // Size
    size_t size() const { return weights_.size(); }
    bool empty() const { return weights_.empty(); }

    // Invert the mask (1 - weight for each node)
    NodeMask inverted() const;

    // Multiply all weights by a factor
    void scale(float factor);

    // Combine masks (multiply weights)
    NodeMask operator*(const NodeMask& other) const;

    // Combine masks (add weights, clamped to [0,1])
    NodeMask operator+(const NodeMask& other) const;

    // Factory: create mask from hierarchy depth information
    // Enables nodes within depth range [minDepth, maxDepth] (inclusive)
    // depths vector contains the depth of each node in the hierarchy
    static NodeMask fromDepthRange(size_t nodeCount,
                                   const std::vector<int>& depths,
                                   int minDepth, int maxDepth);

    // Factory: create mask that enables nodes based on a predicate
    // predicate(nodeIndex) returns the weight for that node
    template<typename Predicate>
    static NodeMask fromPredicate(size_t nodeCount, Predicate predicate) {
        NodeMask mask(nodeCount, 0.0f);
        for (size_t i = 0; i < nodeCount; ++i) {
            mask.setWeight(i, predicate(i));
        }
        return mask;
    }

    // Factory: create mask from parent indices (enables node and all descendants)
    // parentIndices[i] = parent of node i, -1 for root
    // rootNodes = set of root node indices to start from
    static NodeMask fromSubtree(size_t nodeCount,
                                const std::vector<int32_t>& parentIndices,
                                const std::vector<size_t>& rootNodes);

    // Lerp between two masks
    static NodeMask lerp(const NodeMask& a, const NodeMask& b, float t);

private:
    std::vector<float> weights_;
};
