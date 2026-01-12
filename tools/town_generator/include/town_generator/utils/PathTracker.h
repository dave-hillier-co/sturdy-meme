#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/GeomUtils.h"
#include <vector>
#include <cmath>
#include <stdexcept>

namespace town_generator {
namespace utils {

/**
 * PathTracker - Track position along a polyline path
 *
 * Provides utilities for walking along a path and getting positions
 * at specific distances. Used for placing objects along roads,
 * creating curved labels, etc.
 *
 * Faithful port from mfcg-clean/utils/PathTracker.js
 */
class PathTracker {
public:
    std::vector<geom::Point> path;
    size_t size;

private:
    size_t curIndex;
    double offset;
    geom::Point curVector;
    double curLength;

public:
    /**
     * Create a path tracker
     * @param path Array of points forming the path (must have at least 2 points)
     */
    explicit PathTracker(const std::vector<geom::Point>& path)
        : path(path)
        , size(path.size())
        , curIndex(0)
        , offset(0)
        , curLength(0)
    {
        if (path.size() < 2) {
            throw std::invalid_argument("Path must have at least 2 points");
        }
        reset();
    }

    /**
     * Reset tracker to start of path
     */
    void reset() {
        curIndex = 0;
        offset = 0;
        curVector = path[1].subtract(path[0]);
        curLength = curVector.length();
    }

    /**
     * Get position at a specific distance along the path
     *
     * @param distance Distance along the path
     * @return Position, or nullopt if past end of path
     */
    std::optional<geom::Point> getPos(double distance) {
        // If distance is before current position, reset
        if (distance < offset) {
            reset();
        }

        // Advance through segments until we find the right one
        while (distance > offset + curLength) {
            curIndex++;

            if (curIndex >= size - 1) {
                reset();
                return std::nullopt; // Past end of path
            }

            offset += curLength;
            curVector = path[curIndex + 1].subtract(path[curIndex]);
            curLength = curVector.length();
        }

        // Interpolate within current segment
        double t = (distance - offset) / curLength;
        return geom::GeomUtils::lerp(path[curIndex], path[curIndex + 1], t);
    }

    /**
     * Get a segment of the path between two distances
     *
     * @param startDist Start distance along path
     * @param endDist End distance along path
     * @return Array of points forming the segment
     */
    std::vector<geom::Point> getSegment(double startDist, double endDist) {
        auto startPos = getPos(startDist);
        size_t startIdx = curIndex + 1;

        auto endPos = getPos(endDist);
        size_t endIdx = curIndex + 1;

        std::vector<geom::Point> result;

        // Add start position
        if (startPos) {
            result.push_back(*startPos);
        }

        // Add intermediate points
        for (size_t i = startIdx; i < endIdx && i < path.size(); ++i) {
            result.push_back(path[i]);
        }

        // Add end position
        if (endPos) {
            result.push_back(*endPos);
        }

        return result;
    }

    /**
     * Get total length of the path
     */
    double getTotalLength() const {
        double total = 0;
        for (size_t i = 0; i < path.size() - 1; ++i) {
            total += geom::Point::distance(path[i], path[i + 1]);
        }
        return total;
    }

    /**
     * Get current tangent (direction) vector
     */
    geom::Point getTangent() const {
        return curVector;
    }

    /**
     * Get normalized tangent vector
     */
    geom::Point getTangentNormalized() const {
        return curVector.norm();
    }

    /**
     * Get normal (perpendicular) vector at current position
     */
    geom::Point getNormal() const {
        geom::Point t = getTangentNormalized();
        return geom::Point(-t.y, t.x);
    }

    /**
     * Get current segment index
     */
    size_t getCurrentIndex() const {
        return curIndex;
    }

    /**
     * Get offset to start of current segment
     */
    double getCurrentOffset() const {
        return offset;
    }

    /**
     * Sample points evenly along the path
     *
     * @param count Number of samples
     * @param startOffset Distance to start from (default 0)
     * @param endOffset Distance to stop before end (default 0)
     * @return Array of evenly spaced points
     */
    std::vector<geom::Point> sample(int count, double startOffset = 0, double endOffset = 0) {
        double length = getTotalLength();
        double usableLength = length - startOffset - endOffset;

        std::vector<geom::Point> result;

        if (count <= 1 || usableLength <= 0) {
            auto pos = getPos(startOffset);
            if (pos) {
                result.push_back(*pos);
            }
            return result;
        }

        double step = usableLength / (count - 1);

        for (int i = 0; i < count; ++i) {
            auto pos = getPos(startOffset + i * step);
            if (pos) {
                result.push_back(*pos);
            }
        }

        return result;
    }

    /**
     * Get points along path at regular intervals
     *
     * @param spacing Distance between points
     * @return Array of points
     */
    std::vector<geom::Point> sampleSpaced(double spacing) {
        double length = getTotalLength();
        int count = static_cast<int>(std::floor(length / spacing)) + 1;

        std::vector<geom::Point> result;

        for (int i = 0; i < count; ++i) {
            auto pos = getPos(i * spacing);
            if (pos) {
                result.push_back(*pos);
            }
        }

        return result;
    }

    /**
     * Get position and tangent at a specific distance
     *
     * @param distance Distance along the path
     * @return Pair of (position, tangent), or nullopt if past end
     */
    std::optional<std::pair<geom::Point, geom::Point>> getPosAndTangent(double distance) {
        auto pos = getPos(distance);
        if (!pos) {
            return std::nullopt;
        }
        return std::make_pair(*pos, getTangentNormalized());
    }

    /**
     * Place points along the path with a callback for each position
     *
     * @param spacing Distance between placements
     * @param callback Function to call at each position (receives position and normal)
     */
    template<typename Func>
    void placeAlong(double spacing, Func callback) {
        double length = getTotalLength();
        double dist = 0;

        while (dist < length) {
            auto pos = getPos(dist);
            if (pos) {
                callback(*pos, getNormal());
            }
            dist += spacing;
        }
    }
};

} // namespace utils
} // namespace town_generator
