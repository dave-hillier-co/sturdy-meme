#include "MotionMatchingKDTree.h"
#include <SDL3/SDL.h>
#include <numeric>
#include <queue>

namespace MotionMatching {

void MotionKDTree::build(std::vector<KDPoint> points) {
    if (points.empty()) {
        nodes_.clear();
        points_.clear();
        return;
    }

    points_ = std::move(points);
    nodes_.clear();
    nodes_.reserve(points_.size());

    // Create index array
    std::vector<size_t> indices(points_.size());
    std::iota(indices.begin(), indices.end(), 0);

    // Build tree recursively
    buildRecursive(indices, 0, indices.size(), 0);

    SDL_Log("MotionKDTree: Built tree with %zu nodes", nodes_.size());
}

int32_t MotionKDTree::buildRecursive(std::vector<size_t>& indices,
                                       size_t begin, size_t end, size_t depth) {
    if (begin >= end) {
        return -1;
    }

    // Find best split dimension (using variance-based selection)
    size_t splitDim = depth % KD_FEATURE_DIM;
    if (end - begin > 10) {
        // For larger sets, find the dimension with highest variance
        splitDim = findBestSplitDimension(indices, begin, end);
    }

    // Find median and partition
    size_t mid = begin + (end - begin) / 2;

    // Partial sort to find median
    std::nth_element(
        indices.begin() + static_cast<ptrdiff_t>(begin),
        indices.begin() + static_cast<ptrdiff_t>(mid),
        indices.begin() + static_cast<ptrdiff_t>(end),
        [this, splitDim](size_t a, size_t b) {
            return points_[a][splitDim] < points_[b][splitDim];
        }
    );

    // Create node
    KDNode node;
    node.point = points_[indices[mid]];
    node.splitDimension = splitDim;

    int32_t nodeIdx = static_cast<int32_t>(nodes_.size());
    nodes_.push_back(node);

    // Build children
    nodes_[nodeIdx].leftChild = buildRecursive(indices, begin, mid, depth + 1);
    nodes_[nodeIdx].rightChild = buildRecursive(indices, mid + 1, end, depth + 1);

    return nodeIdx;
}

size_t MotionKDTree::findBestSplitDimension(const std::vector<size_t>& indices,
                                              size_t begin, size_t end) const {
    size_t bestDim = 0;
    float bestVariance = 0.0f;

    for (size_t dim = 0; dim < KD_FEATURE_DIM; ++dim) {
        // Compute mean
        float sum = 0.0f;
        for (size_t i = begin; i < end; ++i) {
            sum += points_[indices[i]][dim];
        }
        float mean = sum / static_cast<float>(end - begin);

        // Compute variance
        float variance = 0.0f;
        for (size_t i = begin; i < end; ++i) {
            float d = points_[indices[i]][dim] - mean;
            variance += d * d;
        }

        if (variance > bestVariance) {
            bestVariance = variance;
            bestDim = dim;
        }
    }

    return bestDim;
}

std::vector<KDSearchResult> MotionKDTree::findKNearest(const KDPoint& query, size_t k) const {
    std::vector<KDSearchResult> results;
    if (nodes_.empty() || k == 0) {
        return results;
    }

    results.reserve(k);
    float maxDist = std::numeric_limits<float>::max();

    searchKNearestRecursive(0, query, k, results, maxDist);

    // Sort by distance
    std::sort(results.begin(), results.end());

    return results;
}

void MotionKDTree::searchKNearestRecursive(int32_t nodeIdx, const KDPoint& query,
                                             size_t k, std::vector<KDSearchResult>& results,
                                             float& maxDist) const {
    if (nodeIdx < 0 || nodeIdx >= static_cast<int32_t>(nodes_.size())) {
        return;
    }

    const KDNode& node = nodes_[nodeIdx];
    float distSquared = query.squaredDistance(node.point);

    // Check if this point should be added to results
    if (results.size() < k) {
        KDSearchResult result;
        result.poseIndex = node.point.poseIndex;
        result.squaredDistance = distSquared;
        results.push_back(result);

        // Update max distance if we now have k results
        if (results.size() == k) {
            maxDist = 0.0f;
            for (const auto& r : results) {
                maxDist = std::max(maxDist, r.squaredDistance);
            }
        }
    } else if (distSquared < maxDist) {
        // Replace worst result
        size_t worstIdx = 0;
        for (size_t i = 1; i < results.size(); ++i) {
            if (results[i].squaredDistance > results[worstIdx].squaredDistance) {
                worstIdx = i;
            }
        }
        results[worstIdx].poseIndex = node.point.poseIndex;
        results[worstIdx].squaredDistance = distSquared;

        // Update max distance
        maxDist = 0.0f;
        for (const auto& r : results) {
            maxDist = std::max(maxDist, r.squaredDistance);
        }
    }

    // Determine which subtree to search first
    size_t splitDim = node.splitDimension;
    float splitDist = query[splitDim] - node.point[splitDim];
    float splitDistSquared = splitDist * splitDist;

    int32_t nearChild, farChild;
    if (splitDist < 0) {
        nearChild = node.leftChild;
        farChild = node.rightChild;
    } else {
        nearChild = node.rightChild;
        farChild = node.leftChild;
    }

    // Search near subtree first
    searchKNearestRecursive(nearChild, query, k, results, maxDist);

    // Only search far subtree if the splitting plane is closer than current max distance
    if (results.size() < k || splitDistSquared < maxDist) {
        searchKNearestRecursive(farChild, query, k, results, maxDist);
    }
}

std::vector<KDSearchResult> MotionKDTree::findWithinRadius(const KDPoint& query, float radius) const {
    std::vector<KDSearchResult> results;
    if (nodes_.empty()) {
        return results;
    }

    float radiusSquared = radius * radius;
    searchRadiusRecursive(0, query, radiusSquared, results);

    // Sort by distance
    std::sort(results.begin(), results.end());

    return results;
}

void MotionKDTree::searchRadiusRecursive(int32_t nodeIdx, const KDPoint& query,
                                           float radiusSquared, std::vector<KDSearchResult>& results) const {
    if (nodeIdx < 0 || nodeIdx >= static_cast<int32_t>(nodes_.size())) {
        return;
    }

    const KDNode& node = nodes_[nodeIdx];
    float distSquared = query.squaredDistance(node.point);

    // Check if this point is within radius
    if (distSquared <= radiusSquared) {
        KDSearchResult result;
        result.poseIndex = node.point.poseIndex;
        result.squaredDistance = distSquared;
        results.push_back(result);
    }

    // Determine which subtrees to search
    size_t splitDim = node.splitDimension;
    float splitDist = query[splitDim] - node.point[splitDim];
    float splitDistSquared = splitDist * splitDist;

    int32_t nearChild, farChild;
    if (splitDist < 0) {
        nearChild = node.leftChild;
        farChild = node.rightChild;
    } else {
        nearChild = node.rightChild;
        farChild = node.leftChild;
    }

    // Always search near subtree
    searchRadiusRecursive(nearChild, query, radiusSquared, results);

    // Only search far subtree if the splitting plane is within radius
    if (splitDistSquared <= radiusSquared) {
        searchRadiusRecursive(farChild, query, radiusSquared, results);
    }
}

} // namespace MotionMatching
