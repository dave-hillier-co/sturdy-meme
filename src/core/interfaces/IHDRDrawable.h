#pragma once

// ============================================================================
// IHDRDrawable.h - Interface for systems that participate in HDR pass rendering
// ============================================================================
//
// Dependency-inverted interface for HDR pass recording. Instead of
// HDRPassRecorder depending on every concrete rendering system, systems
// implement this interface and register themselves with the recorder.
//
// This decouples HDRPassRecorder from concrete types, enabling:
// - Adding new rendering systems without modifying HDRPassRecorder
// - Easier testing with mock implementations
// - Clear rendering contracts
//

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstdint>

class GPUSceneBuffer;

/**
 * Per-frame parameters passed to HDR drawables during recording.
 *
 * Contains rendering configuration that may change each frame.
 * Drawables use what they need and ignore the rest.
 */
struct HDRDrawParams {
    bool terrainEnabled = true;

    // Scene object rendering
    const vk::Pipeline* sceneObjectsPipeline = nullptr;
    const vk::PipelineLayout* pipelineLayout = nullptr;
    glm::mat4 viewProj = glm::mat4(1.0f);

    // GPU-driven rendering (Phase 3.3)
    GPUSceneBuffer* gpuSceneBuffer = nullptr;
    const vk::Pipeline* instancedPipeline = nullptr;
    const vk::PipelineLayout* instancedPipelineLayout = nullptr;
    bool useIndirectDraw = false;
};

/**
 * Interface for systems that record draw commands in the HDR render pass.
 *
 * Implement this interface and register with HDRPassRecorder to participate
 * in HDR pass rendering. The recorder calls registered drawables in order,
 * wrapping each in a GPU profiler zone.
 */
class IHDRDrawable {
public:
    virtual ~IHDRDrawable() = default;

    /**
     * Record draw commands to the command buffer.
     *
     * @param cmd Command buffer in recording state (inside HDR render pass)
     * @param frameIndex Current frame index for triple-buffered resources
     * @param time Animation time in seconds
     * @param params Per-frame rendering parameters
     */
    virtual void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                float time, const HDRDrawParams& params) = 0;

    /**
     * Whether this drawable should be recorded this frame.
     *
     * Called before recordHDRDraw(). Return false to skip this drawable
     * (e.g., terrain disabled, water not visible).
     * Default returns true (always draw).
     */
    virtual bool shouldDraw(uint32_t frameIndex, const HDRDrawParams& params) const {
        return true;
    }
};
