#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <optional>
#include <string>

#include "core/PerFrameBuffer.h"
#include "material/DescriptorManager.h"
#include "core/InitContext.h"
#include "culling/GPUCullPass.h"  // for GPUCullUniforms

// Forward declarations
class GPUSceneBuffer;

/**
 * ShadowCullPass - per-cascade GPU frustum culling for shadow-casting scene objects.
 *
 * Mirrors GPUCullPass but produces one independent set of indirect draw commands per
 * shadow cascade. It reuses the shared scene_cull.comp shader (cullMode = 1, so it also
 * rejects objects flagged as non-shadow-casters) and reads the GPUSceneBuffer's shared
 * cull-object buffer. Each cascade owns its own uniform/indirect/count buffers and
 * descriptor sets so the four cascade culls can be dispatched back-to-back into distinct
 * outputs within a single command buffer.
 *
 * Usage per frame, per cascade:
 *   1. updateUniforms(frame, cascade, lightViewProj, objectCount)
 *   2. bindSceneBuffer(sceneBuffer, frame, cascade)
 *   3. recordCulling(cmd, frame, cascade)
 *   4. draw from getIndirectBuffer(frame, cascade)
 */
class ShadowCullPass {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit ShadowCullPass(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
        uint32_t cascadeCount;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    static std::unique_ptr<ShadowCullPass> create(const InitInfo& info);

    ~ShadowCullPass();

    ShadowCullPass(const ShadowCullPass&) = delete;
    ShadowCullPass& operator=(const ShadowCullPass&) = delete;
    ShadowCullPass(ShadowCullPass&&) = delete;
    ShadowCullPass& operator=(ShadowCullPass&&) = delete;

    // Update culling uniforms for one cascade (extracts frustum planes from lightViewProj).
    void updateUniforms(uint32_t frameIndex, uint32_t cascade,
                        const glm::mat4& lightViewProj, uint32_t objectCount);

    // Write all descriptor bindings (shared cull-object buffer + per-cascade output buffers +
    // placeholder image) once. All buffers are stable for their lifetime, so this is called a
    // single time at init rather than per frame — avoiding host-synchronized descriptor updates
    // during command recording. Requires setPlaceholderImage() to have been called first.
    bool prepareDescriptors(GPUSceneBuffer* sceneBuffer);

    // Record the culling compute dispatch for one cascade.
    void recordCulling(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t cascade);

    // Per-cascade indirect command buffer (for vkCmdDrawIndexedIndirect).
    VkBuffer getIndirectBuffer(uint32_t frameIndex, uint32_t cascade) const;

    // Visible-object tally written by the cull shader (diagnostic; reflects the last GPU run).
    uint32_t getVisibleCount(uint32_t frameIndex, uint32_t cascade) const;

    // Placeholder image bound to the (unused) Hi-Z sampler slot; required on MoltenVK.
    void setPlaceholderImage(VkImageView view, VkSampler sampler);

private:
    bool initInternal(const InitInfo& info);
    void cleanup();
    bool createPipeline();
    bool createBuffers();
    bool createDescriptorSets();

    uint32_t flatIndex(uint32_t cascade, uint32_t frame) const { return cascade * framesInFlight_ + frame; }

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    uint32_t cascadeCount_ = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    std::optional<vk::raii::DescriptorSetLayout> descSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;

    // Flattened [cascade * framesInFlight + frame] descriptor sets.
    std::vector<VkDescriptorSet> descSets_;

    // Per-cascade per-frame buffers (each set holds framesInFlight buffers).
    std::vector<BufferUtils::PerFrameBufferSet> uniformBuffers_;   // [cascade]
    std::vector<BufferUtils::PerFrameBufferSet> indirectBuffers_;  // [cascade]
    std::vector<BufferUtils::PerFrameBufferSet> countBuffers_;     // [cascade]

    GPUSceneBuffer* currentSceneBuffer_ = nullptr;
    uint32_t lastObjectCount_ = 0;

    VkImageView placeholderImageView_ = VK_NULL_HANDLE;
    VkSampler placeholderSampler_ = VK_NULL_HANDLE;

    static constexpr uint32_t WORKGROUP_SIZE = 64;
    static constexpr uint32_t MAX_OBJECTS = 8192;
};
