#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/rendering/Palette.h>
#include <town_generator/rendering/Brush.h>
#include <town_generator/rendering/SVGWriter.h>
#include <string>
#include <vector>

namespace town_generator {

class Model;
class CurtainWall;

class CityMap {
public:
    static Palette palette;

    explicit CityMap(Model* model);

    // Render the entire city to SVG
    std::string renderToSVG() const;

    // Save SVG to file
    bool saveToFile(const std::string& filename) const;

private:
    Model* model_;
    float minX_, minY_, maxX_, maxY_;

    void calculateBounds();

    void drawRoad(SVGWriter& svg, const Polygon& road) const;
    void drawWall(SVGWriter& svg, CurtainWall* wall, bool large) const;
    void drawTower(SVGWriter& svg, const Point& p, float r) const;
    void drawGate(SVGWriter& svg, const Polygon& wall, const Point& gate) const;
    void drawBuilding(SVGWriter& svg, const std::vector<Polygon>& blocks,
                      uint32_t fill, uint32_t line, float thickness) const;
};

} // namespace town_generator
