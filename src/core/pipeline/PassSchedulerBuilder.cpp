#include "PassSchedulerBuilder.h"
#include "RendererSystems.h"

// Domain-specific pass modules
#include "passes/ComputePasses.h"
#include "passes/ShadowPasses.h"
#include "passes/WaterPasses.h"
#include "passes/HDRPass.h"
#include "passes/PostPasses.h"

#include <SDL3/SDL.h>

bool PassSchedulerBuilder::build(
    PassScheduler& passScheduler,
    RendererSystems& systems,
    const Callbacks& callbacks,
    const State& state)
{
    passScheduler.clear();

    // ===== ADD PASSES FROM DOMAIN MODULES =====

    // Compute passes (compute stage + froxel)
    ComputePasses::Config computeConfig;
    computeConfig.perfToggles = state.perfToggles;
    computeConfig.terrainEnabled = state.terrainEnabled;
    auto computeIds = ComputePasses::addPasses(passScheduler, systems, computeConfig);

    // Shadow passes (shadow map + screen-space resolve)
    ShadowPasses::Config shadowConfig;
    shadowConfig.lastSunIntensity = state.lastSunIntensity;
    shadowConfig.perfToggles = state.perfToggles;
    shadowConfig.recorder = state.shadowRecorder;
    shadowConfig.vulkanContext = state.vulkanContext;
    shadowConfig.terrainEnabled = state.terrainEnabled;
    auto shadowIds = ShadowPasses::addPasses(passScheduler, systems, shadowConfig);

    // Water passes (GBuffer, SSR, tile cull)
    WaterPasses::Config waterConfig;
    waterConfig.hdrPassEnabled = state.hdrPassEnabled;
    waterConfig.perfToggles = state.perfToggles;
    auto waterIds = WaterPasses::addPasses(passScheduler, systems, waterConfig);

    // HDR pass (main scene rendering)
    HDRPass::Config hdrConfig;
    hdrConfig.hdrPassEnabled = state.hdrPassEnabled;
    hdrConfig.recorder = state.hdrRecorder;
    hdrConfig.scenePipeline = state.scenePipeline;
    hdrConfig.instancedScenePipeline = state.instancedScenePipeline;
    hdrConfig.vulkanContext = state.vulkanContext;
    hdrConfig.lastViewProj = state.lastViewProj;
    hdrConfig.terrainEnabled = state.terrainEnabled;
    auto hdr = HDRPass::addPass(passScheduler, systems, hdrConfig);

    // Post passes (HiZ, bloom, bilateral grid, final composite)
    PostPasses::Config postConfig;
    postConfig.guiRenderCallback = callbacks.guiRenderCallback;
    postConfig.framebuffers = state.framebuffers;
    postConfig.perfToggles = state.perfToggles;
    auto postIds = PostPasses::addPasses(passScheduler, systems, postConfig);

    // ===== WIRE DEPENDENCIES =====
    // Shadow and Froxel depend on Compute
    passScheduler.addDependency(computeIds.compute, shadowIds.shadow);
    passScheduler.addDependency(computeIds.compute, computeIds.froxel);
    passScheduler.addDependency(computeIds.compute, waterIds.waterGBuffer);

    // GPU cull depends on Compute (needs scene data uploaded)
    if (computeIds.gpuCull != PassScheduler::INVALID_PASS) {
        passScheduler.addDependency(computeIds.compute, computeIds.gpuCull);
    }

    // Shadow resolve depends on shadow map pass
    if (shadowIds.shadowResolve != PassScheduler::INVALID_PASS) {
        passScheduler.addDependency(shadowIds.shadow, shadowIds.shadowResolve);
    }

    // HDR depends on Shadow (or ShadowResolve if available), Froxel, Water GBuffer, and GPU Cull
    if (shadowIds.shadowResolve != PassScheduler::INVALID_PASS) {
        passScheduler.addDependency(shadowIds.shadowResolve, hdr);
    } else {
        passScheduler.addDependency(shadowIds.shadow, hdr);
    }
    passScheduler.addDependency(computeIds.froxel, hdr);
    passScheduler.addDependency(waterIds.waterGBuffer, hdr);
    if (computeIds.gpuCull != PassScheduler::INVALID_PASS) {
        passScheduler.addDependency(computeIds.gpuCull, hdr);
    }

    // Post-HDR passes depend on HDR
    passScheduler.addDependency(hdr, waterIds.ssr);
    passScheduler.addDependency(hdr, waterIds.waterTileCull);
    passScheduler.addDependency(hdr, postIds.hiZ);
    passScheduler.addDependency(hdr, postIds.bilateralGrid);
    passScheduler.addDependency(hdr, postIds.godRays);

    // Bloom depends on HiZ
    passScheduler.addDependency(postIds.hiZ, postIds.bloom);

    // Final composite depends on all post-HDR passes
    passScheduler.addDependency(waterIds.ssr, postIds.postProcess);
    passScheduler.addDependency(waterIds.waterTileCull, postIds.postProcess);
    passScheduler.addDependency(postIds.bloom, postIds.postProcess);
    passScheduler.addDependency(postIds.bilateralGrid, postIds.postProcess);
    passScheduler.addDependency(postIds.godRays, postIds.postProcess);

    // Compile the graph
    if (!passScheduler.compile()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to compile PassScheduler");
        return false;
    }

    SDL_Log("PassScheduler setup complete:\n%s", passScheduler.debugString().c_str());
    return true;
}
