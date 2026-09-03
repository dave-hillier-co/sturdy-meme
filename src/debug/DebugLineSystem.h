#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "InitContext.h"
#include "core/vulkan/VmaBuffer.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

// Vertex for debug lines
struct DebugLineVertex {
    glm::vec3 position;
    glm::vec4 color;
};

// System for rendering debug lines and triangles using Vulkan
class DebugLineSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit DebugLineSystem(ConstructToken) {}

    // Factory: returns nullptr on failure
    static std::unique_ptr<DebugLineSystem> create(const vk::raii::Device& raiiDevice, VmaAllocator allocator,
                                                    vk::RenderPass renderPass,
                                                    const std::string& shaderPath,
                                                    uint32_t framesInFlight);
    static std::unique_ptr<DebugLineSystem> create(const InitContext& ctx, vk::RenderPass renderPass);

    // RAII members release every Vulkan resource in reverse declaration order
    ~DebugLineSystem() = default;

    // Non-copyable, non-movable (held via unique_ptr)
    DebugLineSystem(DebugLineSystem&&) = delete;
    DebugLineSystem& operator=(DebugLineSystem&&) = delete;
    DebugLineSystem(const DebugLineSystem&) = delete;
    DebugLineSystem& operator=(const DebugLineSystem&) = delete;

    // Begin collecting lines for this frame
    void beginFrame(uint32_t frameIndex);

    // Add lines directly
    void addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color);
    void addTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec4& color);

    // Bulk operations for performance (avoids per-call overhead)
    void reserveLines(size_t lineCount);
    void reserveTriangles(size_t triangleCount);
    void appendLineVertices(const DebugLineVertex* vertices, size_t count);
    void appendTriangleVertices(const DebugLineVertex* vertices, size_t count);

    // Persistent lines - not cleared each frame, only when explicitly cleared
    // Use for static debug geometry that doesn't change often
    void setPersistentLines(const DebugLineVertex* vertices, size_t count);
    void clearPersistentLines();
    size_t getPersistentLineCount() const { return persistentLineVertices.size() / 2; }
    void addBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void addSphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16);
    void addCapsule(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, int segments = 8);
    void addCone(const glm::vec3& base, const glm::vec3& tip, float radius, const glm::vec4& color, int segments = 8);

#ifdef JPH_DEBUG_RENDERER
    // Import lines from physics debug renderer
    void importFromPhysicsDebugRenderer(const PhysicsDebugRenderer& renderer);
#endif

    // Upload collected lines to GPU
    void uploadLines();

    // Record draw commands
    void recordCommands(vk::CommandBuffer cmd, const glm::mat4& viewProj);

    // Check if there are any lines to draw
    bool hasLines() const { return !lineVertices.empty() || !persistentLineVertices.empty(); }

    // Statistics
    size_t getLineCount() const { return (lineVertices.size() + persistentLineVertices.size()) / 2; }
    size_t getTriangleCount() const { return triangleVertices.size() / 3; }

private:
    bool initInternal(const vk::raii::Device& raiiDevice, VmaAllocator allocator, vk::RenderPass renderPass,
                      const std::string& shaderPath, uint32_t framesInFlight);
    bool createPipeline(vk::RenderPass renderPass, const std::string& shaderPath);

    // Persistently mapped host-visible vertex buffer; replaced wholesale when it must grow.
    struct VertexBuffer {
        ManagedBuffer buffer;
        void* mapped = nullptr;
        size_t size = 0;
    };
    // Creates (or grows) `vb` to hold at least `requiredSize` bytes.
    bool ensureVertexBuffer(VertexBuffer& vb, size_t requiredSize, const char* what);

    const vk::raii::Device* raiiDevice_ = nullptr;
    vk::Device device{};
    VmaAllocator allocator = nullptr;

    // Pipeline (layout declared before the pipelines that reference it)
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> linePipeline_;
    std::optional<vk::raii::Pipeline> trianglePipeline_;

    // Per-frame vertex buffers (double-buffered)
    struct FrameData {
        VertexBuffer lines;
        VertexBuffer triangles;
    };
    std::vector<FrameData> frameData;
    uint32_t currentFrame = 0;

    // Collected vertices for current frame (cleared each frame)
    std::vector<DebugLineVertex> lineVertices;
    std::vector<DebugLineVertex> triangleVertices;

    // Persistent line vertices (not cleared each frame)
    std::vector<DebugLineVertex> persistentLineVertices;

    static constexpr size_t INITIAL_BUFFER_SIZE = 64 * 1024; // 64KB initial buffer
};
