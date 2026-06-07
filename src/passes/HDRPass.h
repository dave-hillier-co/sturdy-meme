#pragma once

#include "FrameGraph.h"
#include <glm/glm.hpp>

class RendererSystems;
class HDRPassRecorder;
class ScenePipeline;
class InstancedScenePipeline;
class VulkanContext;

/**
 * HDRPass - Main scene HDR rendering pass
 *
 * Renders sky, terrain, scene objects, grass, water, weather, leaves
 * with parallel secondary command buffer support.
 */
namespace HDRPass {

struct Config {
    bool* hdrPassEnabled = nullptr;
    HDRPassRecorder* recorder = nullptr;
    ScenePipeline* scenePipeline = nullptr;
    InstancedScenePipeline* instancedScenePipeline = nullptr;
    VulkanContext* vulkanContext = nullptr;
    glm::mat4* lastViewProj = nullptr;
    bool* terrainEnabled = nullptr;
};

FrameGraph::PassId addPass(FrameGraph& graph, RendererSystems& systems, const Config& config);

} // namespace HDRPass
