#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <unordered_map>

/**
 * TerrainMeshlet - Pre-tessellated triangle mesh for terrain rendering
 *
 * Instead of rendering each CBT leaf as a single triangle, we render a pre-subdivided
 * meshlet per leaf. This gives higher resolution without increasing CBT depth (and memory).
 *
 * The meshlet is generated using LEB subdivision of a unit triangle, with vertices in
 * barycentric-like coordinates (s, t where s + t <= 1). When rendering, these coordinates
 * are transformed to the parent CBT triangle's UV space.
 *
 * Subdivision levels:
 *   Level 0: 1 triangle
 *   Level 1: 2 triangles
 *   Level 2: 4 triangles
 *   Level 3: 8 triangles
 *   Level 4: 16 triangles
 *   Level 5: 32 triangles
 *   Level 6: 64 triangles
 *   Level 7: 128 triangles
 *   Level 8: 256 triangles (recommended)
 */
class TerrainMeshlet {
public:
    struct InitInfo {
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        uint32_t subdivisionLevel = 4;  // 2^level triangles per meshlet
    };

    // Meshlet vertex: position in unit triangle barycentric coordinates
    // These will be transformed to parent triangle UV space in the vertex shader
    struct MeshletVertex {
        glm::vec2 position;  // (s, t) in unit triangle space
    };

    TerrainMeshlet() = default;
    ~TerrainMeshlet() = default;

    bool init(const InitInfo& info);
    void destroy(VmaAllocator allocator);

    // Buffer accessors
    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }

    // Geometry info
    uint32_t getTriangleCount() const { return triangleCount; }
    uint32_t getVertexCount() const { return vertexCount; }
    uint32_t getIndexCount() const { return indexCount; }
    uint32_t getSubdivisionLevel() const { return subdivisionLevel; }

private:
    // LEB subdivision to generate meshlet triangles
    void generateMeshletGeometry(uint32_t level,
                                  std::vector<MeshletVertex>& vertices,
                                  std::vector<uint16_t>& indices);

    // Recursive LEB subdivision helper
    void subdivideLEB(uint32_t depth, uint32_t targetDepth,
                      glm::vec2 v0, glm::vec2 v1, glm::vec2 v2,
                      std::vector<MeshletVertex>& vertices,
                      std::vector<uint16_t>& indices,
                      std::unordered_map<uint64_t, uint16_t>& vertexMap);

    // Hash function for deduplicating vertices
    static uint64_t hashVertex(const glm::vec2& v);

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;

    uint32_t subdivisionLevel = 0;
    uint32_t triangleCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};
