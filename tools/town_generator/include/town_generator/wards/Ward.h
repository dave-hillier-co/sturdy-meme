#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <vector>
#include <string>

namespace town_generator {

class Patch;  // Forward declaration
class Model;  // Forward declaration

class Ward {
public:
    static constexpr float MAIN_STREET = 2.0f;
    static constexpr float REGULAR_STREET = 1.0f;
    static constexpr float ALLEY = 0.6f;

    Model* model = nullptr;
    Patch* patch = nullptr;
    std::vector<Polygon> geometry;

    Ward() = default;
    Ward(Model* model, Patch* patch);
    virtual ~Ward() = default;

    virtual void createGeometry();
    virtual std::string getLabel() const { return ""; }

    Polygon getCityBlock() const;

    static float rateLocation(Model* model, Patch* patch);

    static std::vector<Polygon> createAlleys(const Polygon& p, float minSq,
                                              float gridChaos, float sizeChaos,
                                              float emptyProb = 0.04f, bool split = true);

    static std::vector<Polygon> createOrthoBuilding(const Polygon& poly, float minBlockSq, float fill);

protected:
    void filterOutskirts();

private:
    static Point findLongestEdge(const Polygon& poly);
};

} // namespace town_generator
