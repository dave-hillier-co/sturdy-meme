#include "MeshClusterBuilder.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>
#include <numeric>

// ============================================================================
// MeshClusterBuilder
// ============================================================================

void MeshClusterBuilder::setTargetClusterSize(uint32_t trianglesPerCluster) {
    targetClusterSize_ = std::clamp(trianglesPerCluster, MIN_CLUSTER_SIZE, MAX_CLUSTER_SIZE);
}

ClusteredMesh MeshClusterBuilder::build(const std::vector<Vertex>& vertices,
                                         const std::vector<uint32_t>& indices,
                                         uint32_t meshId) {
    ClusteredMesh result;
    result.vertices = vertices;
    result.indices = indices;

    uint32_t totalTriangles = static_cast<uint32_t>(indices.size()) / 3;
    uint32_t trianglesPerCluster = targetClusterSize_;

    // Simple linear partitioning of triangles into clusters
    // A more sophisticated approach would use spatial partitioning (e.g., k-d tree)
    // but linear is a good starting point and preserves mesh locality
    uint32_t numClusters = (totalTriangles + trianglesPerCluster - 1) / trianglesPerCluster;

    result.clusters.reserve(numClusters);
    result.totalTriangles = totalTriangles;
    result.totalClusters = numClusters;

    for (uint32_t c = 0; c < numClusters; ++c) {
        uint32_t firstTriangle = c * trianglesPerCluster;
        uint32_t clusterTriangles = std::min(trianglesPerCluster, totalTriangles - firstTriangle);
        uint32_t firstIndex = firstTriangle * 3;
        uint32_t indexCount = clusterTriangles * 3;

        MeshCluster cluster{};
        cluster.firstIndex = firstIndex;
        cluster.indexCount = indexCount;
        cluster.firstVertex = 0;  // All clusters share the same vertex buffer
        cluster.meshId = meshId;
        cluster.lodLevel = 0;  // Base LOD for now
        cluster.parentError = 0.0f;
        cluster.error = 0.0f;

        // Compute bounding data
        cluster.boundingSphere = computeBoundingSphere(vertices, indices, firstIndex, indexCount);
        computeAABB(vertices, indices, firstIndex, indexCount, cluster.aabbMin, cluster.aabbMax);
        computeNormalCone(vertices, indices, firstIndex, indexCount, cluster.coneAxis, cluster.coneAngle);

        result.clusters.push_back(cluster);
    }

    SDL_Log("MeshClusterBuilder: Built %u clusters from %u triangles (target %u tri/cluster)",
            numClusters, totalTriangles, trianglesPerCluster);

    return result;
}

glm::vec4 MeshClusterBuilder::computeBoundingSphere(const std::vector<Vertex>& vertices,
                                                      const std::vector<uint32_t>& indices,
                                                      uint32_t firstIndex, uint32_t indexCount) {
    if (indexCount == 0) return glm::vec4(0.0f);

    // Compute center as centroid of all referenced vertices
    glm::vec3 center(0.0f);
    for (uint32_t i = firstIndex; i < firstIndex + indexCount; ++i) {
        center += vertices[indices[i]].position;
    }
    center /= static_cast<float>(indexCount);

    // Find maximum distance from center
    float maxDist2 = 0.0f;
    for (uint32_t i = firstIndex; i < firstIndex + indexCount; ++i) {
        float dist2 = glm::dot(vertices[indices[i]].position - center,
                                vertices[indices[i]].position - center);
        maxDist2 = std::max(maxDist2, dist2);
    }

    return glm::vec4(center, std::sqrt(maxDist2));
}

void MeshClusterBuilder::computeAABB(const std::vector<Vertex>& vertices,
                                       const std::vector<uint32_t>& indices,
                                       uint32_t firstIndex, uint32_t indexCount,
                                       glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (uint32_t i = firstIndex; i < firstIndex + indexCount; ++i) {
        const auto& pos = vertices[indices[i]].position;
        outMin = glm::min(outMin, pos);
        outMax = glm::max(outMax, pos);
    }
}

void MeshClusterBuilder::computeNormalCone(const std::vector<Vertex>& vertices,
                                             const std::vector<uint32_t>& indices,
                                             uint32_t firstIndex, uint32_t indexCount,
                                             glm::vec3& outAxis, float& outAngle) {
    // Compute average normal direction
    glm::vec3 avgNormal(0.0f);
    uint32_t triCount = indexCount / 3;
    for (uint32_t t = 0; t < triCount; ++t) {
        uint32_t base = firstIndex + t * 3;
        const auto& v0 = vertices[indices[base + 0]].position;
        const auto& v1 = vertices[indices[base + 1]].position;
        const auto& v2 = vertices[indices[base + 2]].position;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 faceNormal = glm::cross(edge1, edge2);
        float area = glm::length(faceNormal);
        if (area > 1e-8f) {
            avgNormal += faceNormal;  // Area-weighted
        }
    }

    float len = glm::length(avgNormal);
    if (len < 1e-8f) {
        outAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        outAngle = -1.0f;  // Degenerate - don't cull
        return;
    }
    outAxis = avgNormal / len;

    // Find max deviation from average normal
    float minCos = 1.0f;
    for (uint32_t t = 0; t < triCount; ++t) {
        uint32_t base = firstIndex + t * 3;
        const auto& v0 = vertices[indices[base + 0]].position;
        const auto& v1 = vertices[indices[base + 1]].position;
        const auto& v2 = vertices[indices[base + 2]].position;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));
        float cosAngle = glm::dot(faceNormal, outAxis);
        minCos = std::min(minCos, cosAngle);
    }

    outAngle = minCos;  // cos(half-angle) - higher = tighter cone
}

// ============================================================================
// GPUClusterBuffer
// ============================================================================

std::unique_ptr<GPUClusterBuffer> GPUClusterBuffer::create(const InitInfo& info) {
    auto buffer = std::make_unique<GPUClusterBuffer>(ConstructToken{});
    if (!buffer->initInternal(info)) {
        return nullptr;
    }
    return buffer;
}

bool GPUClusterBuffer::initInternal(const InitInfo& info) {
    allocator_ = info.allocator;
    device_ = info.device;
    commandPool_ = info.commandPool;
    queue_ = info.queue;
    maxClusters_ = info.maxClusters;
    maxVertices_ = info.maxVertices;
    maxIndices_ = info.maxIndices;

    // Create device-local buffers sized for max capacity
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Vertex buffer
    bufInfo.size = maxVertices_ * sizeof(Vertex);
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!ManagedBuffer::create(allocator_, bufInfo, allocInfo, vertexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create vertex buffer");
        return false;
    }

    // Index buffer
    bufInfo.size = maxIndices_ * sizeof(uint32_t);
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!ManagedBuffer::create(allocator_, bufInfo, allocInfo, indexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create index buffer");
        return false;
    }

    // Cluster metadata buffer
    bufInfo.size = maxClusters_ * sizeof(MeshCluster);
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!ManagedBuffer::create(allocator_, bufInfo, allocInfo, clusterBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create cluster buffer");
        return false;
    }

    SDL_Log("GPUClusterBuffer: Created (maxClusters=%u, maxVertices=%u, maxIndices=%u)",
            maxClusters_, maxVertices_, maxIndices_);
    return true;
}

uint32_t GPUClusterBuffer::uploadMesh(const ClusteredMesh& mesh) {
    if (totalClusters_ + mesh.totalClusters > maxClusters_ ||
        totalVertices_ + mesh.vertices.size() > maxVertices_ ||
        totalIndices_ + mesh.indices.size() > maxIndices_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUClusterBuffer: Not enough space (clusters: %u+%u/%u, vertices: %u+%zu/%u, indices: %u+%zu/%u)",
            totalClusters_, mesh.totalClusters, maxClusters_,
            totalVertices_, mesh.vertices.size(), maxVertices_,
            totalIndices_, mesh.indices.size(), maxIndices_);
        return UINT32_MAX;
    }

    uint32_t baseCluster = totalClusters_;
    uint32_t baseVertex = totalVertices_;
    uint32_t baseIndex = totalIndices_;

    // Create staging buffers and upload
    VkDeviceSize vertexSize = mesh.vertices.size() * sizeof(Vertex);
    VkDeviceSize indexSize = mesh.indices.size() * sizeof(uint32_t);
    VkDeviceSize clusterSize = mesh.clusters.size() * sizeof(MeshCluster);

    // Adjust cluster offsets for global buffer positioning
    std::vector<MeshCluster> adjustedClusters = mesh.clusters;
    for (auto& cluster : adjustedClusters) {
        cluster.firstIndex += baseIndex;
        cluster.firstVertex += static_cast<int32_t>(baseVertex);
    }

    // Create staging buffer for all three uploads
    VkDeviceSize totalStagingSize = vertexSize + indexSize + clusterSize;

    VkBufferCreateInfo stagingBufInfo{};
    stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size = totalStagingSize;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;

    ManagedBuffer stagingBuffer;
    if (!ManagedBuffer::create(allocator_, stagingBufInfo, stagingAllocInfo, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create staging buffer");
        return UINT32_MAX;
    }

    // Map and copy data
    void* mapped = stagingBuffer.map();
    if (!mapped) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to map staging buffer");
        return UINT32_MAX;
    }

    auto* ptr = static_cast<uint8_t*>(mapped);
    memcpy(ptr, mesh.vertices.data(), vertexSize);
    ptr += vertexSize;
    memcpy(ptr, mesh.indices.data(), indexSize);
    ptr += indexSize;
    memcpy(ptr, adjustedClusters.data(), clusterSize);

    // Record copy commands
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkDeviceSize stagingOffset = 0;

    // Copy vertices
    VkBufferCopy vertexCopy{};
    vertexCopy.srcOffset = stagingOffset;
    vertexCopy.dstOffset = baseVertex * sizeof(Vertex);
    vertexCopy.size = vertexSize;
    vkCmdCopyBuffer(cmd, stagingBuffer.get(), vertexBuffer_.get(), 1, &vertexCopy);
    stagingOffset += vertexSize;

    // Copy indices
    VkBufferCopy indexCopy{};
    indexCopy.srcOffset = stagingOffset;
    indexCopy.dstOffset = baseIndex * sizeof(uint32_t);
    indexCopy.size = indexSize;
    vkCmdCopyBuffer(cmd, stagingBuffer.get(), indexBuffer_.get(), 1, &indexCopy);
    stagingOffset += indexSize;

    // Copy cluster metadata
    VkBufferCopy clusterCopy{};
    clusterCopy.srcOffset = stagingOffset;
    clusterCopy.dstOffset = baseCluster * sizeof(MeshCluster);
    clusterCopy.size = clusterSize;
    vkCmdCopyBuffer(cmd, stagingBuffer.get(), clusterBuffer_.get(), 1, &clusterCopy);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    // Update totals
    totalClusters_ += mesh.totalClusters;
    totalVertices_ += static_cast<uint32_t>(mesh.vertices.size());
    totalIndices_ += static_cast<uint32_t>(mesh.indices.size());

    SDL_Log("GPUClusterBuffer: Uploaded mesh (%u clusters, %zu vertices, %zu indices)",
            mesh.totalClusters, mesh.vertices.size(), mesh.indices.size());

    return baseCluster;
}
