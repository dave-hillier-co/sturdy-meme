#pragma once

// ============================================================================
// IWindBufferConsumer.h - Interface for systems that consume wind data
// ============================================================================
//
// This interface defines the binding contract for systems that need
// wind buffer resources for animation and particle effects.
//
// Benefits:
// - Wiring logic decoupled from concrete system types
// - Wind-affected systems self-document their dependency
// - Enables automated wiring and dependency validation
//

#include <vulkan/vulkan.h>
#include <vector>

/**
 * Interface for systems that consume wind buffer data.
 *
 * Wind buffers drive vegetation animation (grass, trees, leaves)
 * and particle effects (weather, debris).
 *
 * Implementations: GrassSystem, LeafSystem, WeatherSystem, TreeRenderer
 */
class IWindBufferConsumer {
public:
    virtual ~IWindBufferConsumer() = default;

    /**
     * Bind wind buffer resources to this system's descriptor sets.
     *
     * @param device Vulkan device for descriptor updates
     * @param buffers Wind buffers for each frame-in-flight
     */
    virtual void bindWindBuffers(VkDevice device, const std::vector<VkBuffer>& buffers) = 0;
};
