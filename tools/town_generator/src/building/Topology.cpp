#include <town_generator/building/Topology.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/CurtainWall.h>
#include <algorithm>
#include <iostream>

namespace town_generator {

Topology::Topology(Model* model) : model(model) {
    // Building a list of all blocked points (shore + walls excluding gates)
    if (model->citadel) {
        for (auto& v : model->citadel->shape.vertices) {
            blocked.push_back(&v);
        }
    }
    if (model->wall) {
        for (auto& v : model->wall->shape.vertices) {
            blocked.push_back(&v);
        }
    }

    // Remove gates from blocked
    for (auto& gate : model->gates) {
        auto it = std::find_if(blocked.begin(), blocked.end(),
            [&gate](Point* p) { return p->equals(gate); });
        if (it != blocked.end()) {
            blocked.erase(it);
        }
    }

    auto& border = model->border->shape;

    for (auto* p : model->patches) {
        bool withinCity = p->withinCity;

        Point* v1 = &p->shape.last();
        Node* n1 = processPoint(v1);

        for (size_t i = 0; i < p->shape.size(); i++) {
            Point* v0 = v1;
            v1 = &p->shape[i];
            Node* n0 = n1;
            n1 = processPoint(v1);

            auto notInBorder = [&border](Point* pt) {
                return border.indexOf(*pt) == -1;
            };

            if (n0 && notInBorder(v0)) {
                auto& list = withinCity ? inner : outer;
                if (std::find(list.begin(), list.end(), n0) == list.end()) {
                    list.push_back(n0);
                }
            }
            if (n1 && notInBorder(v1)) {
                auto& list = withinCity ? inner : outer;
                if (std::find(list.begin(), list.end(), n1) == list.end()) {
                    list.push_back(n1);
                }
            }

            if (n0 && n1) {
                n0->link(n1, v0->distance(*v1));
            }
        }
    }
}

Topology::~Topology() {
    // Graph owns the nodes, no cleanup needed here
}

Node* Topology::processPoint(Point* v) {
    auto it = pt2node.find(v);
    Node* n;

    if (it != pt2node.end()) {
        n = it->second;
    } else {
        n = graph.add();
        pt2node[v] = n;
        node2pt[n] = v;
    }

    // Check if blocked
    for (auto* blockedPt : blocked) {
        if (blockedPt->equals(*v)) {
            return nullptr;
        }
    }

    return n;
}

std::vector<Point> Topology::buildPath(Point* from, Point* to, const std::vector<Node*>* exclude) {
    auto fromIt = pt2node.find(from);
    auto toIt = pt2node.find(to);

    if (fromIt == pt2node.end() || toIt == pt2node.end()) {
        return {};
    }

    auto path = graph.aStar(fromIt->second, toIt->second, exclude);
    if (path.empty()) {
        return {};
    }

    std::vector<Point> result;
    for (auto* n : path) {
        auto it = node2pt.find(n);
        if (it != node2pt.end()) {
            result.push_back(*it->second);
        }
    }
    return result;
}

} // namespace town_generator
