#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <optional>
#include <memory>
#include "UBOs.h"
#include "CatmullClarkCBT.h"
#include "CatmullClarkMesh.h"
#include "DescriptorManager.h"
#include "VmaBuffer.h"
#include "interfaces/IRecordable.h"

// Push constants for rendering
// alignas(16) required for SIMD operations on glm::mat4
struct alignas(16) CatmullClarkPushConstants {
    glm::mat4 model;
};

// Push constants for subdivision compute shader
struct CatmullClarkSubdivisionPushConstants {
    float targetEdgePixels;
    float splitThreshold;
    float mergeThreshold;
    uint32_t padding;
};

// Catmull-Clark configuration
struct CatmullClarkConfig {
    glm::vec3 position = glm::vec3(0.0f, 3.0f, 0.0f);  // World position
    glm::vec3 scale = glm::vec3(2.0f);                  // Scale
    float targetEdgePixels = 12.0f;                     // Target triangle edge length in pixels
    int maxDepth = 16;                                  // Maximum subdivision depth
    float splitThreshold = 18.0f;                       // Screen pixels to trigger split
    float mergeThreshold = 6.0f;                        // Screen pixels to trigger merge
    std::string objPath;                                // Optional OBJ file path (empty = use cube)
};

class CatmullClarkSystem : public IRecordable {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit CatmullClarkSystem(ConstructToken) {}

    struct InitInfo {
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        vk::RenderPass renderPass;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        vk::Queue graphicsQueue;
        vk::CommandPool commandPool;
        const vk::raii::Device* raiiDevice = nullptr;  // vulkan-hpp RAII device
    };

    /**
     * Factory: Create and initialize CatmullClarkSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<CatmullClarkSystem> create(const InitInfo& info, const CatmullClarkConfig& config = {});


    ~CatmullClarkSystem();

    // Non-copyable, non-movable
    CatmullClarkSystem(const CatmullClarkSystem&) = delete;
    CatmullClarkSystem& operator=(const CatmullClarkSystem&) = delete;
    CatmullClarkSystem(CatmullClarkSystem&&) = delete;
    CatmullClarkSystem& operator=(CatmullClarkSystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Update descriptor sets with shared resources
    void updateDescriptorSets(vk::Device device, const std::vector<vk::Buffer>& sceneUniformBuffers);

    // Update uniforms for a frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& view, const glm::mat4& proj);

    // Record compute commands (subdivision update)
    void recordCompute(vk::CommandBuffer cmd, uint32_t frameIndex);

    // Record rendering (implements IRecordable)
    void recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex) override;

    // Config accessors
    const CatmullClarkConfig& getConfig() const { return config; }
    void setConfig(const CatmullClarkConfig& newConfig) { config = newConfig; }

    // Toggle wireframe mode
    void setWireframeMode(bool enabled) { wireframeMode = enabled; }
    bool isWireframeMode() const { return wireframeMode; }

private:
    bool initInternal(const InitInfo& info, const CatmullClarkConfig& config);
    void cleanup();
    // Initialization helpers
    bool createUniformBuffers();
    bool createIndirectBuffers();

    // Descriptor set creation
    bool createComputeDescriptorSetLayout();
    bool createRenderDescriptorSetLayout();
    bool createDescriptorSets();

    // Pipeline creation
    bool createSubdivisionPipeline();
    bool createRenderPipeline();
    bool createWireframePipeline();

    // Vulkan resources
    vk::Device device = VK_NULL_HANDLE;
    vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    vk::RenderPass renderPass = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    vk::Queue graphicsQueue = VK_NULL_HANDLE;
    vk::CommandPool commandPool = VK_NULL_HANDLE;

    // Composed subsystems (RAII-managed)
    std::unique_ptr<CatmullClarkCBT> cbt;
    std::optional<CatmullClarkMesh> mesh;  // Value type with RAII buffers

    // Indirect dispatch/draw buffers (RAII-managed)
    ManagedBuffer indirectDispatchBuffer_;
    ManagedBuffer indirectDrawBuffer_;

    // Uniform buffers (per frame in flight, RAII-managed)
    std::vector<ManagedBuffer> uniformBuffers_;
    std::vector<void*> uniformMappedPtrs;

    // RAII device reference
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Compute pipelines (RAII-managed)
    std::optional<vk::raii::DescriptorSetLayout> computeDescriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> subdivisionPipelineLayout_;
    std::optional<vk::raii::Pipeline> subdivisionPipeline_;

    // Render pipelines (RAII-managed)
    std::optional<vk::raii::DescriptorSetLayout> renderDescriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> renderPipelineLayout_;
    std::optional<vk::raii::Pipeline> renderPipeline_;
    std::optional<vk::raii::Pipeline> wireframePipeline_;

    // Descriptor sets
    std::vector<vk::DescriptorSet> computeDescriptorSets;  // Per frame
    std::vector<vk::DescriptorSet> renderDescriptorSets;   // Per frame

    // Configuration
    CatmullClarkConfig config;
    bool wireframeMode = false;

    // Constants
    static constexpr uint32_t SUBDIVISION_WORKGROUP_SIZE = 64;
};
