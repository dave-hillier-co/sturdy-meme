#include "HDRPass.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "Profiler.h"
#include "HDRPassRecorder.h"
#include "ScenePipeline.h"
#include "InstancedScenePipeline.h"
#include "vulkan/VulkanContext.h"

#include <cstdlib>
#include <string>
#include <SDL3/SDL.h>

namespace HDRPass {

// Runtime toggle for the GPU-driven indirect scene draw path. Defaults ON; set the
// environment variable INDIRECT_SCENE_DRAW=0 to force the CPU draw path (A/B comparison
// and a fallback). Evaluated once.
static bool indirectSceneDrawEnabled() {
    static const bool enabled = []{
        const char* v = std::getenv("INDIRECT_SCENE_DRAW");
        const bool on = (v == nullptr) || (std::string(v) != "0");
        SDL_Log("Indirect scene draw path: %s", on ? "ENABLED" : "disabled (CPU path)");
        return on;
    }();
    return enabled;
}

// Builds HDRPassRecorder::Params for a given frameIndex, replicating the param-building
// previously duplicated across Renderer::recordHDRPass / recordHDRPassWithSecondaries /
// recordHDRPassSecondarySlot.
static HDRPassRecorder::Params buildParams(
    RendererSystems& systems,
    const Config& config,
    uint32_t frameIndex)
{
    HDRPassRecorder::Params params;
    params.terrainEnabled = *config.terrainEnabled;
    params.sceneObjectsPipeline = config.scenePipeline->getGraphicsPipelinePtr();
    params.pipelineLayout = config.scenePipeline->getPipelineLayoutPtr();
    params.viewProj = *config.lastViewProj;

    // GPU-driven rendering params. The GPUCullPass produces per-object indirect commands and
    // SceneObjectsDrawable issues vkCmdDrawIndexedIndirect when the indirect path is active
    // (indirectSceneDrawEnabled, default on; INDIRECT_SCENE_DRAW=0 forces the CPU path).
    if (systems.hasGPUSceneBuffer() && systems.hasGPUCullPass()) {
        params.gpuSceneBuffer = &systems.gpuSceneBuffer();
        params.instancedPipelineLayout = config.instancedScenePipeline->getPipelineLayoutPtr();
        params.instancedPipeline = config.instancedScenePipeline->getGraphicsPipelinePtr();
        params.instanceDescriptorSet = config.instancedScenePipeline->getInstanceDescriptorSet(frameIndex);
        params.useIndirectDraw = indirectSceneDrawEnabled()
                              && config.instancedScenePipeline->hasPipeline()
                              && params.instanceDescriptorSet != VK_NULL_HANDLE;
        // vkCmdDrawIndexedIndirect needs both features; else fall back to direct instanced draw.
        params.canMultiDrawIndirect = config.vulkanContext->hasMultiDrawIndirect()
                                   && config.vulkanContext->hasDrawIndirectFirstInstance();
    }

    return params;
}

PassScheduler::PassId addPass(PassScheduler& graph, RendererSystems& systems, const Config& config) {
    bool* hdrPassEnabled = config.hdrPassEnabled;
    Config cfg = config;

    return graph.addPass({
        .name = "HDR",
        .execute = [&systems, hdrPassEnabled, cfg](PassScheduler::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled) {
                systems.profiler().beginCpuZone("RenderPassRecord");
                auto params = buildParams(systems, cfg, ctx.frameIndex);
                if (ctx.secondaryBuffers && !ctx.secondaryBuffers->empty()) {
                    // Execute with pre-recorded secondary buffers (parallel path)
                    cfg.recorder->recordWithSecondaries(ctx.commandBuffer, ctx.frameIndex,
                                                        renderCtx->frame.time, *ctx.secondaryBuffers, params);
                } else {
                    // Fallback to sequential recording
                    cfg.recorder->record(ctx.commandBuffer, ctx.frameIndex, renderCtx->frame.time, params);
                }
                systems.profiler().endCpuZone("RenderPassRecord");
            }
        },
        .canUseSecondary = true,
        .mainThreadOnly = true,  // Main thread begins render pass, but secondaries record in parallel
        .priority = 30,
        .secondarySlots = 3,  // 3 parallel recording slots
        .secondaryRecord = [&systems, cfg](PassScheduler::RenderContext& ctx, uint32_t slot) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            auto params = buildParams(systems, cfg, ctx.frameIndex);
            cfg.recorder->recordSecondarySlot(ctx.commandBuffer, ctx.frameIndex,
                                              renderCtx->frame.time, slot, params);
        }
    });
}

} // namespace HDRPass
