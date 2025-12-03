#include "TerrainMeshlet.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <unordered_map>
#include <cmath>

uint64_t TerrainMeshlet::makeVertexKey(const glm::vec2& v) {
    // Quantize to avoid floating point comparison issues
    // Use 16-bit precision per component (65536 steps in [0,1])
    uint32_t x = static_cast<uint32_t>(std::round(v.x * 65535.0f));
    uint32_t y = static_cast<uint32_t>(std::round(v.y * 65535.0f));
    return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(y);
}

void TerrainMeshlet::subdivideLEB(const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2,
                                   uint32_t depth, uint32_t targetDepth,
                                   std::vector<glm::vec2>& vertices,
                                   std::vector<uint16_t>& indices,
                                   std::unordered_map<uint64_t, uint16_t>& vertexMap) {
    if (depth >= targetDepth) {
        // Output this triangle
        auto addVertex = [&](const glm::vec2& v) -> uint16_t {
            uint64_t key = makeVertexKey(v);
            auto it = vertexMap.find(key);
            if (it != vertexMap.end()) {
                return it->second;
            }
            uint16_t idx = static_cast<uint16_t>(vertices.size());
            vertices.push_back(v);
            vertexMap[key] = idx;
            return idx;
        };

        indices.push_back(addVertex(v0));
        indices.push_back(addVertex(v1));
        indices.push_back(addVertex(v2));
        return;
    }

    // LEB bisection: split along longest edge (always v1-v2 in our convention)
    glm::vec2 midpoint = (v1 + v2) * 0.5f;

    // Create two child triangles with proper winding
    // Left child: (midpoint, v0, v1)
    // Right child: (midpoint, v2, v0)
    subdivideLEB(midpoint, v0, v1, depth + 1, targetDepth, vertices, indices, vertexMap);
    subdivideLEB(midpoint, v2, v0, depth + 1, targetDepth, vertices, indices, vertexMap);
}

void TerrainMeshlet::generateMeshletGeometry(uint32_t level,
                                              std::vector<glm::vec2>& vertices,
                                              std::vector<uint16_t>& indices) {
    vertices.clear();
    indices.clear();

    std::unordered_map<uint64_t, uint16_t> vertexMap;

    // Start with a unit right triangle in barycentric-like coordinates
    // These vertices represent positions within the parent CBT triangle
    // v0 = apex (opposite to longest edge)
    // v1, v2 = endpoints of longest edge
    glm::vec2 v0(0.0f, 0.0f);
    glm::vec2 v1(1.0f, 0.0f);
    glm::vec2 v2(0.0f, 1.0f);

    // Recursively subdivide
    subdivideLEB(v0, v1, v2, 0, level, vertices, indices, vertexMap);

    SDL_Log("TerrainMeshlet: Generated %zu vertices, %zu indices (%zu triangles) at level %u",
            vertices.size(), indices.size(), indices.size() / 3, level);
}

bool TerrainMeshlet::init(const InitInfo& info) {
    subdivisionLevel = info.subdivisionLevel;

    // Generate meshlet geometry
    std::vector<glm::vec2> vertices;
    std::vector<uint16_t> indices;
    generateMeshletGeometry(subdivisionLevel, vertices, indices);

    vertexCount = static_cast<uint32_t>(vertices.size());
    indexCount = static_cast<uint32_t>(indices.size());
    triangleCount = indexCount / 3;

    // Create vertex buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = vertices.size() * sizeof(glm::vec2);
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (vmaCreateBuffer(info.allocator, &bufferInfo, &allocInfo,
                           &vertexBuffer, &vertexAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet vertex buffer");
            return false;
        }

        // Copy vertex data
        void* data;
        if (vmaMapMemory(info.allocator, vertexAllocation, &data) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map meshlet vertex buffer");
            return false;
        }
        memcpy(data, vertices.data(), bufferInfo.size);
        vmaUnmapMemory(info.allocator, vertexAllocation);
    }

    // Create index buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = indices.size() * sizeof(uint16_t);
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (vmaCreateBuffer(info.allocator, &bufferInfo, &allocInfo,
                           &indexBuffer, &indexAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create meshlet index buffer");
            return false;
        }

        // Copy index data
        void* data;
        if (vmaMapMemory(info.allocator, indexAllocation, &data) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map meshlet index buffer");
            return false;
        }
        memcpy(data, indices.data(), bufferInfo.size);
        vmaUnmapMemory(info.allocator, indexAllocation);
    }

    SDL_Log("TerrainMeshlet initialized: level %u, %u triangles, %u vertices",
            subdivisionLevel, triangleCount, vertexCount);

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
