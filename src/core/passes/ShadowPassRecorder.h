#pragma once

// ============================================================================
// ShadowPassRecorder.h - Stateless shadow pass recording logic
// ============================================================================
//
// Encapsulates all shadow pass recording that was previously in Renderer.
// This class handles:
// - Building callbacks for terrain, grass, trees, and skinned mesh shadows
// - Collecting shadow-casting objects
// - Recording the shadow pass via ShadowSystem
//
// Design: Stateless recording - all configuration passed as parameters to record()
// This ensures no stale config state and makes the recorder thread-safe.
//

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

#include "ShadowPassResources.h"

class RendererSystems;
struct PerformanceToggles;
struct Renderable;

/**
 * ShadowPassRecorder - Stateless shadow pass command recording
 *
 * All configuration is passed to record() to ensure no stale state.
 * The recorder only stores immutable resource references.
 */
class ShadowPassRecorder {
public:
    /**
     * Parameters for shadow pass recording.
     * Passed to record() each frame - no mutable config stored.
     */
    struct Params {
        bool terrainEnabled = true;
        bool terrainShadows = true;
        bool grassShadows = true;
    };

    // Construct with focused resources (preferred - reduced coupling)
    explicit ShadowPassRecorder(const ShadowPassResources& resources);

    // Construct with RendererSystems (convenience, collects resources internally)
    explicit ShadowPassRecorder(RendererSystems& systems);

    // Record the complete shadow pass (stateless - all config via params)
    void record(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                const glm::vec3& cameraPosition, const Params& params);

    // ========================================================================
    // Legacy API (deprecated - for backward compatibility during migration)
    // ========================================================================

    // Configuration for shadow recording (deprecated - use Params in record())
    struct Config {
        bool terrainEnabled = true;
        PerformanceToggles* perfToggles = nullptr;
    };

    // Deprecated: Set configuration (use Params in record() instead)
    [[deprecated("Use record() with Params parameter instead")]]
    void setConfig(const Config& config) { legacyConfig_ = config; }

    // Deprecated: Record using stored config (use record() with Params instead)
    [[deprecated("Use record() with Params parameter instead")]]
    void record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const glm::vec3& cameraPosition);

private:
    ShadowPassResources resources_;
    Config legacyConfig_;  // For deprecated API only
};
