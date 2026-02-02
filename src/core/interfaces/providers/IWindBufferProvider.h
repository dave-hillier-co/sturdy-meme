#pragma once

// ============================================================================
// IWindBufferProvider.h - Interface for systems that provide wind data
// ============================================================================
//
// This interface decouples wind consumers (grass, trees, weather particles)
// from the concrete WindSystem implementation.
//
// Benefits:
// - Vegetation systems depend on interface, not WindSystem directly
// - Wind can be procedural, baked, or simulation-based
// - Enables testing with mock wind data
//

#include <vulkan/vulkan.h>
#include <cstdint>

/**
 * Interface for systems that provide per-frame wind buffer data.
 *
 * Wind buffers contain wind direction, strength, and gust information
 * used by vegetation and particle systems for realistic motion.
 * Buffers are triple-buffered to match frame-in-flight count.
 *
 * Implementations: WindSystem
 */
class IWindBufferProvider {
public:
    virtual ~IWindBufferProvider() = default;

    /**
     * Get the wind uniform buffer for a specific frame.
     * Contains wind direction, strength, gust parameters.
     *
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkBuffer getWindBuffer(uint32_t frameIndex) const = 0;

    /**
     * Get the number of frames (typically 3 for triple buffering).
     */
    virtual uint32_t getFrameCount() const = 0;
};
