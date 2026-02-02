#pragma once

// ============================================================================
// IDepthBufferProvider.h - Interface for systems that provide depth buffers
// ============================================================================
//
// This interface decouples depth buffer consumers from PostProcessSystem.
// Weather particles and other effects need depth for soft particles.
//
// Benefits:
// - Wiring classes work with interfaces, not concrete types
// - Depth source can change without affecting consumers
//

#include <vulkan/vulkan.h>

/**
 * Interface for systems that provide HDR pass depth buffer.
 *
 * The HDR depth buffer is used by weather particles for soft
 * particle blending and depth-based effects.
 *
 * Implementations: PostProcessSystem
 */
class IDepthBufferProvider {
public:
    virtual ~IDepthBufferProvider() = default;

    /**
     * Get the HDR depth buffer image view.
     */
    virtual VkImageView getHDRDepthView() const = 0;
};
