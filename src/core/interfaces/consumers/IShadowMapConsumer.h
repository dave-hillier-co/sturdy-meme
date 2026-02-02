#pragma once

// ============================================================================
// IShadowMapConsumer.h - Interface for systems that consume shadow maps
// ============================================================================
//
// This interface defines the binding contract for systems that need
// shadow map resources. Wiring classes use this interface to connect
// shadow providers to consumers without knowing concrete types.
//
// Benefits:
// - Wiring logic decoupled from concrete system types
// - Systems self-document their shadow map dependency
// - Enables automated wiring and dependency validation
//

#include <vulkan/vulkan.h>

/**
 * Interface for systems that consume directional shadow maps.
 *
 * Implementations should update their descriptor sets when
 * bindShadowMap is called, typically during initialization
 * or when shadow resources are recreated.
 *
 * Implementations: TerrainSystem, GrassSystem, MaterialRegistry
 */
class IShadowMapConsumer {
public:
    virtual ~IShadowMapConsumer() = default;

    /**
     * Bind the shadow map resources to this system's descriptor sets.
     *
     * @param device Vulkan device for descriptor updates
     * @param view Shadow map image view (CSM array)
     * @param sampler Shadow map sampler (comparison sampler)
     */
    virtual void bindShadowMap(VkDevice device, VkImageView view, VkSampler sampler) = 0;
};

/**
 * Interface for systems that consume point light shadow maps.
 */
class IPointShadowConsumer {
public:
    virtual ~IPointShadowConsumer() = default;

    /**
     * Bind point shadow resources for a specific frame.
     *
     * @param device Vulkan device for descriptor updates
     * @param frameIndex Frame index for the specific buffer
     * @param view Point shadow cube map array view
     * @param sampler Point shadow sampler
     */
    virtual void bindPointShadow(VkDevice device, uint32_t frameIndex,
                                  VkImageView view, VkSampler sampler) = 0;
};

/**
 * Interface for systems that consume spot light shadow maps.
 */
class ISpotShadowConsumer {
public:
    virtual ~ISpotShadowConsumer() = default;

    /**
     * Bind spot shadow resources for a specific frame.
     *
     * @param device Vulkan device for descriptor updates
     * @param frameIndex Frame index for the specific buffer
     * @param view Spot shadow 2D array view
     * @param sampler Spot shadow sampler
     */
    virtual void bindSpotShadow(VkDevice device, uint32_t frameIndex,
                                 VkImageView view, VkSampler sampler) = 0;
};
