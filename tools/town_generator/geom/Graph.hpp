/**
 * Ported from: Source/com/watabou/geom/Graph.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>

namespace town {

class Node;

/**
 * Node class - represents a node in the graph with weighted links to other nodes.
 * Uses reference semantics (shared_ptr) for object identity comparisons.
 */
class Node {
public:
    // Links to other nodes with associated costs/weights
    std::unordered_map<Node*, float> links;

    Node() = default;

    /**
     * Creates a link from this node to another node.
     * @param node The target node to link to
     * @param price The cost/weight of this edge (default 1.0)
     * @param symmetrical If true, also creates the reverse link (default true)
     */
    void link(Node* node, float price = 1.0f, bool symmetrical = true) {
        links[node] = price;
        if (symmetrical) {
            node->links[this] = price;
        }
    }

    /**
     * Removes the link from this node to another node.
     * @param node The target node to unlink
     * @param symmetrical If true, also removes the reverse link (default true)
     */
    void unlink(Node* node, bool symmetrical = true) {
        links.erase(node);
        if (symmetrical) {
            node->links.erase(this);
        }
    }

    /**
     * Removes all links from this node (and symmetric reverse links).
     */
    void unlinkAll() {
        // Collect keys first to avoid iterator invalidation
        std::vector<Node*> nodes;
        for (const auto& pair : links) {
            nodes.push_back(pair.first);
        }
        for (Node* node : nodes) {
            unlink(node);
        }
    }
};

/**
 * Graph class - manages a collection of nodes and provides pathfinding.
 */
class Graph {
public:
    std::vector<std::shared_ptr<Node>> nodes;

    Graph() = default;

    /**
     * Adds a node to the graph.
     * @param node The node to add (if null, creates a new node)
     * @return The added node
     */
    std::shared_ptr<Node> add(std::shared_ptr<Node> node = nullptr) {
        if (!node) {
            node = std::make_shared<Node>();
        }
        nodes.push_back(node);
        return node;
    }

    /**
     * Removes a node from the graph, unlinking it from all connected nodes.
     * @param node The node to remove
     */
    void remove(std::shared_ptr<Node> node) {
        node->unlinkAll();
        auto it = std::find(nodes.begin(), nodes.end(), node);
        if (it != nodes.end()) {
            nodes.erase(it);
        }
    }

    /**
     * A* pathfinding algorithm to find a path from start to goal.
     * @param start The starting node
     * @param goal The destination node
     * @param exclude Optional list of nodes to exclude from the search
     * @return Vector of nodes representing the path, or nullptr if no path exists
     */
    std::unique_ptr<std::vector<std::shared_ptr<Node>>> aStar(
            std::shared_ptr<Node> start,
            std::shared_ptr<Node> goal,
            const std::vector<std::shared_ptr<Node>>* exclude = nullptr) {

        std::vector<std::shared_ptr<Node>> closedSet;
        if (exclude) {
            closedSet = *exclude;
        }

        std::vector<std::shared_ptr<Node>> openSet;
        openSet.push_back(start);

        std::unordered_map<Node*, std::shared_ptr<Node>> cameFrom;
        std::unordered_map<Node*, float> gScore;
        gScore[start.get()] = 0;

        while (!openSet.empty()) {
            auto current = openSet.front();
            openSet.erase(openSet.begin());

            if (current == goal) {
                return buildPath(cameFrom, current);
            }

            closedSet.push_back(current);

            float curScore = gScore[current.get()];

            for (const auto& pair : current->links) {
                Node* neighbourRaw = pair.first;

                // Find the shared_ptr for this neighbour
                std::shared_ptr<Node> neighbour;
                for (const auto& n : nodes) {
                    if (n.get() == neighbourRaw) {
                        neighbour = n;
                        break;
                    }
                }
                if (!neighbour) continue;

                // Skip if in closed set
                bool inClosed = false;
                for (const auto& c : closedSet) {
                    if (c.get() == neighbourRaw) {
                        inClosed = true;
                        break;
                    }
                }
                if (inClosed) continue;

                float score = curScore + pair.second;

                bool inOpenSet = false;
                for (const auto& o : openSet) {
                    if (o.get() == neighbourRaw) {
                        inOpenSet = true;
                        break;
                    }
                }

                if (!inOpenSet) {
                    openSet.push_back(neighbour);
                } else if (gScore.count(neighbourRaw) && score >= gScore[neighbourRaw]) {
                    continue;
                }

                cameFrom[neighbourRaw] = current;
                gScore[neighbourRaw] = score;
            }
        }

        return nullptr;
    }

    /**
     * Calculates the total cost of traversing a path.
     * @param path The path to calculate price for
     * @return The total cost, or NaN if path is invalid
     */
    float calculatePrice(const std::vector<std::shared_ptr<Node>>& path) {
        if (path.size() < 2) {
            return 0.0f;
        }

        float price = 0.0f;
        auto current = path[0];

        for (size_t i = 0; i < path.size() - 1; ++i) {
            auto next = path[i + 1];
            auto it = current->links.find(next.get());
            if (it != current->links.end()) {
                price += it->second;
            } else {
                return std::numeric_limits<float>::quiet_NaN();
            }
            current = next;
        }
        return price;
    }

private:
    /**
     * Reconstructs the path from the cameFrom map.
     * @param cameFrom Map of node -> predecessor
     * @param current The goal node
     * @return Vector of nodes representing the path
     */
    std::unique_ptr<std::vector<std::shared_ptr<Node>>> buildPath(
            std::unordered_map<Node*, std::shared_ptr<Node>>& cameFrom,
            std::shared_ptr<Node> current) {

        auto path = std::make_unique<std::vector<std::shared_ptr<Node>>>();
        path->push_back(current);

        while (cameFrom.count(current.get())) {
            current = cameFrom[current.get()];
            path->push_back(current);
        }

        return path;
    }
};

} // namespace town
