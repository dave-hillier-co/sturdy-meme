#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <limits>
#include <memory>

// Axis-Aligned Bounding Box for culling
struct AABB {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());

    // Expand bounds to include a point
    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    // Get center of the bounding box
    glm::vec3 getCenter() const {
        return (min + max) * 0.5f;
    }

    // Get half-extents (for OBB tests)
    glm::vec3 getExtents() const {
        return (max - min) * 0.5f;
    }

    // Check if AABB is valid (has been expanded at least once)
    bool isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    // Transform AABB by a matrix (returns axis-aligned bounds of transformed box)
    AABB transformed(const glm::mat4& transform) const {
        AABB result;
        // Transform all 8 corners and expand result bounds
        glm::vec3 corners[8] = {
            glm::vec3(min.x, min.y, min.z),
            glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, min.z),
            glm::vec3(max.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z),
            glm::vec3(max.x, min.y, max.z),
            glm::vec3(min.x, max.y, max.z),
            glm::vec3(max.x, max.y, max.z)
        };
        for (int i = 0; i < 8; ++i) {
            glm::vec4 transformed = transform * glm::vec4(corners[i], 1.0f);
            result.expand(glm::vec3(transformed));
        }
        return result;
    }
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;  // xyz = tangent direction, w = handedness (+1 or -1)
    glm::vec4 color = glm::vec4(1.0f);  // vertex color (glTF material baseColorFactor)

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, tangent);

        // Note: location 4, 5 reserved for bone data in SkinnedVertex
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 6;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

// CPU-side mesh payload passed between generators and batched uploads.
struct MeshGeometry {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    // Move-only
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void createCube();
    void createPlane(float width, float depth);
    void createDisc(float radius, int segments, float uvScale = 1.0f);
    void createSphere(float radius, int stacks, int slices);
    void createCapsule(float radius, float height, int stacks, int slices);
    void createCylinder(float radius, float height, int segments);
    void createRock(float baseRadius, int subdivisions, uint32_t seed, float roughness = 0.3f, float asymmetry = 0.2f);
    void createBranch(float radius, float length, int sections, int segments, uint32_t seed,
                      float taper = 0.7f, float gnarliness = 0.15f);
    void createForkedBranch(float radius, float length, int sections, int segments, uint32_t seed,
                            float taper = 0.7f, float gnarliness = 0.15f, float forkAngle = 0.4f);
    void setCustomGeometry(const std::vector<Vertex>& verts, const std::vector<uint32_t>& inds);
    void setCustomGeometry(std::vector<Vertex>&& verts, std::vector<uint32_t>&& inds);
    bool upload(VmaAllocator allocator, vk::Device device, vk::CommandPool commandPool, vk::Queue queue);

    // Batched-upload support: several meshes can share one staging buffer and
    // one command-buffer submit (see SceneBuilder::addGeneratedMeshes).
    // stagedSize() is the staging bytes this mesh needs; copyGeometryTo()
    // writes vertices then indices to dst; uploadFromStaging() creates the
    // device buffers and records the copies into an externally managed
    // command buffer — the caller owns the staging lifetime and the submit.
    vk::DeviceSize stagedSize() const {
        return sizeof(Vertex) * vertices.size() + sizeof(uint32_t) * indices.size();
    }
    void copyGeometryTo(void* dst) const;
    bool uploadFromStaging(VmaAllocator allocator, vk::CommandBuffer cmd,
                           vk::Buffer staging, vk::DeviceSize stagingOffset);

    vk::Buffer getVertexBuffer() const { return vertexBuffer; }
    vk::Buffer getIndexBuffer() const { return indexBuffer; }
    uint32_t getIndexCount() const {
        return indices.empty() ? indexCount_ : static_cast<uint32_t>(indices.size());
    }
    uint32_t getVertexCount() const {
        return vertices.empty() ? vertexCount_ : static_cast<uint32_t>(vertices.size());
    }

    // Release GPU resources without destroying CPU data (for dynamic meshes that need re-upload)
    void releaseGPUResources();

    // Release CPU-side geometry after upload, for large generated meshes that
    // never feed physics or re-upload. Index count and bounds are preserved.
    void releaseCpuGeometry() {
        indexCount_ = static_cast<uint32_t>(indices.size());
        vertexCount_ = static_cast<uint32_t>(vertices.size());
        vertices = {};
        indices = {};
    }

    // Access to vertex data for physics collision shapes
    const std::vector<Vertex>& getVertices() const { return vertices; }

    // Get local-space bounding box
    const AABB& getBounds() const { return bounds; }

private:
    // Recalculate bounding box from vertices
    void calculateBounds();

    // Stored for cleanup
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t indexCount_ = 0;   // Preserved counts after releaseCpuGeometry()
    uint32_t vertexCount_ = 0;
    AABB bounds;  // Local-space bounding box

    vk::Buffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    vk::Buffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
};
