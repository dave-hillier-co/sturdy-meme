#pragma once

#include "PassScheduler.h"

class RendererSystems;
struct PerformanceToggles;

/**
 * ComputePasses - GPU compute dispatch pass definitions
 *
 * Includes: Compute stage, Froxel/Atmosphere
 */
namespace ComputePasses {

struct Config {
    PerformanceToggles* perfToggles = nullptr;
    bool* terrainEnabled = nullptr;
};

struct PassIds {
    PassScheduler::PassId compute = PassScheduler::INVALID_PASS;
    PassScheduler::PassId froxel = PassScheduler::INVALID_PASS;
    PassScheduler::PassId gpuCull = PassScheduler::INVALID_PASS;  // GPU-driven culling pass
};

PassIds addPasses(PassScheduler& graph, RendererSystems& systems, const Config& config);

} // namespace ComputePasses
