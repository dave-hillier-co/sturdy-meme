#include "TerrainMeshlet.h"
#include <SDL3/SDL.h>
#include <unordered_map>
#include <cmath>

uint64_t TerrainMeshlet::hashVertex(const glm::vec2& v) {
    // Quantize to prevent floating point issues
    const float scale = 1000000.0f;
    uint32_t x = static_cast<uint32_t>((v.x + 1.0f) * scale);
    uint32_t y = static_cast<uint32_t>((v.y + 1.0f) * scale);
    return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y);
}

void TerrainMeshlet::subdivideLEB(uint32_t depth, uint32_t targetDepth,
                                   glm::vec2 v0, glm::vec2 v1, glm::vec2 v2,
                                   std::vector<MeshletVertex>& vertices,
                                   std::vector<uint16_t>& indices,
                                   std::unordered_map<uint64_t, uint16_t>& vertexMap) {
    if (depth == targetDepth) {
        // Emit triangle
        auto addVertex = [&](const glm::vec2& v) -> uint16_t {
            uint64_t hash = hashVertex(v);
            auto it = vertexMap.find(hash);
            if (it != vertexMap.end()) {
                return it->second;
            }
            uint16_t idx = static_cast<uint16_t>(vertices.size());
            vertices.push_back({v});
            vertexMap[hash] = idx;
            return idx;
        };

        indices.push_back(addVertex(v0));
        indices.push_back(addVertex(v1));
        indices.push_back(addVertex(v2));
        return;
    }

    // LEB bisection: split along longest edge (v1 to v2)
    glm::vec2 midpoint = (v1 + v2) * 0.5f;

    // Left child: v0, midpoint, v1 -> rotated to v1, v0, midpoint
    // Right child: v0, v2, midpoint -> rotated to v2, midpoint, v0
    // After rotation for consistent orientation (matching LEB library):
    // The LEB rotation puts the new edge as the "longest" edge for the next level

    // Left child (bit 0): new triangle is (v1, v0, midpoint)
    // After LEB-style rotation: (midpoint, v1, v0)
    subdivideLEB(depth + 1, targetDepth, midpoint, v1, v0, vertices, indices, vertexMap);

    // Right child (bit 1): new triangle is (v2, midpoint, v0)
    // After LEB-style rotation: (midpoint, v2, v0) but winding matters
    subdivideLEB(depth + 1, targetDepth, midpoint, v2, v0, vertices, indices, vertexMap);
}

void TerrainMeshlet::generateMeshletGeometry(uint32_t level,
                                              std::vector<MeshletVertex>& vertices,
                                              std::vector<uint16_t>& indices) {
    std::unordered_map<uint64_t, uint16_t> vertexMap;

    // Start with unit triangle in barycentric-like coordinates
    // v0 = (0, 0) - first corner
    // v1 = (1, 0) - second corner
    // v2 = (0, 1) - third corner
    // This maps to: P = v0 + s*(v1-v0) + t*(v2-v0) = s*v1 + t*v2 + (1-s-t)*v0
    // In UV space of parent triangle: UV = uv0 + pos.x*(uv1-uv0) + pos.y*(uv2-uv0)

    glm::vec2 v0(0.0f, 0.0f);
    glm::vec2 v1(1.0f, 0.0f);
    glm::vec2 v2(0.0f, 1.0f);

    subdivideLEB(0, level, v0, v1, v2, vertices, indices, vertexMap);
}

bool TerrainMeshlet::init(const InitInfo& info) {
    subdivisionLevel = info.subdivisionLevel;
    triangleCount = 1u << subdivisionLevel;  // 2^level triangles

    SDL_Log("TerrainMeshlet: Generating meshlet with subdivision level %u (%u triangles)",
            subdivisionLevel, triangleCount);

    // Generate geometry
    std::vector<MeshletVertex> vertices;
    std::vector<uint16_t> indices;
    generateMeshletGeometry(subdivisionLevel, vertices, indices);

    vertexCount = static_cast<uint32_t>(vertices.size());
    indexCount = static_cast<uint32_t>(indices.size());

    SDL_Log("TerrainMeshlet: Generated %u vertices, %u indices (%u triangles)",
            vertexCount, indexCount, indexCount / 3);

    // Create staging buffers and upload to GPU
    VkDevice device = info.device;
    VkQueue queue = info.graphicsQueue;
    VkCommandPool cmdPool = info.commandPool;

    // Create vertex buffer
    {
        VkDeviceSize bufferSize = vertices.size() * sizeof(MeshletVertex);

        // Staging buffer
        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = bufferSize;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                  VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocInfo{};
        if (vmaCreateBuffer(info.allocator, &stagingInfo, &stagingAllocInfo,
                            &stagingBuffer, &stagingAllocation, &allocInfo) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet staging buffer");
            return false;
        }

        memcpy(allocInfo.pMappedData, vertices.data(), bufferSize);

        // Device buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(info.allocator, &bufferInfo, &allocCreateInfo,
                            &vertexBuffer, &vertexAllocation, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(info.allocator, stagingBuffer, stagingAllocation);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet vertex buffer");
            return false;
        }

        // Copy via command buffer
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = cmdPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = bufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, vertexBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
        vmaDestroyBuffer(info.allocator, stagingBuffer, stagingAllocation);
    }

    // Create index buffer
    {
        VkDeviceSize bufferSize = indices.size() * sizeof(uint16_t);

        // Staging buffer
        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = bufferSize;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                  VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocInfo{};
        if (vmaCreateBuffer(info.allocator, &stagingInfo, &stagingAllocInfo,
                            &stagingBuffer, &stagingAllocation, &allocInfo) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet index staging buffer");
            return false;
        }

        memcpy(allocInfo.pMappedData, indices.data(), bufferSize);

        // Device buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(info.allocator, &bufferInfo, &allocCreateInfo,
                            &indexBuffer, &indexAllocation, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(info.allocator, stagingBuffer, stagingAllocation);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet index buffer");
            return false;
        }

        // Copy via command buffer
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = cmdPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = bufferSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, indexBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
        vmaDestroyBuffer(info.allocator, stagingBuffer, stagingAllocation);
    }

    SDL_Log("TerrainMeshlet: Initialization complete");
    return true;
}

void TerrainMeshlet::destroy(VmaAllocator allocator) {
    if (vertexBuffer) {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
        vertexAllocation = VK_NULL_HANDLE;
    }
    if (indexBuffer) {
        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
        indexBuffer = VK_NULL_HANDLE;
        indexAllocation = VK_NULL_HANDLE;
    }
}
