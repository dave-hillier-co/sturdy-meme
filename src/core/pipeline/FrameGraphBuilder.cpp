#include "FrameGraphBuilder.h"
#include "RendererSystems.h"
#include "RenderContext.h"

// Subsystem includes for pass lambdas
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "ShadowSystem.h"
#include "Profiler.h"
#include "FroxelSystem.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "SSRSystem.h"
#include "BilateralGridSystem.h"

#include <SDL3/SDL.h>

bool FrameGraphBuilder::build(
    FrameGraph& frameGraph,
    RendererSystems& systems,
    RenderPipeline& renderPipeline,
    const Callbacks& callbacks,
    const State& state)
{
    // Clear any existing passes
    frameGraph.clear();

    // Capture state needed by pass lambdas
    float* lastSunIntensity = state.lastSunIntensity;
    bool* hdrPassEnabled = state.hdrPassEnabled;
    PerformanceToggles* perfToggles = state.perfToggles;
    auto* framebuffers = state.framebuffers;
    auto* guiCallback = callbacks.guiRenderCallback;

    // ===== PASS DEFINITIONS =====
    // The frame graph organizes passes by dependencies, enabling parallel execution
    // where possible.

    // Compute pass - runs all GPU compute dispatches
    auto compute = frameGraph.addPass({
        .name = "Compute",
        .execute = [&systems, &renderPipeline](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.profiler().beginCpuZone("ComputeDispatch");
            renderPipeline.computeStage.execute(*renderCtx);
            systems.profiler().endCpuZone("ComputeDispatch");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 100  // Highest priority - runs first
    });

    // Shadow pass - renders shadow maps for cascaded shadows
    auto shadow = frameGraph.addPass({
        .name = "Shadow",
        .execute = [&systems, lastSunIntensity, perfToggles, &callbacks](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*lastSunIntensity > 0.001f && perfToggles->shadowPass) {
                systems.profiler().beginCpuZone("ShadowRecord");
                systems.profiler().beginGpuZone(ctx.commandBuffer, "ShadowPass");
                callbacks.recordShadowPass(ctx.commandBuffer, ctx.frameIndex,
                                renderCtx->frame.time, renderCtx->frame.cameraPosition);
                systems.profiler().endGpuZone(ctx.commandBuffer, "ShadowPass");
                systems.profiler().endCpuZone("ShadowRecord");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 50
    });

    // Froxel/Atmosphere pass - volumetric fog and atmosphere LUTs
    // Can run in parallel with Shadow since they don't share resources
    auto froxel = frameGraph.addPass({
        .name = "Froxel",
        .execute = [&systems, &renderPipeline, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.postProcess().setCameraPlanes(
                renderCtx->frame.nearPlane, renderCtx->frame.farPlane);
            if (renderPipeline.froxelStageFn && (perfToggles->froxelFog || perfToggles->atmosphereLUT)) {
                renderPipeline.froxelStageFn(*renderCtx);
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = false,  // Can run parallel with Shadow
        .priority = 50
    });

    // Water G-buffer pass - renders water to mini G-buffer
    auto waterGBuffer = frameGraph.addPass({
        .name = "WaterGBuffer",
        .execute = [&systems, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            vk::CommandBuffer vkCmd(ctx.commandBuffer);

            if (perfToggles->waterGBuffer &&
                systems.waterGBuffer().getPipeline() != VK_NULL_HANDLE &&
                systems.hasWaterTileCull() &&
                systems.waterTileCull().wasWaterVisibleLastFrame(ctx.frameIndex)) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "WaterGBuffer");
                systems.waterGBuffer().beginRenderPass(ctx.commandBuffer);

                vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   systems.waterGBuffer().getPipeline());
                vk::DescriptorSet gbufferDescSet =
                    systems.waterGBuffer().getDescriptorSet(ctx.frameIndex);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                        systems.waterGBuffer().getPipelineLayout(),
                                        0, gbufferDescSet, {});

                systems.water().recordMeshDraw(ctx.commandBuffer);
                systems.waterGBuffer().endRenderPass(ctx.commandBuffer);
                systems.profiler().endGpuZone(ctx.commandBuffer, "WaterGBuffer");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 40
    });

    // HDR pass - main scene rendering with parallel secondary command buffers
    auto hdr = frameGraph.addPass({
        .name = "HDR",
        .execute = [&systems, hdrPassEnabled, &callbacks](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled) {
                systems.profiler().beginCpuZone("RenderPassRecord");
                if (ctx.secondaryBuffers && !ctx.secondaryBuffers->empty()) {
                    // Execute with pre-recorded secondary buffers (parallel path)
                    callbacks.recordHDRPassWithSecondaries(ctx.commandBuffer, ctx.frameIndex,
                                                 renderCtx->frame.time, *ctx.secondaryBuffers);
                } else {
                    // Fallback to sequential recording
                    callbacks.recordHDRPass(ctx.commandBuffer, ctx.frameIndex, renderCtx->frame.time);
                }
                systems.profiler().endCpuZone("RenderPassRecord");
            }
        },
        .canUseSecondary = true,
        .mainThreadOnly = true,  // Main thread begins render pass, but secondaries record in parallel
        .priority = 30,
        .secondarySlots = 3,  // 3 parallel recording slots
        .secondaryRecord = [&callbacks](FrameGraph::RenderContext& ctx, uint32_t slot) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            callbacks.recordHDRPassSecondarySlot(ctx.commandBuffer, ctx.frameIndex,
                                       renderCtx->frame.time, slot);
        }
    });

    // SSR pass - screen-space reflections
    auto ssr = frameGraph.addPass({
        .name = "SSR",
        .execute = [&systems, hdrPassEnabled, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled && perfToggles->ssr && systems.ssr().isEnabled()) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "SSR");
                systems.ssr().recordCompute(ctx.commandBuffer, ctx.frameIndex,
                                        systems.postProcess().getHDRColorView(),
                                        systems.postProcess().getHDRDepthView(),
                                        renderCtx->frame.view, renderCtx->frame.projection,
                                        renderCtx->frame.cameraPosition);
                systems.profiler().endGpuZone(ctx.commandBuffer, "SSR");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 20
    });

    // Water tile culling pass
    auto waterTileCull = frameGraph.addPass({
        .name = "WaterTileCull",
        .execute = [&systems, hdrPassEnabled, perfToggles](FrameGraph::RenderContext& ctx) {
            if (!ctx.userData) return;
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (*hdrPassEnabled && perfToggles->waterTileCull && systems.waterTileCull().isEnabled()) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "WaterTileCull");
                glm::mat4 viewProj = renderCtx->frame.projection * renderCtx->frame.view;
                systems.waterTileCull().recordTileCull(ctx.commandBuffer, ctx.frameIndex,
                                              viewProj, renderCtx->frame.cameraPosition,
                                              systems.water().getWaterLevel(),
                                              systems.postProcess().getHDRDepthView());
                systems.profiler().endGpuZone(ctx.commandBuffer, "WaterTileCull");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 20
    });

    // Hi-Z pass - hierarchical Z-buffer generation
    auto hiZ = frameGraph.addPass({
        .name = "HiZ",
        .execute = [&renderPipeline](FrameGraph::RenderContext& ctx) {
            if (renderPipeline.postStage.hiZRecordFn) {
                RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
                if (!renderCtx) return;
                renderPipeline.postStage.hiZRecordFn(*renderCtx);
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 15
    });

    // Bloom pass - multi-pass bloom effect
    auto bloom = frameGraph.addPass({
        .name = "Bloom",
        .execute = [&systems, &renderPipeline](FrameGraph::RenderContext& ctx) {
            if (systems.postProcess().isBloomEnabled() && renderPipeline.postStage.bloomRecordFn) {
                RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
                if (!renderCtx) return;
                renderPipeline.postStage.bloomRecordFn(*renderCtx);
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 10
    });

    // Bilateral grid pass - local tone mapping
    auto bilateralGrid = frameGraph.addPass({
        .name = "BilateralGrid",
        .execute = [&systems](FrameGraph::RenderContext& ctx) {
            if (systems.postProcess().isLocalToneMapEnabled()) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "BilateralGrid");
                systems.bilateralGrid().recordBilateralGrid(ctx.commandBuffer, ctx.frameIndex,
                                                           systems.postProcess().getHDRColorView());
                systems.profiler().endGpuZone(ctx.commandBuffer, "BilateralGrid");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 10
    });

    // Post-process pass - final composite with tone mapping and GUI
    auto postProcess = frameGraph.addPass({
        .name = "PostProcess",
        .execute = [&systems, framebuffers, guiCallback](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.profiler().beginGpuZone(ctx.commandBuffer, "PostProcess");
            systems.postProcess().recordPostProcess(ctx.commandBuffer, ctx.frameIndex,
                *(*framebuffers)[ctx.imageIndex], renderCtx->frame.deltaTime,
                guiCallback ? *guiCallback : std::function<void(VkCommandBuffer)>{});
            systems.profiler().endGpuZone(ctx.commandBuffer, "PostProcess");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 0  // Lowest priority - runs last
    });

    // ===== DEPENDENCY DEFINITIONS =====
    // Shadow depends on Compute (needs terrain compute results)
    frameGraph.addDependency(compute, shadow);

    // Froxel depends on Compute (needs cloud shadow compute)
    frameGraph.addDependency(compute, froxel);

    // Water G-buffer depends on Compute
    frameGraph.addDependency(compute, waterGBuffer);

    // HDR depends on Shadow (needs shadow maps)
    frameGraph.addDependency(shadow, hdr);

    // HDR depends on Froxel (needs volumetric fog data)
    frameGraph.addDependency(froxel, hdr);

    // HDR depends on Water G-buffer (needs water depth)
    frameGraph.addDependency(waterGBuffer, hdr);

    // SSR, WaterTileCull, HiZ, BilateralGrid all depend on HDR
    frameGraph.addDependency(hdr, ssr);
    frameGraph.addDependency(hdr, waterTileCull);
    frameGraph.addDependency(hdr, hiZ);
    frameGraph.addDependency(hdr, bilateralGrid);

    // Bloom depends on HiZ (uses Hi-Z for optimization)
    frameGraph.addDependency(hiZ, bloom);

    // PostProcess depends on all post-HDR passes
    frameGraph.addDependency(ssr, postProcess);
    frameGraph.addDependency(waterTileCull, postProcess);
    frameGraph.addDependency(bloom, postProcess);
    frameGraph.addDependency(bilateralGrid, postProcess);

    // Compile the graph
    if (!frameGraph.compile()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to compile FrameGraph");
        return false;
    }

    SDL_Log("FrameGraph setup complete:\n%s", frameGraph.debugString().c_str());
    return true;
}
