#pragma once

#include "GrassConstants.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>

/**
 * GrassTile - Represents a grass tile in world space
 *
 * Each tile covers a TILE_SIZE x TILE_SIZE area of the world.
 * Tiles are streamed around the camera and track their last-used frame
 * for safe GPU resource management with triple buffering.
 *
 * Note: All tiles share a common instance buffer managed by GrassTileManager.
 * This class primarily tracks tile coordinates and usage for streaming.
 */
class GrassTile {
public:
    // Tile coordinate in the world grid (integer tile position)
    struct TileCoord {
        int x = 0;
        int z = 0;

        bool operator==(const TileCoord& other) const {
            return x == other.x && z == other.z;
        }

        bool operator!=(const TileCoord& other) const {
            return !(*this == other);
        }
    };

    // Tile coordinate hash for use in unordered containers
    struct TileCoordHash {
        std::size_t operator()(const TileCoord& coord) const {
            // Combine x and z into a single hash
            return std::hash<int>()(coord.x) ^ (std::hash<int>()(coord.z) << 16);
        }
    };

    GrassTile() = default;
    ~GrassTile() = default;

    // Copyable and movable (no GPU resources to manage)
    GrassTile(const GrassTile&) = default;
    GrassTile& operator=(const GrassTile&) = default;
    GrassTile(GrassTile&&) noexcept = default;
    GrassTile& operator=(GrassTile&&) noexcept = default;

    /**
     * Initialize tile with coordinate
     */
    void init(TileCoord coord) {
        coord_ = coord;
        lastUsedFrame_ = 0;
    }

    // Accessors
    TileCoord getCoord() const { return coord_; }

    /**
     * Get the world-space origin (corner) of this tile
     */
    glm::vec2 getWorldOrigin() const {
        return glm::vec2(
            static_cast<float>(coord_.x) * GrassConstants::TILE_SIZE,
            static_cast<float>(coord_.z) * GrassConstants::TILE_SIZE
        );
    }

    /**
     * Get the world-space center of this tile
     */
    glm::vec2 getWorldCenter() const {
        return getWorldOrigin() + glm::vec2(GrassConstants::TILE_SIZE * 0.5f);
    }

    /**
     * Check if tile is initialized
     */
    bool isValid() const { return true; }

    /**
     * Calculate squared distance from a world position to tile center
     */
    float distanceSquaredTo(const glm::vec2& worldPos) const {
        glm::vec2 center = getWorldCenter();
        glm::vec2 diff = worldPos - center;
        return glm::dot(diff, diff);
    }

    /**
     * Mark the tile as used this frame (for unload tracking)
     */
    void markUsed(uint64_t frameNumber) { lastUsedFrame_ = frameNumber; }

    /**
     * Get the last frame this tile was used
     */
    uint64_t getLastUsedFrame() const { return lastUsedFrame_; }

    /**
     * Check if tile is safe to unload (hasn't been used for N frames)
     * Uses triple buffering - wait at least 3 frames to ensure GPU isn't using it
     */
    bool canUnload(uint64_t currentFrame, uint32_t framesInFlight) const {
        return (currentFrame - lastUsedFrame_) > framesInFlight;
    }

private:
    TileCoord coord_{0, 0};
    uint64_t lastUsedFrame_ = 0;
};
