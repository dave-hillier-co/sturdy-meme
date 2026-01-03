#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Model.h"
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace town_generator {
namespace svg {

/**
 * SVGWriter - Outputs city model as SVG
 */
class SVGWriter {
public:
    // Style configuration
    struct Style {
        std::string buildingFill;
        std::string buildingStroke;
        double buildingStrokeWidth;

        std::string streetFill;
        std::string streetStroke;
        double streetStrokeWidth;

        std::string wallStroke;
        double wallStrokeWidth;

        std::string towerFill;
        std::string towerStroke;
        double towerRadius;

        std::string gateFill;

        std::string patchStroke;
        double patchStrokeWidth;

        std::string backgroundColor;

        Style()
            : buildingFill("#d4c4a8")
            , buildingStroke("#8b7355")
            , buildingStrokeWidth(0.5)
            , streetFill("none")
            , streetStroke("#c8b89a")
            , streetStrokeWidth(2.0)
            , wallStroke("#5c4033")
            , wallStrokeWidth(3.0)
            , towerFill("#8b7355")
            , towerStroke("#5c4033")
            , towerRadius(2.0)
            , gateFill("#c8b89a")
            , patchStroke("#e0d5c0")
            , patchStrokeWidth(0.3)
            , backgroundColor("#f5f0e6")
        {}
    };

    static bool write(const building::Model& model, const std::string& filename, const Style& style = Style());
    static std::string generate(const building::Model& model, const Style& style = Style());

    // Equality
    bool operator==(const SVGWriter& other) const { return true; }
    bool operator!=(const SVGWriter& other) const { return false; }

private:
    static std::string polygonToPath(const geom::Polygon& poly);
    static std::string polylineToPath(const std::vector<geom::Point>& points);
};

} // namespace svg
} // namespace town_generator
