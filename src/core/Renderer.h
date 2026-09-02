#pragma once

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <memory>

#include "VulkanContext.h"
#include "RendererSystems.h"
#include "InitContext.h"
#include "PerformanceToggles.h"
#include "FrameExecutor.h"
#include "vulkan/AsyncTransferManager.h"
#include "vulkan/ThreadedCommandPool.h"
#include "pipeline/PassScheduler.h"
#include "loading/LoadJobFactory.h"
#include "asset/AssetRegistry.h"
#include "ScenePipeline.h"
#include "ScreenshotCapture.h"
#include "InstancedScenePipeline.h"
#include "material/DescriptorManager.h"

// Forward declarations
class Camera;
class PhysicsWorld;
class ShadowPassRecorder;
class HDRPassRecorder;
class ResizeCoordinator;

namespace ecs {
class World;
}

namespace Loading {
    class AsyncSystemLoader;
    struct SystemInitTask;
}

// Heavy async-init scaffolding (loader + stored InitContext) lives off the
// runtime Renderer object; defined in RendererBuilder.h, owned via unique_ptr
// and reset() once async init completes.
struct AsyncInitState;

// Result of polling async renderer initialization.
enum class AsyncInitStatus {
    Pending,  // Still loading: keep presenting the loading screen and poll again
    Ready,    // All subsystems initialized; the renderer can render frames
    Failed    // A task failed; see Renderer::asyncInitError(). Release the renderer.
};

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);


class Renderer {
    friend class RendererBuilder;

public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit Renderer(ConstructToken);

    // Configuration for renderer initialization
    struct Config {
        uint32_t setsPerPool = 64;
        DescriptorPoolSizes descriptorPoolSizes = DescriptorPoolSizes::standard();
    };

    // Progress callback for async loading feedback
    // Called during initialization with progress (0.0-1.0) and phase description
    using ProgressCallback = std::function<void(float progress, const char* phase)>;

    struct InitInfo {
        SDL_Window* window;
        std::string resourcePath;
        Config config{};  // Optional renderer configuration

        // Optional: pre-initialized VulkanContext (instance already created)
        // If provided, Renderer takes ownership and completes device init.
        // If nullptr, Renderer creates and fully initializes a new VulkanContext.
        std::unique_ptr<VulkanContext> vulkanContext;

        // Optional: progress callback for async loading feedback
        // When provided, renderer will call this between initialization phases
        // to allow the caller to render a loading screen with progress updates
        ProgressCallback progressCallback;

        // Enable async subsystem initialization (non-blocking loading screen)
        // When true, heavy subsystems are loaded on background threads while
        // the loading screen continues to render. This improves perceived startup time.
        bool asyncInit = false;
    };

    /**
     * Factory: Create and initialize Renderer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<Renderer> create(const InitInfo& info);


    ~Renderer();

    // Non-copyable, non-movable
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Returns true if frame was rendered, false if skipped (caller must handle GUI frame cancellation)
    bool render(const Camera& camera);
    void waitIdle();

    // Wait for the previous frame's GPU work to complete.
    // MUST be called before destroying/updating any mesh buffers that the previous frame used.
    // This prevents race conditions where GPU is reading buffers we're about to destroy.
    void waitForPreviousFrame();

    // Viewport dimensions
    uint32_t getWidth() const { return vulkanContext_->getWidth(); }
    uint32_t getHeight() const { return vulkanContext_->getHeight(); }

    // Handle window resize (recreate swapchain and dependent resources)
    bool handleResize();

    // Notify renderer that window was resized (will trigger resize on next render)
    void notifyWindowResized() { framebufferResized = true; }

    // Notify renderer that window was minimized/hidden (e.g., screen lock on macOS)
    void notifyWindowSuspended() {
        windowSuspended = true;
        frameExecutor_.setWindowSuspended(true);
    }

    // Notify renderer that window was restored (e.g., screen unlock on macOS)
    void notifyWindowRestored() {
        windowSuspended = false;
        frameExecutor_.setWindowSuspended(false);
        framebufferResized = true;  // Force swapchain recreation after restore
    }

    bool isWindowSuspended() const { return windowSuspended; }

    // Capture the next presented frame (including GUI) to screenshots/*.png.
    // Non-blocking: the copy rides the frame's command buffer and PNG encoding
    // runs on a worker thread.
    void requestScreenshot();

    // Notify renderer that window lost focus (user clicked another app)
    // On macOS, this can cause compositor to cache stale content
    void notifyWindowFocusLost() { windowFocusLost_ = true; }

    // Notify renderer that window regained focus - invalidate all temporal history
    // to prevent ghost frames from blending with stale compositor-cached content
    void notifyWindowFocusGained();

    // Vulkan handles for GUI/integration are obtained via getVulkanContext() below,
    // so Renderer is not a second-class vendor of Vulkan handles.

    // Access to VulkanContext
    VulkanContext& getVulkanContext() { return *vulkanContext_; }
    const VulkanContext& getVulkanContext() const { return *vulkanContext_; }

    // GUI rendering callback (called during swapchain render pass)
    using GuiRenderCallback = std::function<void(vk::CommandBuffer)>;
    void setGuiRenderCallback(GuiRenderCallback callback) { guiRenderCallback = callback; }

    // Ragdoll draw callback (called during HDR skinned character pass)
    using RagdollDrawCallback = std::function<void(vk::CommandBuffer, uint32_t)>;
    void setRagdollDrawCallback(RagdollDrawCallback callback) { ragdollDrawCallback_ = std::move(callback); }
    const RagdollDrawCallback& getRagdollDrawCallback() const { return ragdollDrawCallback_; }

    // RendererSystems access - use this for all subsystem access
    RendererSystems& getSystems() { return *systems_; }
    const RendererSystems& getSystems() const { return *systems_; }

    // Resource access
    vk::CommandPool getCommandPool() const { return vulkanContext_->getCommandPool(); }
    DescriptorManager::Pool* getDescriptorPool();
    std::string getShaderPath() const { return resourcePath + "/shaders"; }
    const std::string& getResourcePath() const { return resourcePath; }


    // Performance control
    PerformanceToggles& getPerformanceToggles() { return perfToggles; }
    const PerformanceToggles& getPerformanceToggles() const { return perfToggles; }

    // ECS integration for light updates
    void setECSWorld(ecs::World* world) { ecsWorld_ = world; }
    ecs::World* getECSWorld() const { return ecsWorld_; }

#ifdef JPH_DEBUG_RENDERER
    // Update physics debug visualization (call before render)
    void updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos);
#endif

    /**
     * Create async system loader for background initialization
     * Returns a loader that the caller can poll while rendering loading screen
     * Caller must call pollAsyncInit() until isAsyncInitComplete() returns true
     */
    static std::unique_ptr<class Loading::AsyncSystemLoader> createAsyncLoader(const InitInfo& info);

    /**
     * Check if async initialization is complete
     */
    bool isAsyncInitComplete() const { return asyncInitComplete_; }

    /**
     * Poll async loader for completions - call from main thread.
     * Pending while tasks are still running, Ready once everything is wired,
     * Failed if a task failed (asyncInitError() carries the loader's message).
     */
    AsyncInitStatus pollAsyncInit();

    /**
     * Loader error message after pollAsyncInit() returned Failed (empty otherwise)
     */
    const std::string& asyncInitError() const { return asyncInitError_; }

    /**
     * Stop async initialization and join its worker threads. Call before
     * touching the device from outside the renderer (e.g. tearing down the
     * loading screen) while init may still be running: workers submit to the
     * graphics queue, and vkDeviceWaitIdle elsewhere would race them.
     * Idempotent; a no-op when init is synchronous or already finished.
     */
    void cancelAsyncInit();

private:
    void cleanup();

    // Create and configure HDR pass recorder with all registered drawables
    void createHDRPassRecorder();

    // Frame building: updates UBOs, subsystems, records command buffer.
    // Called from the FrameExecutor callback during execute().
    vk::CommandBuffer buildFrame(const Camera& camera, uint32_t imageIndex, uint32_t frameIndex);

    std::string resourcePath;
    Config config_;  // Renderer configuration

    std::unique_ptr<VulkanContext> vulkanContext_;

    // All rendering subsystems - managed with automatic lifecycle
    std::unique_ptr<RendererSystems> systems_;

    // Scene rendering pipeline (layout + graphics pipeline)
    ScenePipeline scenePipeline_;

    // Instanced scene pipeline for GPU-driven indirect rendering. The indirect draw
    // path is the default (set INDIRECT_SCENE_DRAW=0 to force the CPU path); params are
    // built in HDRPass::buildParams. Built alongside scenePipeline_.
    InstancedScenePipeline instancedScenePipeline_;

    // Descriptor pool (shared resource allocator for all subsystems)
    std::optional<DescriptorManager::Pool> descriptorPool_;

    // Resize coordinator (orchestrates resize across subsystems)
    std::unique_ptr<ResizeCoordinator> resizeCoordinator_;

    glm::mat4 lastViewProj{1.0f};  // Cached view-projection for debug rendering
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    // Performance toggles for debugging
    PerformanceToggles perfToggles;

    // Frame execution (owns TripleBuffering, sync, acquire, submit, present)
    FrameExecutor frameExecutor_;

    // Screen grab capture (lazily created on first requestScreenshot)
    std::unique_ptr<ScreenshotCapture> screenshotCapture_;
    uint64_t renderedFrameCount_ = 0;
    uint64_t autoScreenshotFrame_ = 0;  // from SCREENSHOT_AFTER_FRAMES; 0 = disabled

    // Pass recorders (encapsulate pass recording logic extracted from Renderer)
    std::unique_ptr<ShadowPassRecorder> shadowPassRecorder_;
    std::unique_ptr<HDRPassRecorder> hdrPassRecorder_;

    // Multi-threading and asset infrastructure
    AsyncTransferManager asyncTransferManager_;
    ThreadedCommandPool threadedCommandPool_;
    PassScheduler passScheduler_;
    Loading::AsyncTextureUploader asyncTextureUploader_;
    AssetRegistry assetRegistry_;

    // Convenience accessor for frame count (matches TripleBuffering::DEFAULT_FRAME_COUNT)
    static constexpr int MAX_FRAMES_IN_FLIGHT = TripleBuffering::DEFAULT_FRAME_COUNT;

    float lastSunIntensity = 1.0f;

    // Note: Cloud parameters (cloudCoverage, cloudDensity, skyExposure, useParaboloidClouds)
    // are now managed by EnvironmentControlSubsystem as the authoritative source.

    bool framebufferResized = false;       // true = window resized, need to recreate swapchain
    bool windowSuspended = false;          // true = window minimized/hidden (macOS screen lock)
    bool windowFocusLost_ = false;         // true = window lost focus, need to invalidate temporal on regain


    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // ECS world for light updates
    ecs::World* ecsWorld_ = nullptr;
    float lastDeltaTime_ = 0.016f;         // For flicker animation

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    // Ragdoll draw callback
    RagdollDrawCallback ragdollDrawCallback_;

    // Progress callback for async loading (stored during init)
    ProgressCallback progressCallback_;

    // Async initialization state.
    // asyncInit_ holds the heavy scaffolding (loader + stored InitContext) only
    // for the async path; it is allocated in initInternalAsync and reset() when
    // async init completes. The completion/failure flags stay as plain bools so
    // isAsyncInitComplete() reports true immediately on the sync path.
    std::unique_ptr<AsyncInitState> asyncInit_;
    bool asyncInitComplete_ = true;  // True when not using async, or when async is done
    bool asyncInitFailed_ = false;   // True if async init encountered an error
    std::string asyncInitError_;     // Loader error message when asyncInitFailed_
};
