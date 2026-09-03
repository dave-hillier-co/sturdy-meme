#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <string>

#include "core/vulkan/VmaBuffer.h"
#include "material/DescriptorManager.h"
#include "core/InitContext.h"

// Forward declarations
class GPUSceneBuffer;

// GPU Culling uniforms (matches shader struct)
struct alignas(16) GPUCullUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];     // xyz = normal, w = distance
    glm::vec4 cameraPosition;       // xyz = camera pos, w = unused
    glm::vec4 screenParams;         // x = width, y = height, z = 1/width, w = 1/height
    uint32_t objectCount;           // Number of objects to cull
    uint32_t enableHiZ;             // 1 = use Hi-Z, 0 = frustum only
    uint32_t maxDrawCommands;       // Output buffer capacity
    uint32_t cullMode;              // 0 = color pass, 1 = shadow pass (rejects non-casters)
};

/**
 * GPUCullPass - GPU-driven frustum culling for scene objects
 *
 * This class handles:
 * 1. Compute shader-based frustum culling
 * 2. Indirect draw command generation
 * 3. Integration with Hi-Z pyramid for occlusion culling (future)
 *
 * Usage:
 *   1. create() - Factory method to initialize
 *   2. updateUniforms() - Set view/projection matrices each frame
 *   3. recordCulling() - Record compute dispatch in command buffer
 *   4. Use GPUSceneBuffer's indirect buffers for rendering
 */
class GPUCullPass {
public:
    // Passkey for controlled construction
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GPUCullPass(ConstructToken) {}

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Factory methods
    static std::unique_ptr<GPUCullPass> create(const InitInfo& info);
    static std::unique_ptr<GPUCullPass> create(const InitContext& ctx);

    ~GPUCullPass() = default;

    // Non-copyable, non-movable
    GPUCullPass(const GPUCullPass&) = delete;
    GPUCullPass& operator=(const GPUCullPass&) = delete;
    GPUCullPass(GPUCullPass&&) = delete;
    GPUCullPass& operator=(GPUCullPass&&) = delete;

    // Update culling uniforms (call before recording)
    void updateUniforms(uint32_t frameIndex,
                        const glm::mat4& view,
                        const glm::mat4& proj,
                        const glm::vec3& cameraPos,
                        uint32_t objectCount,
                        VkExtent2D screenExtent);

    // Bind scene buffer for culling
    void bindSceneBuffer(GPUSceneBuffer* sceneBuffer, uint32_t frameIndex);

    // Record culling compute pass
    // Assumes scene buffer is already uploaded
    void recordCulling(vk::CommandBuffer cmd, uint32_t frameIndex);

    // Get the uniform buffer for external binding
    vk::Buffer getUniformBuffer(uint32_t frameIndex) const;

    // Statistics
    struct CullingStats {
        uint32_t totalObjects;
        uint32_t visibleObjects;
    };
    CullingStats getStats(uint32_t frameIndex) const;

    // Enable/disable Hi-Z occlusion culling
    void setHiZEnabled(bool enabled) { hiZEnabled_ = enabled; }
    bool isHiZEnabled() const { return hiZEnabled_; }

    // Set Hi-Z pyramid for occlusion culling (optional)
    void setHiZPyramid(vk::ImageView pyramidView, vk::Sampler sampler);

    // Set placeholder image for when Hi-Z is not available (required for MoltenVK)
    void setPlaceholderImage(vk::ImageView view, vk::Sampler sampler);

private:
    bool initInternal(const InitInfo& info);

    bool createPipeline();
    bool createBuffers();
    bool createDescriptorSets();

    // Extract frustum planes from view-projection matrix
    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);

    vk::Device device_{};
    VmaAllocator allocator_ = nullptr;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> descSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;

    // Per-frame descriptor sets
    std::vector<vk::DescriptorSet> descSets_;

    // Per-frame uniform buffers (RAII, persistently mapped)
    std::vector<ManagedBuffer> uniformBuffers_;
    std::vector<void*> uniformMapped_;

    // Currently bound scene buffer
    GPUSceneBuffer* currentSceneBuffer_ = nullptr;

    // Hi-Z pyramid reference (optional)
    vk::ImageView hiZPyramidView_{};
    vk::Sampler hiZSampler_{};
    bool hiZEnabled_ = false;

    // Placeholder image for descriptor binding when Hi-Z is unavailable
    vk::ImageView placeholderImageView_{};
    vk::Sampler placeholderSampler_{};

    // Workgroup size (must match shader)
    static constexpr uint32_t WORKGROUP_SIZE = 64;
    static constexpr uint32_t MAX_OBJECTS = 8192;
};
