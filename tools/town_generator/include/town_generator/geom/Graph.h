#pragma once

#include <vector>
#include <map>
#include <algorithm>
#include <cmath>

namespace town_generator {

class Node {
public:
    std::map<Node*, float> links;

    Node() = default;

    void link(Node* node, float price = 1.0f, bool symmetrical = true) {
        links[node] = price;
        if (symmetrical) {
            node->links[this] = price;
        }
    }

    void unlink(Node* node, bool symmetrical = true) {
        links.erase(node);
        if (symmetrical) {
            node->links.erase(this);
        }
    }

    void unlinkAll() {
        for (auto& [node, price] : links) {
            node->links.erase(this);
        }
        links.clear();
    }
};

class Graph {
public:
    std::vector<Node*> nodes;

    Graph() = default;

    ~Graph() {
        for (auto* node : nodes) {
            delete node;
        }
    }

    Node* add(Node* node = nullptr) {
        if (node == nullptr) {
            node = new Node();
        }
        nodes.push_back(node);
        return node;
    }

    void remove(Node* node) {
        node->unlinkAll();
        auto it = std::find(nodes.begin(), nodes.end(), node);
        if (it != nodes.end()) {
            nodes.erase(it);
        }
        delete node;
    }

    std::vector<Node*> aStar(Node* start, Node* goal, const std::vector<Node*>* exclude = nullptr) {
        std::vector<Node*> closedSet;
        if (exclude != nullptr) {
            closedSet = *exclude;
        }

        std::vector<Node*> openSet = {start};
        std::map<Node*, Node*> cameFrom;
        std::map<Node*, float> gScore;
        gScore[start] = 0;

        while (!openSet.empty()) {
            Node* current = openSet.front();
            openSet.erase(openSet.begin());

            if (current == goal) {
                return buildPath(cameFrom, current);
            }

            closedSet.push_back(current);

            float curScore = gScore[current];
            for (auto& [neighbour, linkPrice] : current->links) {
                if (std::find(closedSet.begin(), closedSet.end(), neighbour) != closedSet.end()) {
                    continue;
                }

                float score = curScore + linkPrice;
                bool inOpenSet = std::find(openSet.begin(), openSet.end(), neighbour) != openSet.end();

                if (!inOpenSet) {
                    openSet.push_back(neighbour);
                } else if (gScore.count(neighbour) && score >= gScore[neighbour]) {
                    continue;
                }

                cameFrom[neighbour] = current;
                gScore[neighbour] = score;
            }
        }

        return {};  // No path found
    }

    float calculatePrice(const std::vector<Node*>& path) {
        if (path.size() < 2) {
            return 0;
        }

        float price = 0.0f;
        for (size_t i = 0; i + 1 < path.size(); i++) {
            Node* current = path[i];
            Node* next = path[i + 1];
            if (current->links.count(next)) {
                price += current->links[next];
            } else {
                return std::nan("");
            }
        }
        return price;
    }

private:
    std::vector<Node*> buildPath(std::map<Node*, Node*>& cameFrom, Node* current) {
        std::vector<Node*> path = {current};
        while (cameFrom.count(current)) {
            current = cameFrom[current];
            path.push_back(current);
        }
        return path;
    }
};

} // namespace town_generator
