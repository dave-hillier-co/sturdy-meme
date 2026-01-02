#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Graph.h>
#include <vector>
#include <map>

namespace town_generator {

class Model;  // Forward declaration

class Topology {
public:
    std::map<Point*, Node*> pt2node;
    std::map<Node*, Point*> node2pt;

    std::vector<Node*> inner;
    std::vector<Node*> outer;

    explicit Topology(Model* model);
    ~Topology();

    std::vector<Point> buildPath(Point* from, Point* to, const std::vector<Node*>* exclude = nullptr);

private:
    Model* model;
    Graph graph;
    std::vector<Point*> blocked;

    Node* processPoint(Point* v);
};

} // namespace town_generator
