#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "RendererBuilder.h"
#include "Camera.h"
#include "RendererSystems.h"
#include "passes/ShadowPassRecorder.h"
#include "passes/HDRPassRecorder.h"
#include "core/FrameUpdate.h"
#include "core/FrameDataBuilder.h"
#include "interfaces/IPlayerControl.h"
#include "UBOs.h"
#include "FrameData.h"
#include "RenderContext.h"
#include "loading/AsyncSystemLoader.h"  // complete type for unique_ptr<AsyncSystemLoader> member dtor

// Subsystem includes for render loop
// Core systems
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "ShadowSystem.h"
#include "GlobalBufferManager.h"
#include "Profiler.h"
#include "SceneManager.h"
#include "ResizeCoordinator.h"
#include "Mesh.h"
// Time and environment
#include "WindSystem.h"
#include "EnvironmentSettings.h"
#include "interfaces/IEnvironmentControl.h"
// Terrain and atmosphere
#include "TerrainSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "CloudShadowSystem.h"
#include "AtmosphereLUTSystem.h"
#include "FroxelSystem.h"
#include "SkySystem.h"
// Animation and debug
#include "SkinnedMeshRenderer.h"
#include "npc/NPCRenderer.h"
#include "npc/NPCSimulation.h"
#include "DebugLineSystem.h"
#include "HiZSystem.h"
#include "ScreenSpaceShadowSystem.h"
#include "GPUSceneBuffer.h"
#include "culling/GPUCullPass.h"
#include "interfaces/IDebugControl.h"
#include "controls/DebugControlSubsystem.h"
#include "threading/TaskScheduler.h"
// Vegetation
#include "GrassSystem.h"
#include "ScatterSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DisplacementSystem.h"
#include "CullCommon.h"  // For extractFrustumPlanes
// Water
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "SSRSystem.h"
// Post-processing
#include "BilateralGridSystem.h"
// Geometry
#include "CatmullClarkSystem.h"
// Weather
#include "WeatherSystem.h"
#include "LeafSystem.h"
// HDR scene composition (drawable registration)
#include "passes/SceneComposition.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <array>
#include <limits>
#include <algorithm>
#include <numeric>
#include <chrono>

std::unique_ptr<Renderer> Renderer::create(const InitInfo& info) {
    auto instance = std::make_unique<Renderer>(ConstructToken{});

    if (info.asyncInit) {
        // Async initialization path - starts background loading
        if (!RendererBuilder::initInternalAsync(*instance, info)) {
            return nullptr;
        }
    } else {
        // Synchronous initialization path (original behavior)
        if (!RendererBuilder::initInternal(*instance, info)) {
            return nullptr;
        }
    }
    return instance;
}

bool Renderer::pollAsyncInit() {
    return RendererBuilder::pollAsyncInit(*this);
}

Renderer::Renderer(ConstructToken) {}

Renderer::~Renderer() {
    cleanup();
}

#ifdef JPH_DEBUG_RENDERER
void Renderer::updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos) {
    if (!systems_->debugControlSubsystem().isPhysicsDebugEnabled()) return;

    // Begin debug line frame (clear previous and set frame index)
    // This is called here so physics debug lines can be collected before render()
    systems_->debugLine().beginFrame(frameExecutor_.currentFrameIndex());

    // Create debug renderer on first use (after Jolt is initialized)
    if (!systems_->physicsDebugRenderer()) {
        InitContext initCtx = InitContext::build(
            *vulkanContext_, vulkanContext_->getCommandPool(), getDescriptorPool(),
            resourcePath, MAX_FRAMES_IN_FLIGHT);
        systems_->createPhysicsDebugRenderer(initCtx, systems_->postProcess().getHDRRenderPass());
    }

    auto* debugRenderer = systems_->physicsDebugRenderer();
    if (!debugRenderer) return;

    // Begin physics debug frame
    debugRenderer->beginFrame(cameraPos);

    // Draw all physics bodies
    if (physics.getPhysicsSystem()) {
        debugRenderer->drawBodies(*physics.getPhysicsSystem());
    }

    // End frame (cleanup cached geometry)
    debugRenderer->endFrame();

    // Import collected lines into our debug line system
    systems_->debugLine().importFromPhysicsDebugRenderer(*debugRenderer);
}
#endif

void Renderer::cleanup() {
    vk::Device device = vulkanContext_->getVkDevice();
    VmaAllocator allocator = vulkanContext_->getAllocator();

    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        // Drop the frame-graph pass lambdas now (after the GPU is idle, before any captured
        // object is torn down). They capture raw pointers to Renderer members (recorders,
        // pipelines, VulkanContext, etc.); clearing here removes any dependence on member
        // destruction order.
        passScheduler_.clear();

        // Shutdown multi-threading infrastructure in reverse init order
        asyncTextureUploader_.shutdown();
        asyncTransferManager_.shutdown();
        threadedCommandPool_.shutdown();

        // Destroy FrameExecutor (owns TripleBuffering) before its dependencies
        frameExecutor_.destroy();

        // Destroy all subsystems via RendererSystems
        if (systems_) {
            systems_->destroy(device, allocator);
            systems_.reset();
        }

        // Clean up ScenePipeline (RAII objects must be reset while device is alive)
        scenePipeline_.reset();
        instancedScenePipeline_.reset();

        // Clean up descriptor pool
        if (descriptorPool_.has_value()) {
            descriptorPool_->destroy();
            descriptorPool_.reset();
        }

        // Note: command pool, render pass, depth resources, and framebuffers
        // are now owned by VulkanContext and cleaned up in its shutdown()
    }

    SDL_Log("calling vulkanContext_->shutdown");
    vulkanContext_->shutdown();
    SDL_Log("vulkanContext shutdown complete");
}

bool Renderer::render(const Camera& camera) {
    if (windowSuspended) return false;

    if (framebufferResized) {
        handleResize();
        framebufferResized = false;
    }

    FrameResult result = frameExecutor_.execute(
        [&](uint32_t imageIndex, uint32_t frameIndex) {
            return buildFrame(camera, imageIndex, frameIndex);
        });

    if (result == FrameResult::SwapchainOutOfDate ||
        result == FrameResult::SurfaceLost ||
        result == FrameResult::DeviceLost) {
        framebufferResized = true;
    }
    return result == FrameResult::Success;
}

vk::CommandBuffer Renderer::buildFrame(const Camera& camera, uint32_t imageIndex, uint32_t frameIndex) {
    // Simulation-update phase: transfers, time, UBOs, bone matrices, FrameData,
    // per-system updates, and GPU scene buffer population.
    FrameUpdate::Config cfg;
    cfg.useVolumetricSnow = useVolumetricSnow;
    cfg.shadowsEnabled = perfToggles.shadowPass;
    cfg.maxSnowHeight = MAX_SNOW_HEIGHT;
    cfg.lightCullRadius = lightCullRadius;
    cfg.ecsWorld = ecsWorld_;
    FrameUpdate::Result upd = FrameUpdate::run(*systems_, asyncTransferManager_, camera, frameIndex,
                                               vulkanContext_->getVkSwapchainExtent(), cfg);
    lastSunIntensity = upd.sunIntensity;
    FrameData& frame = upd.frame;
    lastViewProj = frame.viewProj;

    // Reset this frame's threaded command pools before any parallel recording.
    // The frame fence has already been waited on (FrameExecutor::execute), so the
    // GPU is done with this slot's secondary buffers and it's safe to reset the
    // pools — this returns the per-frame allocation counters to zero so the
    // pre-allocated buffers are reused instead of leaking new ones each frame.
    threadedCommandPool_.resetFrame(frame.frameIndex);

    // Command buffer recording
    vk::CommandBuffer cmd = vulkanContext_->getCommandBuffer(frame.frameIndex);
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.reset();
    vkCmd.begin(vk::CommandBufferBeginInfo{});

    systems_->profiler().beginGpuFrame(cmd, frame.frameIndex);

    RenderResources resources = FrameDataBuilder::buildRenderResources(
        *systems_, imageIndex, vulkanContext_->getFramebuffers(),
        vulkanContext_->getRenderPass(), {vulkanContext_->getWidth(), vulkanContext_->getHeight()},
        scenePipeline_.getGraphicsPipeline(), scenePipeline_.getPipelineLayout(),
        scenePipeline_.getDescriptorSetLayout());
    RenderContext renderCtx(cmd, frame.frameIndex, frame, resources, nullptr);

    PassScheduler::RenderContext psCtx(vkCmd, frame.frameIndex, frame);
    psCtx.imageIndex = imageIndex;
    psCtx.deltaTime = frame.deltaTime;
    psCtx.withUserData(&renderCtx)
        .withThreading(&threadedCommandPool_,
                       vk::RenderPass(systems_->postProcess().getHDRRenderPass()),
                       vk::Framebuffer(systems_->postProcess().getHDRFramebuffer()));

    passScheduler_.execute(psCtx, &TaskScheduler::instance());

    systems_->profiler().endGpuFrame(cmd, frame.frameIndex);
    vkCmd.end();

    // Advance buffer sets for next frame (safe before submit — command buffer
    // already has current frame's buffer references baked in)
    systems_->grass().advanceBufferSet();
    systems_->weather().advanceBufferSet();
    systems_->leaf().advanceBufferSet();
    if (systems_->hasWaterTileCull()) {
        systems_->waterTileCull().endFrame(frameIndex);
    }

    return cmd;
}

void Renderer::waitIdle() {
    vulkanContext_->waitIdle();
}

void Renderer::waitForPreviousFrame() {
    frameExecutor_.waitForPreviousFrame();
}

bool Renderer::handleResize() {
    // Delegate all resize logic to the coordinator (pass {0,0} to trigger core handler)
    bool success = resizeCoordinator_->performResize(
        vulkanContext_->getVkDevice(),
        vulkanContext_->getAllocator(),
        {0, 0}
    );
    framebufferResized = false;
    return success;
}

void Renderer::notifyWindowFocusGained() {
    // When window regains focus (especially on macOS), the compositor may have
    // cached stale content. Invalidate ALL temporal history to prevent ghost frames
    // from any temporal blending systems.

    if (!windowFocusLost_) {
        // Focus wasn't lost, nothing to do
        return;
    }

    windowFocusLost_ = false;

    SDL_Log("Window focus gained - invalidating temporal history to prevent ghost frames");

    // Use the temporal system registry to reset all registered systems
    if (systems_) {
        systems_->resetAllTemporalHistory();
    }

    // Force swapchain clear on next frame to flush compositor cache
    // We set framebufferResized to trigger a full swapchain recreation
    // which includes clearing all swapchain images
    framebufferResized = true;
}

// Render pass recording helpers - pure command recording, no state mutation

void Renderer::createHDRPassRecorder() {
    // Scene composition (which drawables, in what order/slot) lives in SceneComposition.
    hdrPassRecorder_ = SceneComposition::buildHDRPassRecorder(
        *systems_,
        [this](vk::CommandBuffer cmd, uint32_t frameIndex) {
            if (ragdollDrawCallback_) {
                ragdollDrawCallback_(cmd, frameIndex);
            }
        });
}

// Resource access
DescriptorManager::Pool* Renderer::getDescriptorPool() { return descriptorPool_.has_value() ? &*descriptorPool_ : nullptr; }
