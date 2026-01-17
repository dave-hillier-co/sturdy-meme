#pragma once

#include "FrameGraph.h"
#include "RenderPipeline.h"
#include "PerformanceToggles.h"
#include <functional>

class RendererSystems;
struct RenderContext;
class PostProcessSystem;

/**
 * FrameGraphBuilder - Configures the FrameGraph with all render passes
 *
 * Extracted from Renderer::setupFrameGraph() to reduce Renderer.cpp size
 * and consolidate frame graph setup logic in one place.
 *
 * The builder sets up the dependency structure:
 *   ComputeStage ──┬──> ShadowPass ──┐
 *                  ├──> Froxel ──────┼──> HDR ──┬──> SSR ─────────┐
 *                  └──> WaterGBuffer ┘          ├──> WaterTileCull┼──> PostProcess
 *                                               ├──> HiZ ──> Bloom┤
 *                                               └──> BilateralGrid┘
 */
class FrameGraphBuilder {
public:
    /**
     * Callbacks needed by frame graph passes
     * These are provided by Renderer since they depend on Renderer internals
     */
    struct Callbacks {
        // Shadow pass recording
        std::function<void(VkCommandBuffer, uint32_t, float, const glm::vec3&)> recordShadowPass;

        // HDR pass recording (sequential)
        std::function<void(VkCommandBuffer, uint32_t, float)> recordHDRPass;

        // HDR pass recording with secondaries (parallel)
        std::function<void(VkCommandBuffer, uint32_t, float, const std::vector<vk::CommandBuffer>&)> recordHDRPassWithSecondaries;

        // HDR secondary slot recording
        std::function<void(VkCommandBuffer, uint32_t, float, uint32_t)> recordHDRPassSecondarySlot;

        // GUI render callback
        std::function<void(VkCommandBuffer)>* guiRenderCallback = nullptr;
    };

    /**
     * State pointers from Renderer needed by passes
     */
    struct State {
        float* lastSunIntensity = nullptr;
        bool* hdrPassEnabled = nullptr;
        PerformanceToggles* perfToggles = nullptr;
        std::vector<vk::raii::Framebuffer>* framebuffers = nullptr;
    };

    /**
     * Build and configure the frame graph with all passes and dependencies
     *
     * @param frameGraph The FrameGraph to configure
     * @param systems Access to all renderer subsystems
     * @param renderPipeline The render pipeline for compute/froxel/post stages
     * @param callbacks Callback functions from Renderer
     * @param state State pointers from Renderer
     * @return true if compilation succeeded
     */
    static bool build(
        FrameGraph& frameGraph,
        RendererSystems& systems,
        RenderPipeline& renderPipeline,
        const Callbacks& callbacks,
        const State& state
    );
};
