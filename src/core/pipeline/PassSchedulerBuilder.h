#pragma once

#include "PassScheduler.h"
#include "PerformanceToggles.h"
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <functional>

class RendererSystems;
class ShadowPassRecorder;
class HDRPassRecorder;
class ScenePipeline;
class InstancedScenePipeline;
class VulkanContext;

/**
 * PassSchedulerBuilder - Wires together domain-specific render passes
 *
 * Delegates pass creation to domain modules in src/core/passes/:
 * - ComputePasses: GPU compute dispatches, froxel/atmosphere
 * - ShadowPasses: Shadow map rendering
 * - WaterPasses: Water GBuffer, SSR, tile culling
 * - HDRPass: Main scene rendering
 * - PostPasses: HiZ, bloom, bilateral grid, final composite
 *
 * This class only wires dependencies between passes:
 *   ComputeStage ──┬──> ShadowPass ──┐
 *                  ├──> Froxel ──────┼──> HDR ──┬──> SSR ─────────┐
 *                  └──> WaterGBuffer ┘          ├──> WaterTileCull┼──> PostProcess
 *                                               ├──> HiZ ──> Bloom┤
 *                                               └──> BilateralGrid┘
 */
class PassSchedulerBuilder {
public:
    struct Callbacks {
        std::function<void(vk::CommandBuffer)>* guiRenderCallback = nullptr;
    };

    struct State {
        float* lastSunIntensity = nullptr;
        PerformanceToggles* perfToggles = nullptr;
        std::vector<vk::raii::Framebuffer>* framebuffers = nullptr;

        // Pass-module inputs (the pass lambdas build Params and call the recorder directly)
        ShadowPassRecorder* shadowRecorder = nullptr;
        HDRPassRecorder* hdrRecorder = nullptr;
        ScenePipeline* scenePipeline = nullptr;
        InstancedScenePipeline* instancedScenePipeline = nullptr;
        VulkanContext* vulkanContext = nullptr;
        glm::mat4* lastViewProj = nullptr;
    };

    static bool build(
        PassScheduler& passScheduler,
        RendererSystems& systems,
        const Callbacks& callbacks,
        const State& state
    );
};
