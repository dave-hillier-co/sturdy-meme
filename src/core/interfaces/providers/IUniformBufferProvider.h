#pragma once

// ============================================================================
// IUniformBufferProvider.h - Interface for systems that provide uniform buffers
// ============================================================================
//
// This interface decouples uniform buffer consumers from GlobalBufferManager.
// Most render systems need access to the global UBO containing camera,
// lighting, and time data.
//
// Benefits:
// - Systems depend on interface, not GlobalBufferManager
// - Buffer management can be changed without affecting consumers
// - Enables testing with mock uniform data
//

#include <vulkan/vulkan.h>
#include <cstdint>

/**
 * Interface for systems that provide the main uniform buffer.
 *
 * The uniform buffer contains per-frame global data including:
 * - View/projection matrices
 * - Camera position
 * - Light direction and colors
 * - Time values
 * - Shadow cascade matrices
 *
 * Implementations: GlobalBufferManager
 */
class IUniformBufferProvider {
public:
    virtual ~IUniformBufferProvider() = default;

    /**
     * Get the uniform buffer for a specific frame.
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkBuffer getUniformBuffer(uint32_t frameIndex) const = 0;

    /**
     * Get the size of the uniform buffer in bytes.
     */
    virtual VkDeviceSize getUniformBufferSize() const = 0;

    /**
     * Get the number of frames (typically 3 for triple buffering).
     */
    virtual uint32_t getFrameCount() const = 0;
};

/**
 * Extended interface for light buffer provider.
 *
 * The light buffer contains dynamic light data for point/spot lights.
 */
class ILightBufferProvider {
public:
    virtual ~ILightBufferProvider() = default;

    /**
     * Get the light buffer for a specific frame.
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkBuffer getLightBuffer(uint32_t frameIndex) const = 0;

    /**
     * Get the size of the light buffer in bytes.
     */
    virtual VkDeviceSize getLightBufferSize() const = 0;
};

/**
 * Extended interface for snow buffer provider.
 *
 * The snow buffer contains snow accumulation parameters.
 */
class ISnowBufferProvider {
public:
    virtual ~ISnowBufferProvider() = default;

    /**
     * Get the snow parameter buffer for a specific frame.
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkBuffer getSnowBuffer(uint32_t frameIndex) const = 0;
};

/**
 * Extended interface for cloud shadow buffer provider.
 *
 * The cloud shadow buffer contains cloud shadow transformation data.
 */
class ICloudShadowBufferProvider {
public:
    virtual ~ICloudShadowBufferProvider() = default;

    /**
     * Get the cloud shadow buffer for a specific frame.
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkBuffer getCloudShadowBuffer(uint32_t frameIndex) const = 0;
};
