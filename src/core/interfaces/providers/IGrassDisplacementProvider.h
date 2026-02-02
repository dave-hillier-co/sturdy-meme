#pragma once

// ============================================================================
// IGrassDisplacementProvider.h - Interface for grass displacement data
// ============================================================================
//
// This interface decouples grass displacement consumers from GrassSystem.
// The leaf system uses grass displacement to know where grass has been
// pushed down (e.g., by characters walking through it).
//
// Benefits:
// - Wiring classes work with interfaces, not concrete types
// - Displacement can come from different sources
//

#include <vulkan/vulkan.h>

/**
 * Interface for systems that provide grass displacement data.
 *
 * Grass displacement is a texture showing where grass has been
 * pushed down by characters or objects. This is used by the
 * leaf system to spawn leaves where grass is disturbed.
 *
 * Implementations: GrassSystem
 */
class IGrassDisplacementProvider {
public:
    virtual ~IGrassDisplacementProvider() = default;

    /**
     * Get the grass displacement image view.
     */
    virtual VkImageView getDisplacementImageView() const = 0;

    /**
     * Get the sampler for displacement sampling.
     */
    virtual VkSampler getDisplacementSampler() const = 0;
};
