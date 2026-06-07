#pragma once

#include "PassScheduler.h"
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
};

struct PassIds {
    PassScheduler::PassId shadow = PassScheduler::INVALID_PASS;
    PassScheduler::PassId shadowResolve = PassScheduler::INVALID_PASS;
};

PassIds addPasses(PassScheduler& graph, RendererSystems& systems, const Config& config);

} // namespace ShadowPasses
