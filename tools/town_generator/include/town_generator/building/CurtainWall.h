#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <vector>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class CurtainWall {
public:
    Polygon shape;
    std::vector<bool> segments;
    std::vector<Point> gates;
    std::vector<Point> towers;

    CurtainWall(bool real, Model* model, const std::vector<Patch*>& patches,
                const std::vector<Point>& reserved);

    void buildTowers();
    float getRadius() const;
    bool bordersBy(Patch* p, const Point& v0, const Point& v1) const;
    bool borders(Patch* p) const;

private:
    bool real_;
    std::vector<Patch*> patches_;

    void buildGates(bool real, Model* model, const std::vector<Point>& reserved);
};

} // namespace town_generator
