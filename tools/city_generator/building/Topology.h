#pragma once

#include "../geom/Graph.h"
#include "../geom/Point.h"
#include <map>
#include <vector>
#include <algorithm>

namespace towngenerator {
namespace building {

// Forward declarations
class Model;

/**
 * Comparator for Point to use as map key
 */
struct PointCompare {
    bool operator()(const geom::Point& a, const geom::Point& b) const {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    }
};

/**
 * Topology class - manages spatial graph for pathfinding
 * Ported from Haxe Topology class
 */
class Topology {
public:
    // The pathfinding graph
    geom::Graph graph;

    // Point to node mapping
    std::map<geom::Point, geom::Node*, PointCompare> pt2node;

    // Node to point mapping
    std::map<geom::Node*, geom::Point> node2pt;

    // Nodes within city
    std::vector<geom::Node*> inner;

    // Nodes outside city
    std::vector<geom::Node*> outer;

    // Impassable points (walls, citadel)
    std::vector<geom::Point> blocked;

    /**
     * Constructor - builds topology from model
     * @param model The city model containing patches, walls, etc.
     *
     * Algorithm:
     * 1. Builds blocked list from shore + walls (excluding gates)
     * 2. Iterates through all patches
     * 3. For each patch shape vertex:
     *    - Calls processPoint to get/create node
     *    - Links adjacent nodes with edge weight = distance
     *    - Categorizes as inner/outer based on withinCity flag
     */
    explicit Topology(const Model& model);

    /**
     * Process a point - get existing node or create new one
     * @param point The point to process
     * @return Node for this point, or nullptr if blocked
     *
     * Algorithm:
     * 1. If point already processed, return existing node
     * 2. If point is blocked, return nullptr
     * 3. Create new node, add to mappings, return it
     */
    geom::Node* processPoint(const geom::Point& point) {
        // If point already processed, return existing node
        auto it = pt2node.find(point);
        if (it != pt2node.end()) {
            return it->second;
        }

        // If point is blocked, return nullptr
        if (isBlocked(point)) {
            return nullptr;
        }

        // Create new node, add to mappings, return it
        geom::Node* node = graph.add();
        pt2node[point] = node;
        node2pt[node] = point;

        return node;
    }

    /**
     * Build path between two points using A* pathfinding
     * @param from Starting point
     * @param to Goal point
     * @param exclude Nodes to exclude from pathfinding (default: empty)
     * @return Path from start to goal as vector of points, empty if no path found
     *
     * Algorithm:
     * 1. Look up nodes for from/to points
     * 2. Use graph.aStar for pathfinding
     * 3. Convert node path back to point path
     * 4. Return empty vector if no path
     */
    std::vector<geom::Point> buildPath(const geom::Point& from, const geom::Point& to,
                                       const std::vector<geom::Node*>& exclude = {}) {
        // Look up nodes for from/to points
        auto fromIt = pt2node.find(from);
        auto toIt = pt2node.find(to);

        if (fromIt == pt2node.end() || toIt == pt2node.end()) {
            return {};
        }

        geom::Node* fromNode = fromIt->second;
        geom::Node* toNode = toIt->second;

        // Use graph.aStar for pathfinding
        std::vector<geom::Node*> nodePath = graph.aStar(fromNode, toNode, exclude);

        // Convert node path back to point path
        std::vector<geom::Point> pointPath;
        pointPath.reserve(nodePath.size());

        for (geom::Node* node : nodePath) {
            auto nodeIt = node2pt.find(node);
            if (nodeIt != node2pt.end()) {
                pointPath.push_back(nodeIt->second);
            }
        }

        return pointPath;
    }

private:
    /**
     * Check if a point is blocked (in the blocked list)
     * @param point The point to check
     * @return true if point is blocked, false otherwise
     */
    bool isBlocked(const geom::Point& point) const {
        return std::find_if(blocked.begin(), blocked.end(),
            [&point](const geom::Point& p) {
                return p.x == point.x && p.y == point.y;
            }) != blocked.end();
    }
};

} // namespace building
} // namespace towngenerator
