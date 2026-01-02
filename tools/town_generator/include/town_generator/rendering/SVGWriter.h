#pragma once

#include <town_generator/geom/Point.h>
#include <town_generator/geom/Polygon.h>
#include <town_generator/rendering/Palette.h>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>

namespace town_generator {

class SVGWriter {
public:
    SVGWriter(float width, float height, float minX, float minY, float maxX, float maxY);

    // Basic shape drawing
    void drawPolygon(const Polygon& poly, uint32_t fill, uint32_t stroke = 0xFFFFFFFF,
                     float strokeWidth = 0);
    void drawPolyline(const Polygon& poly, uint32_t stroke, float strokeWidth,
                      const std::string& lineCap = "round");
    void drawCircle(float cx, float cy, float r, uint32_t fill, uint32_t stroke = 0xFFFFFFFF,
                    float strokeWidth = 0);
    void drawLine(float x1, float y1, float x2, float y2, uint32_t stroke, float strokeWidth,
                  const std::string& lineCap = "butt");

    // Group management
    void beginGroup(const std::string& id = "");
    void endGroup();

    // Get the complete SVG document
    std::string toString() const;

    // Save to file
    bool saveToFile(const std::string& filename) const;

private:
    float width_, height_;
    float minX_, minY_, maxX_, maxY_;
    std::stringstream content_;
    int indentLevel_ = 1;

    std::string indent() const;
    std::string colorToHex(uint32_t color) const;
    std::string pointsToPath(const Polygon& poly, bool closed = true) const;
};

} // namespace town_generator
