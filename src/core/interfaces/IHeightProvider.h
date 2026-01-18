#pragma once

// ============================================================================
// IHeightProvider.h - Interface for systems that provide height map access
// ============================================================================
//
// This interface decouples systems that need terrain height data from the
// concrete TerrainSystem implementation. Useful for vegetation placement,
// water rendering, and other terrain-dependent systems.
//
// Benefits:
// - Reduces coupling between vegetation/water systems and TerrainSystem
// - Enables mock implementations for testing
// - Makes height data contract explicit
// - Allows different height sources (terrain, procedural, etc.)
//

#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <array>

/**
 * Interface for systems that provide height map textures.
 *
 * Implement this for systems that maintain height data (e.g., TerrainSystem).
 * Consumers bind the height map view and sampler for GPU-side height queries.
 */
class IHeightProvider {
public:
    virtual ~IHeightProvider() = default;

    /**
     * Get the height map image view for shader binding.
     */
    virtual vk::ImageView getHeightMapView() const = 0;

    /**
     * Get the sampler for height map sampling.
     * Typically uses bilinear filtering for smooth interpolation.
     */
    virtual vk::Sampler getHeightMapSampler() const = 0;

    /**
     * Get the height map resolution.
     */
    virtual uint32_t getHeightMapResolution() const = 0;

    /**
     * Get CPU-side height data pointer (may be nullptr if not available).
     * Data is normalized [0,1] and needs heightScale multiplication.
     */
    virtual const float* getHeightMapData() const = 0;
};

/**
 * Extended interface for tile-based terrain height providers.
 *
 * Use this when the height provider also manages terrain tiles
 * with associated tile information buffers.
 */
class ITiledHeightProvider : public IHeightProvider {
public:
    /**
     * Get the terrain tile array texture view.
     */
    virtual vk::ImageView getTileArrayView() const = 0;

    /**
     * Get the sampler for tile texture sampling.
     */
    virtual vk::Sampler getTileSampler() const = 0;

    /**
     * Get the tile info buffer for a specific frame.
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual vk::Buffer getTileInfoBuffer(uint32_t frameIndex) const = 0;

    /**
     * Convenience method to get all three tile info buffers.
     */
    virtual std::array<vk::Buffer, 3> getTileInfoBuffers() const {
        return {
            getTileInfoBuffer(0),
            getTileInfoBuffer(1),
            getTileInfoBuffer(2)
        };
    }
};
