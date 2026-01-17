#include "NodeMask.h"
#include <algorithm>
#include <unordered_set>

NodeMask::NodeMask(size_t nodeCount, float defaultWeight)
    : weights_(nodeCount, defaultWeight) {
}

void NodeMask::resize(size_t count, float defaultWeight) {
    weights_.resize(count, defaultWeight);
}

float NodeMask::getWeight(size_t nodeIndex) const {
    if (nodeIndex < weights_.size()) {
        return weights_[nodeIndex];
    }
    return 0.0f;
}

void NodeMask::setWeight(size_t nodeIndex, float weight) {
    if (nodeIndex < weights_.size()) {
        weights_[nodeIndex] = std::clamp(weight, 0.0f, 1.0f);
    }
}

NodeMask NodeMask::inverted() const {
    NodeMask result(weights_.size());
    for (size_t i = 0; i < weights_.size(); ++i) {
        result.weights_[i] = 1.0f - weights_[i];
    }
    return result;
}

void NodeMask::scale(float factor) {
    for (float& w : weights_) {
        w = std::clamp(w * factor, 0.0f, 1.0f);
    }
}

NodeMask NodeMask::operator*(const NodeMask& other) const {
    size_t count = std::min(weights_.size(), other.weights_.size());
    NodeMask result(count);
    for (size_t i = 0; i < count; ++i) {
        result.weights_[i] = weights_[i] * other.weights_[i];
    }
    return result;
}

NodeMask NodeMask::operator+(const NodeMask& other) const {
    size_t count = std::min(weights_.size(), other.weights_.size());
    NodeMask result(count);
    for (size_t i = 0; i < count; ++i) {
        result.weights_[i] = std::clamp(weights_[i] + other.weights_[i], 0.0f, 1.0f);
    }
    return result;
}

NodeMask NodeMask::fromDepthRange(size_t nodeCount,
                                  const std::vector<int>& depths,
                                  int minDepth, int maxDepth) {
    NodeMask mask(nodeCount, 0.0f);
    size_t count = std::min(nodeCount, depths.size());
    for (size_t i = 0; i < count; ++i) {
        if (depths[i] >= minDepth && depths[i] <= maxDepth) {
            mask.setWeight(i, 1.0f);
        }
    }
    return mask;
}

NodeMask NodeMask::fromSubtree(size_t nodeCount,
                               const std::vector<int32_t>& parentIndices,
                               const std::vector<size_t>& rootNodes) {
    NodeMask mask(nodeCount, 0.0f);

    // Start with root nodes
    std::unordered_set<size_t> enabledNodes;
    for (size_t root : rootNodes) {
        if (root < nodeCount) {
            enabledNodes.insert(root);
        }
    }

    // Iteratively add children until no new nodes are found
    bool foundNew = true;
    while (foundNew) {
        foundNew = false;
        for (size_t i = 0; i < std::min(nodeCount, parentIndices.size()); ++i) {
            int32_t parentIdx = parentIndices[i];
            if (parentIdx >= 0 && enabledNodes.count(static_cast<size_t>(parentIdx)) > 0) {
                if (enabledNodes.insert(i).second) {
                    foundNew = true;
                }
            }
        }
    }

    // Set weights for all enabled nodes
    for (size_t idx : enabledNodes) {
        mask.setWeight(idx, 1.0f);
    }

    return mask;
}

NodeMask NodeMask::lerp(const NodeMask& a, const NodeMask& b, float t) {
    size_t count = std::min(a.size(), b.size());
    NodeMask result(count);
    for (size_t i = 0; i < count; ++i) {
        result.weights_[i] = a.weights_[i] * (1.0f - t) + b.weights_[i] * t;
    }
    return result;
}
