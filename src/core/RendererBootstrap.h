#pragma once

#include "Renderer.h"
#include "InitContext.h"

#include <vector>

// RendererBootstrap - stateless construction/initialization helper for Renderer.
//
// All of Renderer's construction-time initialization lives here as static methods
// taking a Renderer& to operate on. RendererBootstrap is a friend of Renderer so it
// can access private members. This keeps Renderer focused on the runtime/render path.
class RendererBootstrap {
public:
    static bool initInternal(Renderer& r, const Renderer::InitInfo& info);
    static bool initInternalAsync(Renderer& r, const Renderer::InitInfo& info);

    static bool initCoreVulkanResources(Renderer& r);
    static bool initDescriptorInfrastructure(Renderer& r);
    static std::vector<Loading::SystemInitTask> buildInitTasks(Renderer& r, const InitContext& initCtx);
    static bool initSubsystems(Renderer& r, const InitContext& initCtx);
    static bool initSubsystemsAsync(Renderer& r);
    static void initResizeCoordinator(Renderer& r);
    static void initControlSubsystems(Renderer& r);
    static void initTemporalSystems(Renderer& r);

    static bool createDescriptorSets(Renderer& r);
    static bool initSkinnedMeshRenderer(Renderer& r);
    static bool createSkinnedMeshRendererDescriptorSets(Renderer& r);

    static void setupPassScheduler(Renderer& r);

    static bool pollAsyncInit(Renderer& r);
};
