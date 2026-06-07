#pragma once

#include "PassScheduler.h"
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
    HDRPassRecorder* recorder = nullptr;
    ScenePipeline* scenePipeline = nullptr;
    InstancedScenePipeline* instancedScenePipeline = nullptr;
    VulkanContext* vulkanContext = nullptr;
    glm::mat4* lastViewProj = nullptr;
};

PassScheduler::PassId addPass(PassScheduler& graph, RendererSystems& systems, const Config& config);

} // namespace HDRPass
