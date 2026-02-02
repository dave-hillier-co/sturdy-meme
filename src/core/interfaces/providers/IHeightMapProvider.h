#pragma once

// ============================================================================
// IHeightMapProvider.h - Interface for systems that provide terrain height data
// ============================================================================
//
// This interface decouples height map consumers (grass, trees, objects)
// from the concrete TerrainSystem implementation.
//
// Benefits:
// - Vegetation placement depends on interface, not TerrainSystem
// - Height data can come from procedural, baked, or streamed sources
// - Enables testing with mock terrain data
//

#include <vulkan/vulkan.h>
#include <functional>

/**
 * Interface for systems that provide terrain height map resources.
 *
 * The height map is used by vegetation and object placement systems
 * to position content at the correct elevation.
 *
 * Implementations: TerrainSystem
 */
class IHeightMapProvider {
public:
    virtual ~IHeightMapProvider() = default;

    /**
     * Get the height map image view.
     * This is a 2D texture containing normalized height values.
     */
    virtual VkImageView getHeightMapView() const = 0;

    /**
     * Get the sampler for height map sampling.
     */
    virtual VkSampler getHeightMapSampler() const = 0;

    /**
     * Get the terrain height at a world position.
     * This is used for CPU-side object placement.
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Height at the given position
     */
    virtual float getHeightAt(float x, float z) const = 0;
};

/**
 * Extended interface for virtual texture terrain systems.
 *
 * Virtual texture terrain uses a tile cache with multiple layers
 * for different terrain attributes (albedo, normal, etc.)
 */
class ITerrainTileProvider {
public:
    virtual ~ITerrainTileProvider() = default;

    /**
     * Get the tile array image view (virtual texture cache).
     */
    virtual VkImageView getTileArrayView() const = 0;

    /**
     * Get the sampler for tile sampling.
     */
    virtual VkSampler getTileSampler() const = 0;

    /**
     * Get the tile info buffer for a specific frame.
     * Contains tile metadata for virtual texture lookup.
     *
     * @param frameIndex Frame index for triple-buffered resources
     */
    virtual VkBuffer getTileInfoBuffer(uint32_t frameIndex) const = 0;
};

/**
 * Extended interface for terrain with hole masking.
 *
 * Hole masks allow areas of terrain to be hidden (e.g., cave entrances).
 */
class ITerrainHoleMaskProvider {
public:
    virtual ~ITerrainHoleMaskProvider() = default;

    /**
     * Get the hole mask array image view.
     */
    virtual VkImageView getHoleMaskArrayView() const = 0;

    /**
     * Get the sampler for hole mask sampling.
     */
    virtual VkSampler getHoleMaskSampler() const = 0;
};
