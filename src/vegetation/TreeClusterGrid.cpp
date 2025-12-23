#include "TreeClusterGrid.h"
#include "TreeSystem.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <limits>

void TreeClusterGrid::initialize(const glm::vec3& worldMin, const glm::vec3& worldMax, float cellSize) {
    worldMin_ = worldMin;
    worldMax_ = worldMax;
    cellSize_ = cellSize;

    // Calculate grid dimensions
    glm::vec3 worldSize = worldMax - worldMin;
    gridDimensions_.x = std::max(1, static_cast<int>(std::ceil(worldSize.x / cellSize)));
    gridDimensions_.y = std::max(1, static_cast<int>(std::ceil(worldSize.y / cellSize)));
    gridDimensions_.z = std::max(1, static_cast<int>(std::ceil(worldSize.z / cellSize)));

    // Create clusters (one per cell)
    int totalCells = gridDimensions_.x * gridDimensions_.y * gridDimensions_.z;
    clusters_.resize(totalCells);

    // Initialize cluster bounds
    for (int z = 0; z < gridDimensions_.z; ++z) {
        for (int y = 0; y < gridDimensions_.y; ++y) {
            for (int x = 0; x < gridDimensions_.x; ++x) {
                int idx = cellToClusterIndex(glm::ivec3(x, y, z));
                auto& cluster = clusters_[idx];

                cluster.minBounds = worldMin + glm::vec3(x, y, z) * cellSize;
                cluster.maxBounds = cluster.minBounds + glm::vec3(cellSize);
                cluster.center = (cluster.minBounds + cluster.maxBounds) * 0.5f;
                cluster.boundingSphereRadius = cellSize * 0.866f; // sqrt(3)/2 for cube diagonal
                cluster.treeIndices.clear();
            }
        }
    }

    treeToCluster_.clear();

    SDL_Log("TreeClusterGrid: Initialized %dx%dx%d grid (%d clusters) with cell size %.1f",
            gridDimensions_.x, gridDimensions_.y, gridDimensions_.z, totalCells, cellSize);
}

void TreeClusterGrid::clear() {
    for (auto& cluster : clusters_) {
        cluster.treeIndices.clear();
        cluster.visibleTreeCount = 0;
    }
    treeToCluster_.clear();
    visibleClusterCount_ = 0;
    visibleTreeCount_ = 0;
}

glm::ivec3 TreeClusterGrid::worldToCell(const glm::vec3& worldPos) const {
    glm::vec3 localPos = worldPos - worldMin_;
    glm::ivec3 cell;
    cell.x = std::clamp(static_cast<int>(localPos.x / cellSize_), 0, gridDimensions_.x - 1);
    cell.y = std::clamp(static_cast<int>(localPos.y / cellSize_), 0, gridDimensions_.y - 1);
    cell.z = std::clamp(static_cast<int>(localPos.z / cellSize_), 0, gridDimensions_.z - 1);
    return cell;
}

int TreeClusterGrid::cellToClusterIndex(const glm::ivec3& cell) const {
    return cell.x + cell.y * gridDimensions_.x + cell.z * gridDimensions_.x * gridDimensions_.y;
}

void TreeClusterGrid::addTree(uint32_t treeIndex, const glm::vec3& position) {
    glm::ivec3 cell = worldToCell(position);
    int clusterIdx = cellToClusterIndex(cell);

    if (clusterIdx >= 0 && clusterIdx < static_cast<int>(clusters_.size())) {
        clusters_[clusterIdx].treeIndices.push_back(treeIndex);

        // Ensure treeToCluster_ is large enough
        if (treeIndex >= treeToCluster_.size()) {
            treeToCluster_.resize(treeIndex + 1, -1);
        }
        treeToCluster_[treeIndex] = clusterIdx;
    }
}

void TreeClusterGrid::removeTree(uint32_t treeIndex, const glm::vec3& position) {
    glm::ivec3 cell = worldToCell(position);
    int clusterIdx = cellToClusterIndex(cell);

    if (clusterIdx >= 0 && clusterIdx < static_cast<int>(clusters_.size())) {
        auto& indices = clusters_[clusterIdx].treeIndices;
        indices.erase(std::remove(indices.begin(), indices.end(), treeIndex), indices.end());

        if (treeIndex < treeToCluster_.size()) {
            treeToCluster_[treeIndex] = -1;
        }
    }
}

void TreeClusterGrid::rebuildClusterBounds(const std::vector<TreeInstanceData>& trees) {
    // Reset all cluster bounds to invalid state
    for (auto& cluster : clusters_) {
        if (cluster.treeIndices.empty()) {
            continue;
        }

        // Compute tight AABB from actual tree positions
        glm::vec3 minB(std::numeric_limits<float>::max());
        glm::vec3 maxB(std::numeric_limits<float>::lowest());

        for (uint32_t treeIdx : cluster.treeIndices) {
            if (treeIdx < trees.size()) {
                const auto& tree = trees[treeIdx];
                // Approximate tree bounds (assuming typical tree size)
                float treeRadius = 10.0f * tree.scale;
                float treeHeight = 20.0f * tree.scale;

                glm::vec3 treeMin = tree.position - glm::vec3(treeRadius, 0, treeRadius);
                glm::vec3 treeMax = tree.position + glm::vec3(treeRadius, treeHeight, treeRadius);

                minB = glm::min(minB, treeMin);
                maxB = glm::max(maxB, treeMax);
            }
        }

        if (minB.x < maxB.x) { // Valid bounds
            cluster.minBounds = minB;
            cluster.maxBounds = maxB;
            cluster.center = (minB + maxB) * 0.5f;
            cluster.boundingSphereRadius = glm::length(maxB - cluster.center);
        }
    }
}

bool TreeClusterGrid::isClusterInFrustum(const TreeCluster& cluster,
                                          const std::array<glm::vec4, 6>& frustumPlanes) const {
    // Sphere-frustum test for quick rejection
    for (const auto& plane : frustumPlanes) {
        glm::vec3 normal(plane.x, plane.y, plane.z);
        float distance = glm::dot(normal, cluster.center) + plane.w;

        if (distance < -cluster.boundingSphereRadius) {
            return false; // Completely outside this plane
        }
    }
    return true;
}

void TreeClusterGrid::updateVisibility(const glm::vec3& cameraPos,
                                        const std::array<glm::vec4, 6>& frustumPlanes,
                                        const ClusterGridSettings& settings) {
    visibleClusterCount_ = 0;
    visibleTreeCount_ = 0;

    for (auto& cluster : clusters_) {
        if (cluster.treeIndices.empty()) {
            cluster.isVisible = false;
            cluster.forceImpostor = false;
            cluster.visibleTreeCount = 0;
            continue;
        }

        // Calculate distance to camera
        cluster.distanceToCamera = glm::distance(cameraPos, cluster.center);

        // Distance culling
        if (settings.enableClusterCulling && cluster.distanceToCamera > settings.clusterCullDistance) {
            cluster.isVisible = false;
            cluster.forceImpostor = false;
            cluster.visibleTreeCount = 0;
            continue;
        }

        // Frustum culling
        if (settings.enableClusterCulling && !isClusterInFrustum(cluster, frustumPlanes)) {
            cluster.isVisible = false;
            cluster.forceImpostor = false;
            cluster.visibleTreeCount = 0;
            continue;
        }

        // Cluster is visible
        cluster.isVisible = true;
        visibleClusterCount_++;

        // Cluster-level LOD: force impostor for distant clusters
        if (settings.enableClusterLOD && cluster.distanceToCamera > settings.clusterImpostorDistance) {
            cluster.forceImpostor = true;
        } else {
            cluster.forceImpostor = false;
        }

        cluster.visibleTreeCount = static_cast<uint32_t>(cluster.treeIndices.size());
        visibleTreeCount_ += cluster.visibleTreeCount;
    }
}

std::vector<uint32_t> TreeClusterGrid::getVisibleTreeIndices() const {
    std::vector<uint32_t> result;
    result.reserve(visibleTreeCount_);

    for (const auto& cluster : clusters_) {
        if (cluster.isVisible) {
            result.insert(result.end(), cluster.treeIndices.begin(), cluster.treeIndices.end());
        }
    }

    return result;
}

std::vector<uint32_t> TreeClusterGrid::getForceImpostorTreeIndices() const {
    std::vector<uint32_t> result;

    for (const auto& cluster : clusters_) {
        if (cluster.isVisible && cluster.forceImpostor) {
            result.insert(result.end(), cluster.treeIndices.begin(), cluster.treeIndices.end());
        }
    }

    return result;
}

bool TreeClusterGrid::isTreeClusterVisible(uint32_t treeIndex) const {
    if (treeIndex >= treeToCluster_.size()) {
        return true; // Default to visible if not tracked
    }

    int clusterIdx = treeToCluster_[treeIndex];
    if (clusterIdx < 0 || clusterIdx >= static_cast<int>(clusters_.size())) {
        return true;
    }

    return clusters_[clusterIdx].isVisible;
}

bool TreeClusterGrid::shouldTreeForceImpostor(uint32_t treeIndex) const {
    if (treeIndex >= treeToCluster_.size()) {
        return false;
    }

    int clusterIdx = treeToCluster_[treeIndex];
    if (clusterIdx < 0 || clusterIdx >= static_cast<int>(clusters_.size())) {
        return false;
    }

    return clusters_[clusterIdx].forceImpostor;
}

TreeCluster* TreeClusterGrid::getClusterAt(const glm::vec3& position) {
    glm::ivec3 cell = worldToCell(position);
    int idx = cellToClusterIndex(cell);
    if (idx >= 0 && idx < static_cast<int>(clusters_.size())) {
        return &clusters_[idx];
    }
    return nullptr;
}

const TreeCluster* TreeClusterGrid::getClusterAt(const glm::vec3& position) const {
    glm::ivec3 cell = worldToCell(position);
    int idx = cellToClusterIndex(cell);
    if (idx >= 0 && idx < static_cast<int>(clusters_.size())) {
        return &clusters_[idx];
    }
    return nullptr;
}

std::vector<ClusterGPU> TreeClusterGrid::exportClustersForGPU() const {
    std::vector<ClusterGPU> result;
    result.reserve(clusters_.size());

    uint32_t firstTreeIndex = 0;
    for (const auto& cluster : clusters_) {
        ClusterGPU gpu;
        gpu.centerRadius = glm::vec4(cluster.center, cluster.boundingSphereRadius);
        gpu.minBounds = glm::vec4(cluster.minBounds, static_cast<float>(cluster.treeIndices.size()));
        gpu.maxBounds = glm::vec4(cluster.maxBounds, static_cast<float>(firstTreeIndex));

        result.push_back(gpu);
        firstTreeIndex += static_cast<uint32_t>(cluster.treeIndices.size());
    }

    return result;
}
