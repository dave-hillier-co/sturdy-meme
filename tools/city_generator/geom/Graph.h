#pragma once

#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <limits>

namespace towngenerator {
namespace geom {

// Forward declaration
class Node;

/**
 * Node in a weighted graph
 * Ported from Haxe Node class
 */
class Node {
public:
    // Weighted edges to other nodes (neighbor -> weight)
    std::map<Node*, float> links;

    /**
     * Create edge to another node
     * @param other The node to link to
     * @param weight Edge weight (cost)
     * @param symmetric If true, creates bidirectional edge (default: true)
     */
    void link(Node* other, float weight, bool symmetric = true) {
        if (other == nullptr) return;

        links[other] = weight;

        if (symmetric) {
            other->links[this] = weight;
        }
    }

    /**
     * Remove edge to another node (bidirectional)
     * @param other The node to unlink from
     */
    void unlink(Node* other) {
        if (other == nullptr) return;

        links.erase(other);
        other->links.erase(this);
    }

    /**
     * Remove all edges from this node
     */
    void unlinkAll() {
        // Copy links to avoid iterator invalidation
        auto linksCopy = links;

        for (auto& pair : linksCopy) {
            Node* neighbor = pair.first;
            // Remove this node from neighbor's links
            neighbor->links.erase(this);
        }

        links.clear();
    }
};

/**
 * Weighted graph with A* pathfinding
 * Ported from Haxe Graph class
 */
class Graph {
public:
    // All nodes in the graph
    std::vector<std::unique_ptr<Node>> nodes;

    /**
     * Create and add a new node to the graph
     * @return Pointer to the newly created node
     */
    Node* add() {
        auto node = std::make_unique<Node>();
        Node* nodePtr = node.get();
        nodes.push_back(std::move(node));
        return nodePtr;
    }

    /**
     * Remove a node from the graph and all its links
     * @param node The node to remove
     */
    void remove(Node* node) {
        if (node == nullptr) return;

        // Unlink from all neighbors
        node->unlinkAll();

        // Remove from nodes vector
        nodes.erase(
            std::remove_if(nodes.begin(), nodes.end(),
                [node](const std::unique_ptr<Node>& n) {
                    return n.get() == node;
                }),
            nodes.end()
        );
    }

    /**
     * A* pathfinding algorithm
     * @param start Starting node
     * @param goal Goal node
     * @param exclude Nodes to exclude from pathfinding (default: empty)
     * @return Path from start to goal as vector of nodes, empty if no path found
     */
    std::vector<Node*> aStar(Node* start, Node* goal, const std::vector<Node*>& exclude = {}) {
        if (start == nullptr || goal == nullptr) {
            return {};
        }

        if (start == goal) {
            return {start};
        }

        // Convert exclude vector to set for faster lookup
        std::unordered_set<Node*> excludeSet(exclude.begin(), exclude.end());

        // Check if start or goal are excluded
        if (excludeSet.count(start) || excludeSet.count(goal)) {
            return {};
        }

        // Open set (priority queue ordered by f-score)
        auto compareFScore = [](const std::pair<float, Node*>& a, const std::pair<float, Node*>& b) {
            return a.first > b.first; // Min-heap
        };
        std::priority_queue<std::pair<float, Node*>,
                          std::vector<std::pair<float, Node*>>,
                          decltype(compareFScore)> openSet(compareFScore);

        // Track which nodes are in open set
        std::unordered_set<Node*> openSetNodes;

        // G-scores (cost from start)
        std::unordered_map<Node*, float> gScore;

        // F-scores (g-score + heuristic)
        std::unordered_map<Node*, float> fScore;

        // Came from (for path reconstruction)
        std::unordered_map<Node*, Node*> cameFrom;

        // Initialize
        gScore[start] = 0.0f;
        fScore[start] = heuristic(start, goal);
        openSet.push({fScore[start], start});
        openSetNodes.insert(start);

        while (!openSet.empty()) {
            Node* current = openSet.top().second;
            openSet.pop();
            openSetNodes.erase(current);

            // Goal reached
            if (current == goal) {
                return buildPath(cameFrom, current);
            }

            // Explore neighbors
            for (const auto& pair : current->links) {
                Node* neighbor = pair.first;
                float weight = pair.second;

                // Skip excluded nodes
                if (excludeSet.count(neighbor)) {
                    continue;
                }

                float tentativeGScore = gScore[current] + weight;

                // If this is a better path to neighbor
                if (gScore.find(neighbor) == gScore.end() || tentativeGScore < gScore[neighbor]) {
                    // Update path
                    cameFrom[neighbor] = current;
                    gScore[neighbor] = tentativeGScore;
                    fScore[neighbor] = tentativeGScore + heuristic(neighbor, goal);

                    // Add to open set if not already there
                    if (openSetNodes.find(neighbor) == openSetNodes.end()) {
                        openSet.push({fScore[neighbor], neighbor});
                        openSetNodes.insert(neighbor);
                    }
                }
            }
        }

        // No path found
        return {};
    }

    /**
     * Calculate total cost of a path
     * @param path Vector of nodes forming a path
     * @return Sum of edge weights, or NaN if path is invalid
     */
    float calculatePrice(const std::vector<Node*>& path) const {
        if (path.empty()) {
            return std::numeric_limits<float>::quiet_NaN();
        }

        if (path.size() == 1) {
            return 0.0f;
        }

        float totalCost = 0.0f;

        for (size_t i = 0; i < path.size() - 1; ++i) {
            Node* current = path[i];
            Node* next = path[i + 1];

            // Check if edge exists
            auto it = current->links.find(next);
            if (it == current->links.end()) {
                // Invalid path - edge doesn't exist
                return std::numeric_limits<float>::quiet_NaN();
            }

            totalCost += it->second;
        }

        return totalCost;
    }

    /**
     * Reconstruct path from A* came-from map
     * @param cameFrom Map of node -> previous node in path
     * @param current End node
     * @return Path from start to current as vector of nodes
     */
    static std::vector<Node*> buildPath(const std::unordered_map<Node*, Node*>& cameFrom, Node* current) {
        std::vector<Node*> path = {current};

        while (cameFrom.find(current) != cameFrom.end()) {
            current = cameFrom.at(current);
            path.push_back(current);
        }

        // Reverse to get path from start to end
        std::reverse(path.begin(), path.end());

        return path;
    }

private:
    /**
     * Heuristic function for A* (optimistic estimate of remaining cost)
     * Uses number of remaining edges as estimate (assumes minimum weight of 1)
     * In practice, this returns 0 to make A* behave like Dijkstra's algorithm,
     * which is appropriate when we don't have spatial coordinates for nodes
     * @param from Current node
     * @param to Goal node
     * @return Heuristic cost estimate
     */
    static float heuristic(Node* from, Node* to) {
        // Since we don't have spatial coordinates, we use 0 heuristic
        // This makes A* equivalent to Dijkstra's algorithm
        // The algorithm will still find the optimal path
        return 0.0f;
    }
};

} // namespace geom
} // namespace towngenerator
