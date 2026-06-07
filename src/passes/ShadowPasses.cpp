#include "ShadowPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "Profiler.h"
#include "PerformanceToggles.h"
#include "ScreenSpaceShadowSystem.h"
#include "ShadowPassRecorder.h"
#include "vulkan/VulkanContext.h"

#include <cstdlib>
#include <string>
#include <SDL3/SDL.h>

namespace ShadowPasses {

// Runtime toggle for the GPU-driven indirect shadow path. Defaults ON; set the environment
// variable INDIRECT_SHADOW_DRAW=0 to force the instanced shadow path (A/B comparison and a
// fallback). Per-cascade GPU frustum culling + vkCmdDrawIndexedIndirect for scene-object
// shadows. Evaluated once.
static bool indirectShadowDrawEnabled() {
    static const bool enabled = []{
        const char* v = std::getenv("INDIRECT_SHADOW_DRAW");
        const bool on = (v == nullptr) || (std::string(v) != "0");
        SDL_Log("Indirect shadow draw path: %s", on ? "ENABLED" : "disabled (instanced path)");
        return on;
    }();
    return enabled;
}

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config) {
    PassIds ids;
    float* lastSunIntensity = config.lastSunIntensity;
    PerformanceToggles* perfToggles = config.perfToggles;
    ShadowPassRecorder* recorder = config.recorder;
    VulkanContext* vulkanContext = config.vulkanContext;
    bool* terrainEnabled = config.terrainEnabled;

    // Shadow map rendering pass
    ids.shadow = graph.addPass({
        .name = "Shadow",
        .execute = [&systems, lastSunIntensity, perfToggles, recorder, vulkanContext, terrainEnabled](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*lastSunIntensity > 0.001f && perfToggles->shadowPass) {
                systems.profiler().beginCpuZone("ShadowRecord");
                systems.profiler().beginGpuZone(ctx.commandBuffer, "ShadowPass");

                ShadowPassRecorder::Params params;
                params.terrainEnabled = *terrainEnabled;
                params.terrainShadows = perfToggles->terrainShadows;
                params.grassShadows = perfToggles->grassShadows;
                params.indirectShadowDraw = indirectShadowDrawEnabled();
                // vkCmdDrawIndexedIndirect needs both features; else the indirect path falls back
                // to a direct instanced draw (no per-object GPU culling).
                params.canMultiDrawIndirect = vulkanContext->hasMultiDrawIndirect()
                                           && vulkanContext->hasDrawIndirectFirstInstance();
                recorder->record(ctx.commandBuffer, ctx.frameIndex,
                                 renderCtx->frame.time, renderCtx->frame.cameraPosition, params);

                systems.profiler().endGpuZone(ctx.commandBuffer, "ShadowPass");
                systems.profiler().endCpuZone("ShadowRecord");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 50
    });

    // Screen-space shadow resolve pass (compute)
    if (systems.hasScreenSpaceShadow()) {
        ids.shadowResolve = graph.addPass({
            .name = "ShadowResolve",
            .execute = [&systems, lastSunIntensity, perfToggles](FrameGraph::RenderContext& ctx) {
                if (!systems.hasScreenSpaceShadow()) return;
                if (*lastSunIntensity <= 0.001f || !perfToggles->shadowPass) return;
                systems.profiler().beginGpuZone(ctx.commandBuffer, "ShadowResolve");
                systems.screenSpaceShadow()->record(ctx.commandBuffer, ctx.frameIndex);
                systems.profiler().endGpuZone(ctx.commandBuffer, "ShadowResolve");
            },
            .canUseSecondary = false,
            .mainThreadOnly = true,
            .priority = 45  // Between shadow (50) and HDR (30)
        });
    }

    return ids;
}

} // namespace ShadowPasses
