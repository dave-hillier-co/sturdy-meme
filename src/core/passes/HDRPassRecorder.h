#pragma once

// ============================================================================
// HDRPassRecorder.h - Stateless HDR render pass recording logic
// ============================================================================
//
// Encapsulates all HDR pass recording that was previously in Renderer.
// This class handles:
// - Beginning/ending the HDR render pass
// - Drawing sky, terrain, scene objects, grass, water, weather, debug lines
// - Secondary command buffer recording for parallel execution
//
// Design: Stateless recording - all configuration passed as parameters to record()
// This ensures no stale config state and makes the recorder thread-safe.
//

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>

#include "HDRPassResources.h"

class RendererSystems;
class GPUSceneBuffer;

/**
 * HDRPassRecorder - Stateless HDR pass command recording
 *
 * All configuration is passed to record() to ensure no stale state.
 * The recorder only stores immutable resource references.
 */
class HDRPassRecorder {
public:
    /**
     * Parameters for HDR pass recording.
     * Passed to record() each frame - no mutable config stored.
     */
    struct Params {
        bool terrainEnabled = true;
        const vk::Pipeline* sceneObjectsPipeline = nullptr;
        const vk::PipelineLayout* pipelineLayout = nullptr;
        glm::mat4 viewProj = glm::mat4(1.0f);

        // GPU-driven rendering (Phase 3.3)
        GPUSceneBuffer* gpuSceneBuffer = nullptr;              // Scene instance SSBO
        const vk::Pipeline* instancedPipeline = nullptr;       // Pipeline for instanced rendering
        const vk::PipelineLayout* instancedPipelineLayout = nullptr;
        bool useIndirectDraw = false;                          // Enable vkCmdDrawIndexedIndirectCount
    };

    // Construct with focused resources (preferred - reduced coupling)
    explicit HDRPassRecorder(const HDRPassResources& resources);

    // Construct with RendererSystems (convenience, collects resources internally)
    explicit HDRPassRecorder(RendererSystems& systems);

    // Record the complete HDR pass (stateless - all config via params)
    void record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const Params& params);

    // Record HDR pass with pre-recorded secondary command buffers
    void recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                               const std::vector<vk::CommandBuffer>& secondaries, const Params& params);

    // Record a specific slot to a secondary command buffer
    // Slot 0: Sky + Terrain + Catmull-Clark
    // Slot 1: Scene Objects + Skinned Character
    // Slot 2: Grass + Water + Leaves + Weather + Debug lines
    void recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                             uint32_t slot, const Params& params);

    // ========================================================================
    // Legacy API (deprecated - for backward compatibility during migration)
    // ========================================================================

    // Configuration for HDR recording (deprecated - use Params in record())
    struct Config {
        bool terrainEnabled = true;
        const vk::Pipeline* sceneObjectsPipeline = nullptr;
        const vk::PipelineLayout* pipelineLayout = nullptr;
        glm::mat4* lastViewProj = nullptr;
    };

    // Deprecated: Set configuration (use Params in record() instead)
    [[deprecated("Use record() with Params parameter instead")]]
    void setConfig(const Config& config) { legacyConfig_ = config; }

    // Deprecated: Record using stored config
    [[deprecated("Use record() with Params parameter instead")]]
    void record(VkCommandBuffer cmd, uint32_t frameIndex, float time);

    // Deprecated: Record with secondaries using stored config
    [[deprecated("Use recordWithSecondaries() with Params parameter instead")]]
    void recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                               const std::vector<vk::CommandBuffer>& secondaries);

    // Deprecated: Record secondary slot using stored config
    [[deprecated("Use recordSecondarySlot() with Params parameter instead")]]
    void recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t slot);

private:
    // Helper to record scene objects using the provided pipeline
    void recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex, const Params& params);

    // Helper to record scene objects using GPU-driven indirect rendering (Phase 3.3)
    void recordSceneObjectsIndirect(VkCommandBuffer cmd, uint32_t frameIndex, const Params& params);

    // Helper to record debug lines with viewport/scissor setup
    void recordDebugLines(VkCommandBuffer cmd, const glm::mat4& viewProj);

    HDRPassResources resources_;
    Config legacyConfig_;  // For deprecated API only
};
