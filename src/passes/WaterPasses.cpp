#include "WaterPasses.h"
#include "RendererSystems.h"
#include "PerformanceToggles.h"
#include "Profiler.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "SSRSystem.h"
#include "PostProcessSystem.h"

namespace WaterPasses {

PassIds addPasses(PassScheduler& graph, RendererSystems& systems, const Config& config) {
    PassIds ids;
    PerformanceToggles* perfToggles = config.perfToggles;

    // Capture pointer to systems to avoid dangling reference issues
    // The systems object must outlive the frame graph (owned by Renderer)
    RendererSystems* systemsPtr = &systems;

    // Water G-buffer pass - renders water to mini G-buffer
    ids.waterGBuffer = graph.addPass({
        .name = "WaterGBuffer",
        .execute = [systemsPtr, perfToggles](PassScheduler::RenderContext& ctx) {
            if (!systemsPtr || !perfToggles) return;
            vk::CommandBuffer vkCmd(ctx.commandBuffer);

            if (perfToggles->waterGBuffer &&
                systemsPtr->waterGBuffer().getPipeline() != VK_NULL_HANDLE &&
                systemsPtr->waterGBuffer().hasDescriptorSets() &&
                systemsPtr->hasWaterTileCull() &&
                systemsPtr->waterTileCull().wasWaterVisibleLastFrame(ctx.frameIndex)) {
                systemsPtr->profiler().beginGpuZone(ctx.commandBuffer, "WaterGBuffer");
                systemsPtr->waterGBuffer().beginRenderPass(ctx.commandBuffer);

                vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   systemsPtr->waterGBuffer().getPipeline());
                vk::DescriptorSet gbufferDescSet =
                    systemsPtr->waterGBuffer().getDescriptorSet(ctx.frameIndex);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                        systemsPtr->waterGBuffer().getPipelineLayout(),
                                        0, gbufferDescSet, {});

                systemsPtr->water().recordMeshDraw(ctx.commandBuffer);
                systemsPtr->waterGBuffer().endRenderPass(ctx.commandBuffer);
                systemsPtr->profiler().endGpuZone(ctx.commandBuffer, "WaterGBuffer");
            }
        },
        .canUseSecondary = false,
        .priority = 40
    });

    // SSR pass - screen-space reflections
    ids.ssr = graph.addPass({
        .name = "SSR",
        .execute = [systemsPtr, perfToggles](PassScheduler::RenderContext& ctx) {
            if (!systemsPtr || !perfToggles) return;
            if (systemsPtr->postProcess().isHDRPassEnabled() && perfToggles->ssr && systemsPtr->ssr().isEnabled()) {
                systemsPtr->profiler().beginGpuZone(ctx.commandBuffer, "SSR");
                systemsPtr->ssr().recordCompute(ctx.commandBuffer, ctx.frameIndex,
                                        systemsPtr->postProcess().getHDRColorView(),
                                        systemsPtr->postProcess().getHDRDepthView(),
                                        ctx.viewMatrix(), ctx.projectionMatrix(),
                                        ctx.cameraPosition());
                systemsPtr->profiler().endGpuZone(ctx.commandBuffer, "SSR");
            }
        },
        .canUseSecondary = false,
        .priority = 20
    });

    // Water tile culling pass
    ids.waterTileCull = graph.addPass({
        .name = "WaterTileCull",
        .execute = [systemsPtr, perfToggles](PassScheduler::RenderContext& ctx) {
            if (!systemsPtr || !perfToggles) return;
            if (systemsPtr->postProcess().isHDRPassEnabled() && perfToggles->waterTileCull && systemsPtr->waterTileCull().isEnabled()) {
                systemsPtr->profiler().beginGpuZone(ctx.commandBuffer, "WaterTileCull");
                glm::mat4 viewProj = ctx.projectionMatrix() * ctx.viewMatrix();
                systemsPtr->waterTileCull().recordTileCull(ctx.commandBuffer, ctx.frameIndex,
                                              viewProj, ctx.cameraPosition(),
                                              systemsPtr->water().getWaterLevel(),
                                              systemsPtr->postProcess().getHDRDepthView());
                systemsPtr->profiler().endGpuZone(ctx.commandBuffer, "WaterTileCull");
            }
        },
        .canUseSecondary = false,
        .priority = 20
    });

    return ids;
}

} // namespace WaterPasses
