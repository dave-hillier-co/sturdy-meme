#pragma once

// ============================================================================
// IFrameBuffers.h - Interface for systems that provide per-frame UBO buffers
// ============================================================================
//
// This interface decouples systems that need access to frame-synchronized
// uniform buffers from the concrete GlobalBufferManager implementation.
//
// Benefits:
// - Systems can bind UBOs without GlobalBufferManager dependency
// - Enables alternative buffer management strategies
// - Makes buffer contract explicit
// - Reduces header dependencies on buffer utils
//

#include <vulkan/vulkan.hpp>
#include <vector>
#include <span>

/**
 * Interface for systems that provide per-frame uniform buffers.
 *
 * Implement this for systems that manage triple-buffered uniform buffers
 * (e.g., GlobalBufferManager). Consumers use these for descriptor set updates.
 */
class IFrameBuffers {
public:
    virtual ~IFrameBuffers() = default;

    /**
     * Get the main scene uniform buffers (one per frame in flight).
     * Contains camera matrices, time, etc.
     */
    virtual std::span<const vk::Buffer> getUniformBuffers() const = 0;

    /**
     * Get the uniform buffer size.
     */
    virtual size_t getUniformBufferSize() const = 0;

    /**
     * Get the light uniform buffers (one per frame in flight).
     * Contains light parameters for the scene.
     */
    virtual std::span<const vk::Buffer> getLightBuffers() const = 0;

    /**
     * Get the light buffer size.
     */
    virtual size_t getLightBufferSize() const = 0;

    /**
     * Get the number of frames in flight.
     */
    virtual uint32_t getFramesInFlight() const = 0;
};

/**
 * Extended interface for systems with additional specialized buffers.
 *
 * Use this when you need access to additional per-frame buffers
 * (snow, cloud shadow, etc.)
 */
class IExtendedFrameBuffers : public IFrameBuffers {
public:
    /**
     * Get the snow uniform buffers.
     */
    virtual std::span<const vk::Buffer> getSnowBuffers() const = 0;

    /**
     * Get the snow buffer size.
     */
    virtual size_t getSnowBufferSize() const = 0;

    /**
     * Get the cloud shadow uniform buffers.
     */
    virtual std::span<const vk::Buffer> getCloudShadowBuffers() const = 0;

    /**
     * Get the cloud shadow buffer size.
     */
    virtual size_t getCloudShadowBufferSize() const = 0;
};
