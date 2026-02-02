#pragma once

// ============================================================================
// ISnowMaskProvider.h - Interface for systems that provide snow masks
// ============================================================================
//
// This interface decouples snow mask consumers from SnowMaskSystem.
// Terrain and vegetation use snow masks to blend snow coverage.
//
// Benefits:
// - Wiring classes work with interfaces, not concrete types
// - Snow can be disabled without affecting consumer code
// - Enables testing with mock snow data
//

#include <vulkan/vulkan.h>

/**
 * Interface for systems that provide snow mask resources.
 *
 * The snow mask indicates where snow has accumulated on surfaces.
 * This is used by terrain and vegetation for snow blending.
 *
 * Implementations: SnowMaskSystem
 */
class ISnowMaskProvider {
public:
    virtual ~ISnowMaskProvider() = default;

    /**
     * Get the snow mask image view.
     * Contains snow coverage/depth information.
     */
    virtual VkImageView getSnowMaskView() const = 0;

    /**
     * Get the sampler for snow mask sampling.
     */
    virtual VkSampler getSnowMaskSampler() const = 0;
};

/**
 * Interface for systems that provide volumetric snow cascades.
 *
 * Volumetric snow uses multiple cascades for different distance ranges,
 * similar to cascaded shadow maps.
 *
 * Implementations: VolumetricSnowSystem
 */
class IVolumetricSnowProvider {
public:
    virtual ~IVolumetricSnowProvider() = default;

    /**
     * Get a volumetric snow cascade image view.
     * @param cascade Cascade index (0 = nearest)
     */
    virtual VkImageView getVolumetricSnowCascadeView(uint32_t cascade) const = 0;

    /**
     * Get the sampler for volumetric snow sampling.
     */
    virtual VkSampler getVolumetricSnowSampler() const = 0;
};
