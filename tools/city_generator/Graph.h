// Graph data structure with A* pathfinding
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// - Graph nodes represent intersection points in the city
// - Edges have weights (typically Euclidean distance)
// - A* pathfinding used to route streets from gates to center
// - Excluded nodes can block path finding (walls, citadel)

#pragma once

#include "Geometry.h"
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <algorithm>
#include <limits>
#include <memory>

namespace city {

// Graph node
class Node {
public:
    std::map<Node*, float> links;  // Connected nodes with edge weights

    // Link to another node with given weight
    void link(Node* other, float weight) {
        links[other] = weight;
        other->links[this] = weight;
    }

    // Unlink from another node
    void unlink(Node* other) {
        links.erase(other);
        other->links.erase(this);
    }

    // Unlink from all connected nodes
    void unlinkAll() {
        for (auto& [node, weight] : links) {
            node->links.erase(this);
        }
        links.clear();
    }
};

// Graph with A* pathfinding
class Graph {
public:
    std::vector<std::unique_ptr<Node>> nodes;

    // Add a new node to the graph
    Node* add() {
        nodes.push_back(std::make_unique<Node>());
        return nodes.back().get();
    }

    // Remove a node from the graph
    void remove(Node* node) {
        node->unlinkAll();
        nodes.erase(
            std::remove_if(nodes.begin(), nodes.end(),
                [node](const std::unique_ptr<Node>& n) { return n.get() == node; }),
            nodes.end());
    }

    // A* pathfinding from start to goal
    // Returns path as list of nodes (empty if no path found)
    // exclude: nodes to avoid in pathfinding
    std::vector<Node*> aStar(Node* start, Node* goal,
                             const std::vector<Node*>& exclude = {}) const {
        if (!start || !goal) return {};

        std::set<Node*> closedSet(exclude.begin(), exclude.end());
        std::vector<Node*> openSet = {start};
        std::map<Node*, Node*> cameFrom;
        std::map<Node*, float> gScore;

        gScore[start] = 0.0f;

        while (!openSet.empty()) {
            // Find node in openSet with lowest gScore
            auto currentIt = std::min_element(openSet.begin(), openSet.end(),
                [&gScore](Node* a, Node* b) {
                    float sa = gScore.count(a) ? gScore[a] : std::numeric_limits<float>::max();
                    float sb = gScore.count(b) ? gScore[b] : std::numeric_limits<float>::max();
                    return sa < sb;
                });

            Node* current = *currentIt;

            if (current == goal) {
                return buildPath(cameFrom, current);
            }

            openSet.erase(currentIt);
            closedSet.insert(current);

            float curScore = gScore.count(current) ? gScore[current] : std::numeric_limits<float>::max();

            for (auto& [neighbour, weight] : current->links) {
                if (closedSet.count(neighbour)) continue;

                float tentativeScore = curScore + weight;

                bool inOpenSet = std::find(openSet.begin(), openSet.end(), neighbour) != openSet.end();

                if (!inOpenSet) {
                    openSet.push_back(neighbour);
                } else if (gScore.count(neighbour) && tentativeScore >= gScore[neighbour]) {
                    continue;
                }

                cameFrom[neighbour] = current;
                gScore[neighbour] = tentativeScore;
            }
        }

        return {};  // No path found
    }

    // Calculate total path cost
    float calculatePathCost(const std::vector<Node*>& path) const {
        if (path.size() < 2) return 0.0f;

        float cost = 0.0f;
        for (size_t i = 0; i < path.size() - 1; i++) {
            Node* current = path[i];
            Node* next = path[i + 1];

            if (current->links.count(next)) {
                cost += current->links.at(next);
            } else {
                return std::numeric_limits<float>::quiet_NaN();
            }
        }
        return cost;
    }

private:
    std::vector<Node*> buildPath(const std::map<Node*, Node*>& cameFrom, Node* current) const {
        std::vector<Node*> path = {current};

        while (cameFrom.count(current)) {
            current = cameFrom.at(current);
            path.push_back(current);
        }

        std::reverse(path.begin(), path.end());
        return path;
    }
};

// Topology: Maps points to graph nodes for street pathfinding
// Semantic rules:
// - Each Voronoi vertex becomes a node
// - Adjacent vertices in patches are linked
// - Blocked points (walls, citadel) are excluded from pathfinding
// - Vertices with same coordinates are merged into same node
class Topology {
public:
    std::map<Vec2*, Node*> pointToNode;
    std::map<Node*, Vec2*> nodeToPoint;
    std::vector<Node*> innerNodes;  // Nodes within city
    std::vector<Node*> outerNodes;  // Nodes outside walls

    Graph graph;

    // Build topology from patch vertices
    // blockedPoints: vertices on walls/citadel that block paths
    // borderShape: wall shape (for categorizing inner/outer nodes)
    void build(const std::vector<std::vector<Vec2>*>& patchShapes,
               const std::vector<bool>& withinCity,
               const std::vector<Vec2>& blockedPoints,
               const Polygon* borderShape) {

        // First pass: create nodes for all unique positions
        // Use position-based lookup to merge vertices with same coords
        std::map<std::pair<int, int>, Node*> posToNode;  // Quantized position -> node

        auto quantize = [](const Vec2& v) -> std::pair<int, int> {
            // Quantize to 0.01 precision to handle floating point errors
            return {static_cast<int>(v.x * 100), static_cast<int>(v.y * 100)};
        };

        auto isBlocked = [&blockedPoints](const Vec2& v) -> bool {
            for (const auto& bp : blockedPoints) {
                if (v == bp) return true;
            }
            return false;
        };

        // Create or find node for a vertex
        auto getOrCreateNode = [&](Vec2* v) -> Node* {
            if (isBlocked(*v)) return nullptr;

            auto key = quantize(*v);
            if (posToNode.count(key)) {
                Node* n = posToNode[key];
                pointToNode[v] = n;  // Also map this pointer to the node
                return n;
            }

            Node* n = graph.add();
            posToNode[key] = n;
            pointToNode[v] = n;
            nodeToPoint[n] = v;
            return n;
        };

        // Build graph from patch edges
        for (size_t patchIdx = 0; patchIdx < patchShapes.size(); patchIdx++) {
            auto* shape = patchShapes[patchIdx];
            bool isWithinCity = withinCity[patchIdx];

            if (shape->empty()) continue;

            Vec2* v1 = &shape->back();
            Node* n1 = getOrCreateNode(v1);

            for (size_t i = 0; i < shape->size(); i++) {
                Vec2* v0 = v1;
                v1 = &(*shape)[i];
                Node* n0 = n1;
                n1 = getOrCreateNode(v1);

                // Categorize nodes as inner/outer (not on wall)
                if (n0 && (!borderShape || !borderShape->contains(*v0))) {
                    auto& list = isWithinCity ? innerNodes : outerNodes;
                    if (std::find(list.begin(), list.end(), n0) == list.end()) {
                        list.push_back(n0);
                    }
                }

                if (n1 && (!borderShape || !borderShape->contains(*v1))) {
                    auto& list = isWithinCity ? innerNodes : outerNodes;
                    if (std::find(list.begin(), list.end(), n1) == list.end()) {
                        list.push_back(n1);
                    }
                }

                // Link adjacent nodes
                if (n0 && n1 && n0 != n1) {
                    n0->link(n1, Vec2::distance(*v0, *v1));
                }
            }
        }
    }

    // Find path between two points
    std::vector<Vec2> buildPath(Vec2* from, Vec2* to,
                                const std::vector<Node*>& exclude = {}) {
        if (!pointToNode.count(from) || !pointToNode.count(to)) {
            return {};
        }

        auto nodePath = graph.aStar(pointToNode[from], pointToNode[to], exclude);

        std::vector<Vec2> result;
        for (Node* n : nodePath) {
            if (nodeToPoint.count(n)) {
                result.push_back(*nodeToPoint[n]);
            }
        }
        return result;
    }
};

} // namespace city
