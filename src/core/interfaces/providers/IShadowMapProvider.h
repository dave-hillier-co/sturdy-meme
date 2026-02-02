#pragma once

// ============================================================================
// IShadowMapProvider.h - Interface for systems that provide shadow maps
// ============================================================================
//
// This interface decouples shadow map consumers from the concrete ShadowSystem.
// Systems that need shadow maps depend on this interface rather than ShadowSystem.
//
// Benefits:
// - Wiring classes work with interfaces, not concrete types
// - New shadow implementations can be added without changing consumers
// - Enables testing with mock shadow providers
// - Reduces compilation dependencies
//

#include <vulkan/vulkan.hpp>
#include <cstdint>

/**
 * Interface for systems that provide shadow map resources.
 *
 * The primary shadow map is typically a cascaded shadow map (CSM) array
 * with multiple cascade layers for different distance ranges.
 *
 * Implementations: ShadowSystem
 */
class IShadowMapProvider {
public:
    virtual ~IShadowMapProvider() = default;

    /**
     * Get the shadow map image view (typically a 2D array view for CSM).
     * This is the main directional light shadow map.
     */
    virtual vk::ImageView getShadowMapView() const = 0;

    /**
     * Get the sampler configured for shadow map sampling.
     * Typically uses comparison sampling for PCF filtering.
     */
    virtual vk::Sampler getShadowMapSampler() const = 0;
};

/**
 * Extended interface for systems that provide point light shadow maps.
 *
 * Point lights use cube map arrays, with one cube map per light.
 * These are triple-buffered to match frame-in-flight count.
 */
class IPointShadowProvider {
public:
    virtual ~IPointShadowProvider() = default;

    /**
     * Get the point shadow cube map array view for a specific frame.
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkImageView getPointShadowArrayView(uint32_t frameIndex) const = 0;

    /**
     * Get the sampler for point shadow sampling.
     */
    virtual VkSampler getPointShadowSampler() const = 0;
};

/**
 * Extended interface for systems that provide spot light shadow maps.
 *
 * Spot lights use 2D texture arrays, with one slice per light.
 * These are triple-buffered to match frame-in-flight count.
 */
class ISpotShadowProvider {
public:
    virtual ~ISpotShadowProvider() = default;

    /**
     * Get the spot shadow 2D array view for a specific frame.
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkImageView getSpotShadowArrayView(uint32_t frameIndex) const = 0;

    /**
     * Get the sampler for spot shadow sampling.
     */
    virtual VkSampler getSpotShadowSampler() const = 0;
};
