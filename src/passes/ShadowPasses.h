#pragma once

#include "FrameGraph.h"
#include <glm/glm.hpp>

class RendererSystems;
class ShadowPassRecorder;
class VulkanContext;
struct PerformanceToggles;

/**
 * ShadowPasses - Shadow map rendering pass definitions
 */
namespace ShadowPasses {

struct Config {
    float* lastSunIntensity = nullptr;
    PerformanceToggles* perfToggles = nullptr;
    ShadowPassRecorder* recorder = nullptr;
    VulkanContext* vulkanContext = nullptr;   // for hasMultiDrawIndirect / hasDrawIndirectFirstInstance
    bool* terrainEnabled = nullptr;
};

struct PassIds {
    FrameGraph::PassId shadow = FrameGraph::INVALID_PASS;
    FrameGraph::PassId shadowResolve = FrameGraph::INVALID_PASS;
};

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config);

} // namespace ShadowPasses
