#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/geom/Voronoi.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Topology.h>
#include <town_generator/building/CurtainWall.h>
#include <vector>
#include <memory>

namespace town_generator {

// Street is an alias for Polygon (a polyline of points)
using Street = Polygon;

class Model {
public:
    static Model* instance;

    // Size categories:
    // Small Town: 6, Large Town: 10, Small City: 15, Large City: 24, Metropolis: 40
    int nPatches;

    std::unique_ptr<Topology> topology;

    std::vector<Patch*> patches;
    std::vector<Patch*> waterbody;
    std::vector<Patch*> inner;  // Patches within walls (or all city wards if no walls)

    Patch* citadel = nullptr;
    Patch* plaza = nullptr;
    Point center;

    CurtainWall* border = nullptr;
    CurtainWall* wall = nullptr;

    float cityRadius = 0;

    // List of all entrances including castle gates
    std::vector<Point> gates;

    // Streets and roads
    std::vector<Street> arteries;  // Combined streets and roads without duplicates
    std::vector<Street> streets;   // Inside walls
    std::vector<Street> roads;     // Outside walls

    Model(int nPatches = -1, int seed = -1);
    ~Model();

    static Polygon findCircumference(const std::vector<Patch*>& wards);

    std::vector<Patch*> patchByVertex(const Point& v);
    Patch* getNeighbour(Patch* patch, const Point& v);
    std::vector<Patch*> getNeighbours(Patch* patch);
    bool isEnclosed(Patch* patch);

    void replacePatches(Patch* old, const std::vector<Patch*>& newPatches);

private:
    bool plazaNeeded;
    bool citadelNeeded;
    bool wallsNeeded;

    // Voronoi and generated points (need to manage memory)
    std::vector<Point*> generatedPoints;

    void build();
    void buildPatches();
    void buildWalls();
    void buildStreets();
    void optimizeJunctions();
    void createWards();
    void buildGeometry();
    void tidyUpRoads();
};

} // namespace town_generator
