#pragma once

#include "PassScheduler.h"

class RendererSystems;
struct PerformanceToggles;

/**
 * WaterPasses - Water rendering pass definitions
 *
 * Includes: WaterGBuffer, SSR, WaterTileCull
 */
namespace WaterPasses {

struct Config {
    bool* hdrPassEnabled = nullptr;
    PerformanceToggles* perfToggles = nullptr;
};

struct PassIds {
    PassScheduler::PassId waterGBuffer = PassScheduler::INVALID_PASS;
    PassScheduler::PassId ssr = PassScheduler::INVALID_PASS;
    PassScheduler::PassId waterTileCull = PassScheduler::INVALID_PASS;
};

PassIds addPasses(PassScheduler& graph, RendererSystems& systems, const Config& config);

} // namespace WaterPasses
