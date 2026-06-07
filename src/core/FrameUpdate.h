#pragma once

// ============================================================================
// FrameUpdate.h - Per-frame simulation-update phase
// ============================================================================
//
// Stateless component that runs the "update simulation state" phase of a frame,
// separate from command-buffer recording. Mirrors the existing helper style of
// UBOUpdater / FrameDataBuilder / FrameUpdater.
//
// Responsibilities (in order):
// - Process pending async transfers
// - Update the time system
// - Update all UBOs (via UBOUpdater)
// - Update skinned-mesh bone matrices
// - Build per-frame shared state (FrameData)
// - Run per-subsystem updates (debug lines, all systems)
// - Populate the GPU scene buffer
//

#include "FrameData.h"
#include <vulkan/vulkan.hpp>
#include <cstdint>

class Camera;
class RendererSystems;
class AsyncTransferManager;

namespace ecs {
class World;
}

struct FrameUpdate {
    struct Config {
        bool  showCascadeDebug = false;
        bool  useVolumetricSnow = true;
        bool  showSnowDepthDebug = false;
        bool  shadowsEnabled = true;
        bool  hdrEnabled = true;
        float maxSnowHeight = 0.0f;
        float lightCullRadius = 0.0f;
        ecs::World* ecsWorld = nullptr;
    };

    struct Result {
        FrameData frame;        // the per-frame shared state
        float sunIntensity = 0.0f;
    };

    // Runs the full simulation-update phase: processes transfers, updates time,
    // UBOs, bone matrices, builds FrameData, runs per-system updates, and
    // populates the GPU scene buffer.
    static Result run(RendererSystems& systems,
                      AsyncTransferManager& transferManager,
                      const Camera& camera,
                      uint32_t frameIndex,
                      VkExtent2D extent,
                      const Config& config);
};
