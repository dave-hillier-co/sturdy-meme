#pragma once

// ============================================================================
// IShadowMapProvider.h - Interface for systems that provide shadow map access
// ============================================================================
//
// This interface decouples systems that need shadow maps from the concrete
// ShadowSystem implementation. Any system that renders shadows can implement
// this interface.
//
// Benefits:
// - SystemWiring can depend on interface rather than ShadowSystem
// - Enables mock implementations for testing
// - Makes shadow map contract explicit
// - Reduces compilation dependencies
//

#include <vulkan/vulkan.hpp>
#include <cstdint>

/**
 * Interface for systems that provide shadow map textures.
 *
 * Implement this for systems that render shadow maps (e.g., ShadowSystem,
 * CloudShadowSystem). Consumers bind the shadow map view and sampler
 * to their descriptor sets.
 */
class IShadowMapProvider {
public:
    virtual ~IShadowMapProvider() = default;

    /**
     * Get the shadow map image view for shader binding.
     * For cascaded shadow maps, this returns the array view containing all cascades.
     */
    virtual vk::ImageView getShadowImageView() const = 0;

    /**
     * Get the sampler configured for shadow map sampling.
     * Typically uses comparison sampling for PCF.
     */
    virtual vk::Sampler getShadowSampler() const = 0;

    /**
     * Get the shadow map resolution (width = height for square maps).
     */
    virtual uint32_t getShadowMapSize() const = 0;
};

/**
 * Extended interface for cascaded shadow map providers.
 *
 * Use this when you need access to individual cascade views
 * (e.g., for cascade-specific rendering or debugging).
 */
class ICascadedShadowMapProvider : public IShadowMapProvider {
public:
    /**
     * Get the number of shadow map cascades.
     */
    virtual uint32_t getCascadeCount() const = 0;

    /**
     * Get the image view for a specific cascade.
     * @param cascade Cascade index (0 to getCascadeCount()-1)
     */
    virtual vk::ImageView getCascadeView(uint32_t cascade) const = 0;

    /**
     * Get the render pass used for shadow map rendering.
     */
    virtual vk::RenderPass getShadowRenderPass() const = 0;

    /**
     * Get the framebuffer for a specific cascade.
     * @param cascade Cascade index
     */
    virtual vk::Framebuffer getCascadeFramebuffer(uint32_t cascade) const = 0;
};
