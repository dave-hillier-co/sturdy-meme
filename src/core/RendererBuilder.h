#pragma once

#include "Renderer.h"
#include "InitContext.h"

#include <deque>
#include <memory>
#include <vector>

// Heavy async-init scaffolding, kept off the runtime Renderer object.
// Owned by Renderer via a unique_ptr that is reset() once async init finishes.
// Holds the background system loader and the InitContext the async tasks read
// from (the context must outlive every task, so Renderer owns it through here).
struct AsyncInitState {
    std::unique_ptr<Loading::AsyncSystemLoader> loader;
    InitContext ctx;  // Stored for async task access (lifetime == this state)
    // Per-task InitContext copies, each carrying its own worker command pool
    // (command pools are externally synchronized so concurrent tasks must not
    // share one). Deque for stable addresses; tasks capture pointers.
    std::deque<InitContext> taskContexts;
};

// RendererBuilder - stateless construction/initialization helper for Renderer.
//
// All of Renderer's construction-time initialization lives here as static methods
// taking a Renderer& to operate on. RendererBuilder is a friend of Renderer so it
// can access private members. This keeps Renderer focused on the runtime/render path.
class RendererBuilder {
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

    // Helpers used inside task bodies. They take the systems built by the same
    // task explicitly (staged, not yet in the registry) because registry
    // registration only happens in that task's gpuWork.
    static bool createDescriptorSets(Renderer& r, SceneManager& scene);
    static bool initSkinnedMeshRenderer(Renderer& r, vk::RenderPass hdrRenderPass,
                                        std::unique_ptr<SkinnedMeshRenderer>& outSkinnedMesh,
                                        std::unique_ptr<NPCRenderer>& outNpcRenderer);
    static bool createSkinnedMeshRendererDescriptorSets(Renderer& r, SceneManager& scene,
                                                        SkinnedMeshRenderer& skinnedMesh);

    static void setupPassScheduler(Renderer& r);

    static AsyncInitStatus pollAsyncInit(Renderer& r);
};
