#pragma once

// ============================================================================
// ICloudShadowConsumer.h - Interface for systems that consume cloud shadows
// ============================================================================
//
// This interface defines the binding contract for systems that need
// cloud shadow resources for outdoor lighting.
//
// Benefits:
// - Wiring logic decoupled from concrete system types
// - Cloud shadows can be optionally disabled
// - Enables automated wiring and dependency validation
//

#include <vulkan/vulkan.h>

/**
 * Interface for systems that consume cloud shadow maps.
 *
 * Cloud shadows affect terrain, vegetation, and other outdoor
 * objects to create dynamic lighting variation from clouds.
 *
 * Implementations: TerrainSystem, GrassSystem, ScatterSystem,
 *                  MaterialRegistry, SkinnedMeshRenderer
 */
class ICloudShadowConsumer {
public:
    virtual ~ICloudShadowConsumer() = default;

    /**
     * Bind the cloud shadow resources to this system's descriptor sets.
     *
     * @param device Vulkan device for descriptor updates
     * @param view Cloud shadow map image view
     * @param sampler Cloud shadow sampler
     */
    virtual void bindCloudShadow(VkDevice device, VkImageView view, VkSampler sampler) = 0;
};
