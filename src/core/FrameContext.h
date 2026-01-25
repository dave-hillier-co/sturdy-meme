#pragma once

// ============================================================================
// FrameContext.h - Unified per-frame execution context
// ============================================================================
//
// This consolidates the previously separate RenderContext and FrameGraph::RenderContext
// into a single type that contains everything needed for command recording.
//
// Design:
// - FrameData: Pure scene state (camera, lighting, weather) - immutable per frame
// - FrameContext: Execution context (command buffer, frame index, resources)
//
// The FrameContext is passed through the frame graph and to all render passes.
// It contains references to frame-constant data and mutable recording state.
//

#include <vulkan/vulkan.hpp>
#include <cstdint>

#include "FrameData.h"

// Forward declarations
class ThreadedCommandPool;
struct QueueSubmitDiagnostics;
struct RenderResources;

/**
 * FrameContext - Unified execution context for all rendering operations
 *
 * Passed to frame graph passes, render recorders, and subsystem update/record methods.
 * Contains everything needed to record commands for the current frame.
 *
 * Usage:
 *   void MyPass::record(FrameContext& ctx) {
 *       vkCmdBindPipeline(ctx.cmd(), ...);  // Raw VkCommandBuffer
 *       ctx.commandBuffer.bindPipeline(...); // vulkan-hpp style
 *       // Access frame state via ctx.frame()
 *       auto& camera = ctx.frame();
 *       // Use frame index for buffer selection
 *       uint32_t bufferIndex = ctx.frameIndex;
 *   }
 */
struct FrameContext {
    // ========================================================================
    // Core execution state (always valid)
    // ========================================================================

    /// Command buffer for recording (primary or secondary)
    /// vulkan-hpp type for compatibility with existing code
    vk::CommandBuffer commandBuffer;

    /// Raw VkCommandBuffer accessor for C API calls
    VkCommandBuffer cmd() const { return static_cast<VkCommandBuffer>(commandBuffer); }

    /// Frame index for buffer selection (0 to MAX_FRAMES_IN_FLIGHT-1)
    uint32_t frameIndex = 0;

    /// Swapchain image index for this frame
    uint32_t imageIndex = 0;

    /// Time since last frame in seconds
    float deltaTime = 0.0f;

    /// Total elapsed time in seconds
    float time = 0.0f;

    // ========================================================================
    // Frame state (immutable reference to per-frame data)
    // ========================================================================

    /// Access the immutable frame data (camera, lighting, weather, etc.)
    const FrameData& frame() const { return *frameData_; }

    // ========================================================================
    // Render resources (optional, for passes that need GPU resources)
    // ========================================================================

    /// Render resources snapshot (HDR targets, shadow maps, pipelines)
    /// May be null for compute-only passes
    const RenderResources* resources = nullptr;

    // ========================================================================
    // Threading support (for parallel command recording)
    // ========================================================================

    /// Thread pool for secondary command buffer allocation
    ThreadedCommandPool* threadedCommandPool = nullptr;

    /// Render pass for secondary buffer inheritance
    vk::RenderPass renderPass;

    /// Framebuffer for secondary buffer inheritance
    vk::Framebuffer framebuffer;

    /// Recorded secondary buffers (filled by parallel recording)
    std::vector<vk::CommandBuffer>* secondaryBuffers = nullptr;

    // ========================================================================
    // Diagnostics (optional)
    // ========================================================================

    /// Command diagnostics for profiling (draw call counts, etc.)
    QueueSubmitDiagnostics* diagnostics = nullptr;

    /// User data for pass-specific context
    void* userData = nullptr;

    // ========================================================================
    // Construction
    // ========================================================================

    /// Default constructor (for delayed initialization)
    FrameContext() : frameData_(&defaultFrameData_) {}

    /// Construct with required fields (VkCommandBuffer)
    FrameContext(VkCommandBuffer cmdBuffer, uint32_t frame, const FrameData& data)
        : commandBuffer(cmdBuffer)
        , frameIndex(frame)
        , frameData_(&data) {}

    /// Construct with required fields (vk::CommandBuffer)
    FrameContext(vk::CommandBuffer cmdBuffer, uint32_t frame, const FrameData& data)
        : commandBuffer(cmdBuffer)
        , frameIndex(frame)
        , frameData_(&data) {}

    /// Full constructor with all common fields
    FrameContext(VkCommandBuffer cmdBuffer, uint32_t frame, uint32_t image,
                 float dt, float t, const FrameData& data,
                 const RenderResources* res = nullptr,
                 QueueSubmitDiagnostics* diag = nullptr)
        : commandBuffer(cmdBuffer)
        , frameIndex(frame)
        , imageIndex(image)
        , deltaTime(dt)
        , time(t)
        , frameData_(&data)
        , resources(res)
        , diagnostics(diag) {}

    // ========================================================================
    // Convenience accessors (delegate to FrameData)
    // ========================================================================

    const glm::vec3& cameraPosition() const { return frameData_->cameraPosition; }
    const glm::mat4& viewMatrix() const { return frameData_->view; }
    const glm::mat4& projectionMatrix() const { return frameData_->projection; }
    const glm::mat4& viewProjMatrix() const { return frameData_->viewProj; }
    const glm::vec3& sunDirection() const { return frameData_->sunDirection; }
    float sunIntensity() const { return frameData_->sunIntensity; }

    // ========================================================================
    // Builder pattern for optional fields
    // ========================================================================

    FrameContext& withResources(const RenderResources* res) {
        resources = res;
        return *this;
    }

    FrameContext& withDiagnostics(QueueSubmitDiagnostics* diag) {
        diagnostics = diag;
        return *this;
    }

    FrameContext& withThreading(ThreadedCommandPool* pool, vk::RenderPass rp, vk::Framebuffer fb) {
        threadedCommandPool = pool;
        renderPass = rp;
        framebuffer = fb;
        return *this;
    }

    FrameContext& withSecondaryBuffers(std::vector<vk::CommandBuffer>* buffers) {
        secondaryBuffers = buffers;
        return *this;
    }

    FrameContext& withUserData(void* data) {
        userData = data;
        return *this;
    }

private:
    const FrameData* frameData_;
    static inline FrameData defaultFrameData_{};  // For default constructor
};

// ============================================================================
// Note on RenderContext
// ============================================================================
// The legacy RenderContext type is defined in RenderContext.h and uses
// references for FrameData and RenderResources. FrameContext uses pointers
// for more flexibility. New code should use FrameContext; existing code
// using RenderContext continues to work unchanged.
