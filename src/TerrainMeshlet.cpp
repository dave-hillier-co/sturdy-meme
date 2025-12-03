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

// LEB splitting matrix - matches leb__SplittingMatrix in leb.glsl
static glm::mat3 splittingMatrix(uint32_t splitBit) {
    float b = static_cast<float>(splitBit);
    float c = 1.0f - b;

    // Note: GLM is column-major, shader uses transpose of row-major
    // Original shader: transpose(mat3(c, b, 0, 0.5, 0, 0.5, 0, c, b))
    return glm::mat3(
        glm::vec3(c, 0.5f, 0.0f),      // column 0
        glm::vec3(b, 0.0f, c),          // column 1
        glm::vec3(0.0f, 0.5f, b)        // column 2
    );
}

// Winding correction matrix - matches leb__WindingMatrix in leb.glsl
static glm::mat3 windingMatrix(uint32_t mirrorBit) {
    float b = static_cast<float>(mirrorBit);
    float c = 1.0f - b;

    return glm::mat3(
        glm::vec3(c, 0.0f, b),          // column 0
        glm::vec3(0.0f, 1.0f, 0.0f),    // column 1
        glm::vec3(b, 0.0f, c)           // column 2
    );
}

// Decode triangle vertices for a given node ID and depth
// This matches leb_DecodeTriangleVertices in leb.glsl for the unit triangle case
static void decodeTriangleVertices(uint32_t nodeId, int depth, glm::vec2& v0, glm::vec2& v1, glm::vec2& v2) {
    // Start with identity
    glm::mat3 xf(1.0f);

    // Apply splitting matrices for each bit (from MSB to LSB)
    for (int bitID = depth - 1; bitID >= 0; --bitID) {
        uint32_t bit = (nodeId >> bitID) & 1u;
        xf = splittingMatrix(bit) * xf;
    }

    // Apply winding correction based on depth parity
    xf = windingMatrix(static_cast<uint32_t>(depth) & 1u) * xf;

    // Base triangle vertices in barycentric coordinates:
    // We use a unit right triangle: (0,0), (1,0), (0,1)
    // The matrix transforms barycentric weights
    glm::vec3 bary0(1.0f, 0.0f, 0.0f);  // vertex 0 weight
    glm::vec3 bary1(0.0f, 1.0f, 0.0f);  // vertex 1 weight
    glm::vec3 bary2(0.0f, 0.0f, 1.0f);  // vertex 2 weight

    glm::vec3 w0 = xf * bary0;
    glm::vec3 w1 = xf * bary1;
    glm::vec3 w2 = xf * bary2;

    // Convert barycentric weights to positions in unit triangle
    // Unit triangle: A=(0,0), B=(1,0), C=(0,1)
    // P = w.x * A + w.y * B + w.z * C = (w.y, w.z)
    v0 = glm::vec2(w0.y, w0.z);
    v1 = glm::vec2(w1.y, w1.z);
    v2 = glm::vec2(w2.y, w2.z);
}

void TerrainMeshlet::subdivideLEB(uint32_t depth, uint32_t targetDepth,
                                   glm::vec2 v0, glm::vec2 v1, glm::vec2 v2,
                                   std::vector<MeshletVertex>& vertices,
                                   std::vector<uint16_t>& indices,
                                   std::unordered_map<uint64_t, uint16_t>& vertexMap) {
    // Not used anymore - kept for interface compatibility
    (void)depth; (void)targetDepth; (void)v0; (void)v1; (void)v2;
    (void)vertices; (void)indices; (void)vertexMap;
}

void TerrainMeshlet::generateMeshletGeometry(uint32_t level,
                                              std::vector<MeshletVertex>& vertices,
                                              std::vector<uint16_t>& indices) {
    std::unordered_map<uint64_t, uint16_t> vertexMap;

    // Generate all 2^level leaf triangles using the same matrix-based
    // transformation as the shader's LEB library
    uint32_t numTriangles = 1u << level;

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

    for (uint32_t i = 0; i < numTriangles; ++i) {
        glm::vec2 v0, v1, v2;
        decodeTriangleVertices(i, static_cast<int>(level), v0, v1, v2);

        indices.push_back(addVertex(v0));
        indices.push_back(addVertex(v1));
        indices.push_back(addVertex(v2));
    }
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
