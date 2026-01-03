#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Graph.h"
#include "town_generator/building/Patch.h"
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace town_generator {
namespace building {

// Forward declaration
class Model;
class CurtainWall;

/**
 * Topology - Street graph pathfinding, faithful port from Haxe TownGeneratorOS
 *
 * Note on reference semantics: The original Haxe code uses reference equality
 * for Point objects. In C++, we use coordinate-based equality but need to be
 * careful about the mapping between Points and Nodes.
 */
class Topology {
private:
    Model* model_;
    geom::Graph graph_;

    std::vector<geom::Point> blocked_;

public:
    // Point <-> Node mappings (using coordinate-based lookup)
    std::unordered_map<geom::Point, geom::Node*> pt2node;
    std::unordered_map<geom::Node*, geom::Point> node2pt;

    std::vector<geom::Node*> inner;
    std::vector<geom::Node*> outer;

    explicit Topology(Model* model);

    std::vector<geom::Point> buildPath(
        const geom::Point& from,
        const geom::Point& to,
        const std::vector<geom::Node*>* exclude = nullptr
    );

    // Equality
    bool operator==(const Topology& other) const {
        return model_ == other.model_;
    }

    bool operator!=(const Topology& other) const {
        return !(*this == other);
    }

private:
    geom::Node* processPoint(const geom::Point& v);
};

} // namespace building
} // namespace town_generator
