#pragma once

// ============================================================================
// ICloudShadowProvider.h - Interface for systems that provide cloud shadows
// ============================================================================
//
// This interface decouples cloud shadow consumers from CloudShadowSystem.
// Terrain, vegetation, and other outdoor systems use cloud shadows for
// dynamic lighting variation.
//
// Benefits:
// - Wiring classes work with interfaces, not concrete types
// - Cloud shadow can be disabled/stubbed without affecting consumers
// - Enables testing with mock cloud shadow providers
//

#include <vulkan/vulkan.h>

/**
 * Interface for systems that provide cloud shadow map resources.
 *
 * Cloud shadows are rendered from above, looking down, to create
 * a shadow map that moves across the terrain based on wind direction.
 *
 * Implementations: CloudShadowSystem
 */
class ICloudShadowProvider {
public:
    virtual ~ICloudShadowProvider() = default;

    /**
     * Get the cloud shadow map image view.
     * This is a 2D texture representing shadow density from clouds.
     */
    virtual VkImageView getCloudShadowView() const = 0;

    /**
     * Get the sampler for cloud shadow sampling.
     * Typically uses linear filtering for smooth shadow edges.
     */
    virtual VkSampler getCloudShadowSampler() const = 0;
};
