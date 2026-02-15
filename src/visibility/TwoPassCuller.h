#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "InitContext.h"
#include "DescriptorManager.h"
#include "VmaBuffer.h"
#include "PerFrameBuffer.h"

class GPUClusterBuffer;
class HiZSystem;

// Uniform data for the cluster culling compute shader
struct alignas(16) ClusterCullUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];
    glm::vec4 cameraPosition;
    glm::vec4 screenParams;    // width, height, 1/width, 1/height
    glm::vec4 depthParams;     // near, far, numMipLevels, unused
    uint32_t clusterCount;
    uint32_t instanceCount;
    uint32_t enableHiZ;
    uint32_t maxDrawCommands;
    uint32_t passIndex;        // 0 = pass 1 (prev visible), 1 = pass 2 (remaining)
    uint32_t _pad0, _pad1, _pad2;
};

/**
 * TwoPassCuller - Two-phase GPU occlusion culling for mesh clusters
 *
 * Implements the nanite-style two-pass approach:
 *
 * Pass 1 (early):
 *   - Test clusters visible in the previous frame (high hit rate)
 *   - Render these to produce an initial depth buffer
 *   - Build Hi-Z pyramid from this depth
 *
 * Pass 2 (late):
 *   - Test remaining clusters against the Hi-Z from pass 1
 *   - Catches newly visible clusters (disocclusion)
 *   - Results merged with pass 1 for final rendering
 *
 * The key insight: most clusters visible last frame are still visible,
 * so pass 1 produces a good depth buffer for pass 2's occlusion tests.
 *
 * Usage:
 *   1. create()
 *   2. updateUniforms() - set camera, frustum
 *   3. recordPass1() - cull previous frame's visible clusters
 *   4. [render pass 1 visible clusters]
 *   5. [build Hi-Z from pass 1 depth]
 *   6. recordPass2() - cull remaining clusters against Hi-Z
 *   7. [render pass 2 visible clusters]
 *   8. swapBuffers() - swap visible lists for next frame
 */
class TwoPassCuller {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TwoPassCuller(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
        uint32_t maxClusters;      // Max clusters to cull per frame
        uint32_t maxDrawCommands;   // Max indirect draw commands
        const vk::raii::Device* raiiDevice = nullptr;
    };

    static std::unique_ptr<TwoPassCuller> create(const InitInfo& info);
    static std::unique_ptr<TwoPassCuller> create(const InitContext& ctx,
                                                   uint32_t maxClusters,
                                                   uint32_t maxDrawCommands);

    ~TwoPassCuller();

    // Non-copyable, non-movable
    TwoPassCuller(const TwoPassCuller&) = delete;
    TwoPassCuller& operator=(const TwoPassCuller&) = delete;

    /**
     * Update culling uniforms for the current frame.
     */
    void updateUniforms(uint32_t frameIndex,
                         const glm::mat4& view, const glm::mat4& proj,
                         const glm::vec3& cameraPos,
                         const glm::vec4 frustumPlanes[6],
                         uint32_t clusterCount, uint32_t instanceCount,
                         float nearPlane, float farPlane, uint32_t hiZMipLevels);

    /**
     * Record pass 1: cull previous frame's visible clusters.
     * After this, render the visible clusters and build Hi-Z.
     */
    void recordPass1(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Record pass 2: cull remaining clusters against Hi-Z.
     * hiZView must be the Hi-Z pyramid built from pass 1.
     */
    void recordPass2(VkCommandBuffer cmd, uint32_t frameIndex, VkImageView hiZView);

    /**
     * Swap the visible cluster buffers (call at end of frame).
     * Current frame's visible list becomes next frame's "previous" list.
     */
    void swapBuffers();

    // Access indirect draw commands and counts
    VkBuffer getPass1IndirectBuffer(uint32_t frameIndex) const;
    VkBuffer getPass1DrawCountBuffer(uint32_t frameIndex) const;
    VkBuffer getPass2IndirectBuffer(uint32_t frameIndex) const;
    VkBuffer getPass2DrawCountBuffer(uint32_t frameIndex) const;

    // Statistics
    struct Stats {
        uint32_t pass1Visible;
        uint32_t pass2Visible;
        uint32_t totalCulled;
    };

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createBuffers();
    void destroyBuffers();

    bool createPipeline();
    void destroyPipeline();

    bool createDescriptorSets();
    void destroyDescriptorSets();

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    uint32_t maxClusters_ = 0;
    uint32_t maxDrawCommands_ = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> descSetLayout_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    // Per-frame buffers (double-buffered for pass 1 / pass 2)
    // Indirect draw command buffers
    BufferUtils::PerFrameBufferSet pass1IndirectBuffers_;
    BufferUtils::PerFrameBufferSet pass1DrawCountBuffers_;
    BufferUtils::PerFrameBufferSet pass2IndirectBuffers_;
    BufferUtils::PerFrameBufferSet pass2DrawCountBuffers_;

    // Visible cluster tracking (double-buffered)
    BufferUtils::PerFrameBufferSet visibleClusterBuffers_;     // Current frame output
    BufferUtils::PerFrameBufferSet visibleCountBuffers_;       // Current frame count
    BufferUtils::PerFrameBufferSet prevVisibleClusterBuffers_; // Previous frame (pass 1 input)
    BufferUtils::PerFrameBufferSet prevVisibleCountBuffers_;   // Previous frame count

    // Uniform buffers
    BufferUtils::PerFrameBufferSet uniformBuffers_;

    // Descriptor sets per frame
    std::vector<VkDescriptorSet> pass1DescSets_;
    std::vector<VkDescriptorSet> pass2DescSets_;

    // Ping-pong index for visible buffer swapping
    uint32_t currentBufferIndex_ = 0;
};
