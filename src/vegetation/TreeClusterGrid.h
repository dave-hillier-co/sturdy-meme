#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <array>

// Forward declarations
struct TreeInstanceData;

// Cluster data for spatial organization
struct TreeCluster {
    // Bounding volume
    glm::vec3 center{0.0f};           // World-space center
    float boundingSphereRadius{0.0f}; // Bounding sphere for quick culling
    glm::vec3 minBounds{0.0f};        // AABB min
    glm::vec3 maxBounds{0.0f};        // AABB max

    // Tree indices in this cluster
    std::vector<uint32_t> treeIndices;

    // Per-frame state
    bool isVisible{true};             // Frustum visibility
    bool forceImpostor{false};        // All trees in cluster use impostor
    float distanceToCamera{0.0f};     // Distance from cluster center to camera

    // Stats
    uint32_t visibleTreeCount{0};     // Trees visible after per-tree culling
};

// GPU-compatible cluster data for compute shader culling
struct ClusterGPU {
    glm::vec4 centerRadius;           // xyz = center, w = bounding sphere radius
    glm::vec4 minBounds;              // xyz = AABB min, w = tree count
    glm::vec4 maxBounds;              // xyz = AABB max, w = first tree index
};
static_assert(sizeof(ClusterGPU) == 48, "ClusterGPU must be 48 bytes");

// Cluster visibility result from GPU
struct ClusterVisibility {
    uint32_t visible;                 // 1 = visible, 0 = culled
    uint32_t forceImpostor;           // 1 = force all trees to impostor
    float distanceToCamera;
    uint32_t _pad;
};
static_assert(sizeof(ClusterVisibility) == 16, "ClusterVisibility must be 16 bytes");

// Spatial grid settings
struct ClusterGridSettings {
    float cellSize{50.0f};            // Size of each grid cell in world units
    float clusterImpostorDistance{400.0f}; // Distance at which entire cluster uses impostors
    float clusterCullDistance{1000.0f};    // Distance at which cluster is completely culled
    bool enableClusterLOD{true};      // Enable cluster-level LOD decisions
    bool enableClusterCulling{true};  // Enable cluster-level frustum culling
};

// Spatial grid for forest clustering
class TreeClusterGrid {
public:
    TreeClusterGrid() = default;
    ~TreeClusterGrid() = default;

    // Initialize grid with world bounds
    void initialize(const glm::vec3& worldMin, const glm::vec3& worldMax, float cellSize);

    // Clear all clusters
    void clear();

    // Add a tree to the appropriate cluster
    void addTree(uint32_t treeIndex, const glm::vec3& position);

    // Remove a tree from its cluster
    void removeTree(uint32_t treeIndex, const glm::vec3& position);

    // Rebuild cluster bounds after adding/removing trees
    void rebuildClusterBounds(const std::vector<TreeInstanceData>& trees);

    // Update cluster visibility and LOD based on camera
    // frustumPlanes: 6 planes in format (normal.xyz, distance)
    void updateVisibility(const glm::vec3& cameraPos,
                          const std::array<glm::vec4, 6>& frustumPlanes,
                          const ClusterGridSettings& settings);

    // Get all visible tree indices (respecting cluster culling)
    std::vector<uint32_t> getVisibleTreeIndices() const;

    // Get trees that should be forced to impostor (cluster LOD)
    std::vector<uint32_t> getForceImpostorTreeIndices() const;

    // Check if a specific tree should be culled (cluster-level culling)
    bool isTreeClusterVisible(uint32_t treeIndex) const;

    // Check if a specific tree should force impostor (cluster LOD)
    bool shouldTreeForceImpostor(uint32_t treeIndex) const;

    // Get cluster containing a position
    TreeCluster* getClusterAt(const glm::vec3& position);
    const TreeCluster* getClusterAt(const glm::vec3& position) const;

    // Get all clusters
    const std::vector<TreeCluster>& getClusters() const { return clusters_; }
    std::vector<TreeCluster>& getClusters() { return clusters_; }

    // Get grid dimensions
    glm::ivec3 getGridDimensions() const { return gridDimensions_; }
    float getCellSize() const { return cellSize_; }

    // Stats
    uint32_t getVisibleClusterCount() const { return visibleClusterCount_; }
    uint32_t getTotalClusterCount() const { return static_cast<uint32_t>(clusters_.size()); }
    uint32_t getVisibleTreeCount() const { return visibleTreeCount_; }

    // GPU data export
    std::vector<ClusterGPU> exportClustersForGPU() const;

private:
    // Convert world position to grid cell index
    glm::ivec3 worldToCell(const glm::vec3& worldPos) const;

    // Convert grid cell to cluster index
    int cellToClusterIndex(const glm::ivec3& cell) const;

    // Check if cluster is in frustum
    bool isClusterInFrustum(const TreeCluster& cluster,
                            const std::array<glm::vec4, 6>& frustumPlanes) const;

    // Grid configuration
    glm::vec3 worldMin_{0.0f};
    glm::vec3 worldMax_{0.0f};
    float cellSize_{50.0f};
    glm::ivec3 gridDimensions_{0};

    // Cluster storage (one per grid cell)
    std::vector<TreeCluster> clusters_;

    // Tree to cluster mapping (treeIndex -> clusterIndex)
    std::vector<int> treeToCluster_;

    // Stats
    uint32_t visibleClusterCount_{0};
    uint32_t visibleTreeCount_{0};
};
