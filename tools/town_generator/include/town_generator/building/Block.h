#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/wards/Ward.h"
#include <vector>
#include <map>

namespace town_generator {
namespace building {

class WardGroup;

/**
 * Block - A city block within a WardGroup
 *
 * Faithful port from mfcg.js Block class (lines 12176-12287).
 * A Block is created by recursive alley subdivision and contains:
 * - shape: the block perimeter polygon
 * - lots: individual building lots from TwistedBlock partitioning
 * - rects: rectangular approximations of lots (for simple rendering)
 * - buildings: complex building shapes (from Dwellings engine)
 * - courtyard: inner lots filtered out during building creation
 */
class Block {
public:
    // The perimeter shape of this block
    geom::Polygon shape;

    // The WardGroup this block belongs to
    WardGroup* group = nullptr;

    // Building lots from TwistedBlock partitioning
    std::vector<geom::Polygon> lots;

    // Rectangular approximations of lots (createRects output)
    std::vector<geom::Polygon> rects;

    // Complex building shapes (createBuildings output)
    std::vector<geom::Polygon> buildings;

    // Inner (courtyard) lots filtered out of buildings
    std::vector<geom::Polygon> courtyard;

    // Cache for polygon areas (optimization from mfcg.js)
    std::map<const geom::Polygon*, double> cacheArea;

    // Cache for OBBs (optimization from mfcg.js)
    std::map<const geom::Polygon*, std::vector<geom::Point>> cacheOBB;

    // Center point of the block (cached)
    geom::Point center;
    bool centerComputed = false;

    Block() = default;
    explicit Block(const geom::Polygon& shape, WardGroup* group = nullptr);

    // Create lots using TwistedBlock partitioning algorithm
    // Faithful to mfcg.js TwistedBlock.createLots (lines 12287-12325)
    void createLots();

    // Convert lots to rectangular approximations
    // Faithful to mfcg.js Block.createRects (lines 12177-12220)
    void createRects();

    // Create building shapes from rectangles
    // Faithful to mfcg.js Block.createBuildings (lines 12221-12252)
    void createBuildings();

    // Filter out inner lots that don't touch the block perimeter
    // Faithful to mfcg.js Block.filterInner (lines 12253-12286)
    // Returns inner lots, removes them from lots vector
    std::vector<geom::Polygon> filterInner();

    // Indent lots away from block center for setback variation
    // Faithful to mfcg.js Block.indentFronts (lines 12287-12302)
    void indentFronts();

    // Spawn trees in courtyard areas
    // Faithful to mfcg.js Block.spawnTrees (lines 12303-12325)
    std::vector<geom::Point> spawnTrees();

    // Get cached area of a polygon
    double getArea(const geom::Polygon& poly);

    // Get cached OBB of a polygon
    std::vector<geom::Point> getOBB(const geom::Polygon& poly);

    // Check if a polygon is approximately rectangular
    // Faithful to mfcg.js Block.isRectangle (lines 12213-12219)
    bool isRectangle(const geom::Polygon& poly);

    // Get the center of this block (cached)
    geom::Point getCenter();

private:
    // Check if a lot edge converges with any block edge
    bool edgeConvergesWithBlock(const geom::Point& v0, const geom::Point& v1) const;
};

/**
 * TwistedBlock - Partitioning algorithm for irregular block shapes
 *
 * Faithful port from mfcg.js TwistedBlock (lines 12267-12325).
 * Creates building lots by recursive subdivision with non-axis-aligned cuts.
 */
class TwistedBlock {
public:
    // Create lots from a block shape
    // minSq: minimum lot area
    // sizeChaos: variation in lot sizes (0-1)
    static std::vector<geom::Polygon> createLots(
        Block* block,
        const wards::AlleyParams& params
    );

private:
    // Recursive partitioning
    static void partition(
        const geom::Polygon& shape,
        double minSq,
        double sizeChaos,
        double minTurnOffset,
        std::vector<geom::Polygon>& result
    );
};

} // namespace building
} // namespace town_generator
