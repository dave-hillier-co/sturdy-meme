#pragma once

// ============================================================================
// RendererCore.h - Core frame loop execution
// ============================================================================
//
// RendererCore handles the essential per-frame rendering operations:
// 1. Frame synchronization (semaphores, fences via TripleBuffering)
// 2. Acquiring swapchain images
// 3. Calling frameGraph.execute()
// 4. Queue submission and presentation
//
// This class focuses purely on frame execution mechanics, while Renderer
// handles initialization, subsystem management, and per-frame data building.
//
// Usage:
//   RendererCore core;
//   core.init(vulkanContext, frameGraph, commandBuffers);
//
//   // In render loop:
//   FrameExecutionParams params = buildFrameParams(...);
//   core.executeFrame(params);
//

#include "TripleBuffering.h"
#include "RenderContext.h"
#include "pipeline/FrameGraph.h"
#include "vulkan/ThreadedCommandPool.h"
#include <vulkan/vulkan.hpp>
#include <functional>

class VulkanContext;
class TaskScheduler;
struct QueueSubmitDiagnostics;

// Parameters needed for frame execution
struct FrameExecutionParams {
    // Frame identification
    uint32_t swapchainImageIndex = 0;

    // Command buffer for this frame
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // Context passed to frame graph passes
    RenderContext* renderContext = nullptr;

    // FrameGraph execution context
    FrameGraph::RenderContext frameGraphContext{};

    // Optional task scheduler for parallel pass execution
    TaskScheduler* taskScheduler = nullptr;

    // Diagnostics tracking
    QueueSubmitDiagnostics* diagnostics = nullptr;

    // Callbacks for profiling zones (optional)
    std::function<void(const char*)> beginCpuZone;
    std::function<void(const char*)> endCpuZone;
    std::function<void(VkCommandBuffer, uint32_t)> beginGpuFrame;
    std::function<void(VkCommandBuffer, uint32_t)> endGpuFrame;
};

// Result of frame execution
enum class FrameResult {
    Success,           // Frame rendered successfully
    SwapchainOutOfDate,// Swapchain needs recreation
    SurfaceLost,       // Surface lost (macOS screen lock)
    DeviceLost,        // Device lost
    AcquireFailed,     // Failed to acquire swapchain image
    SubmitFailed,      // Failed to submit command buffer
    Skipped            // Frame skipped (minimized, suspended)
};

class RendererCore {
public:
    RendererCore() = default;
    ~RendererCore() = default;

    // Non-copyable, movable
    RendererCore(const RendererCore&) = delete;
    RendererCore& operator=(const RendererCore&) = delete;
    RendererCore(RendererCore&&) noexcept = default;
    RendererCore& operator=(RendererCore&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    struct InitParams {
        VulkanContext* vulkanContext = nullptr;
        FrameGraph* frameGraph = nullptr;
        TripleBuffering* frameSync = nullptr;  // Reference to existing frame sync (owned by Renderer)
    };

    bool init(const InitParams& params);
    void destroy();
    bool isInitialized() const { return vulkanContext_ != nullptr && frameSync_ != nullptr && frameSync_->isInitialized(); }

    // =========================================================================
    // Frame Execution
    // =========================================================================

    // Begin a new frame: wait for sync and acquire swapchain image
    // Returns the swapchain image index on success, or empty on failure
    struct FrameBeginResult {
        bool success = false;
        uint32_t imageIndex = 0;
        FrameResult error = FrameResult::Success;
    };
    FrameBeginResult beginFrame();

    // Execute the frame graph with the given parameters
    void executeFrameGraph(const FrameExecutionParams& params);

    // Submit command buffer and present
    FrameResult submitAndPresent(const FrameExecutionParams& params);

    // Complete frame: advance synchronization
    void endFrame();

    // Convenience: execute entire frame pipeline
    FrameResult executeFrame(const FrameExecutionParams& params);

    // =========================================================================
    // Synchronization Access
    // =========================================================================

    TripleBuffering& getFrameSync() { return *frameSync_; }
    const TripleBuffering& getFrameSync() const { return *frameSync_; }

    uint32_t currentFrameIndex() const { return frameSync_->currentIndex(); }

    // Wait for previous frame's GPU work (before destroying resources)
    void waitForPreviousFrame() { frameSync_->waitForPreviousFrame(); }

    // Wait for all GPU work to complete
    void waitForAllFrames() { frameSync_->waitForAllFrames(); }

    // Check if current frame is ready (non-blocking)
    bool isCurrentFrameReady() const { return frameSync_->isCurrentFrameComplete(); }

    // =========================================================================
    // Resize Handling
    // =========================================================================

    void notifyResizeNeeded() { resizeNeeded_ = true; }
    bool isResizeNeeded() const { return resizeNeeded_; }
    void clearResizeFlag() { resizeNeeded_ = false; }

    void notifyWindowSuspended() { windowSuspended_ = true; }
    void notifyWindowRestored() {
        windowSuspended_ = false;
        resizeNeeded_ = true;
    }
    bool isWindowSuspended() const { return windowSuspended_; }

private:
    // Acquire swapchain image, handling errors
    FrameBeginResult acquireSwapchainImage();

    // Submit command buffer with timeline semaphore
    FrameResult submitCommandBuffer(VkCommandBuffer cmd, QueueSubmitDiagnostics* diagnostics);

    // Present to swapchain
    FrameResult present(uint32_t imageIndex, QueueSubmitDiagnostics* diagnostics);

    VulkanContext* vulkanContext_ = nullptr;
    FrameGraph* frameGraph_ = nullptr;
    TripleBuffering* frameSync_ = nullptr;  // Non-owning reference (owned by Renderer)

    bool resizeNeeded_ = false;
    bool windowSuspended_ = false;

    // Cached swapchain image index for current frame
    uint32_t currentImageIndex_ = 0;
};
