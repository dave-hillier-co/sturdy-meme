#pragma once

#include "building/Model.h"
#include "building/Patch.h"
#include "building/CurtainWall.h"
#include "wards/Ward.h"
#include "wards/Castle.h"
#include "wards/Cathedral.h"
#include "wards/Farm.h"
#include "wards/Park.h"
#include "wards/Market.h"
#include "mapping/Palette.h"
#include "mapping/Brush.h"
#include "geom/Point.h"
#include "geom/Polygon.h"
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cmath>

namespace towngenerator {

using geom::Point;
using geom::Polygon;
using building::Model;
using building::Patch;
using building::CurtainWall;
using wards::Ward;
using mapping::Palette;
using mapping::Brush;

/**
 * SvgExporter - Export city model to SVG format
 * Ported from Haxe CityMap.hx
 *
 * Renders the city using the same layering and styling as the original
 * TownGeneratorOS CityMap class.
 */
class SvgExporter {
public:
    // ====================
    // CONSTRUCTOR
    // ====================

    /**
     * Constructor
     * @param model Reference to the city model to export
     * @param palette Color palette to use (default: Palette::DEFAULT())
     */
    explicit SvgExporter(const Model& model, Palette palette = Palette::DEFAULT())
        : model_(model)
        , palette_(palette)
        , brush_(palette_)
        , minX_(0.0f)
        , minY_(0.0f)
        , maxX_(0.0f)
        , maxY_(0.0f)
    {
        calculateBounds();
    }

    // ====================
    // PUBLIC METHODS
    // ====================

    /**
     * Export city to SVG file
     * @param filename Path to output SVG file
     * @return true if successful, false on error
     */
    bool exportToFile(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        // Calculate dimensions with padding
        const float padding = 20.0f;
        float width = maxX_ - minX_ + (padding * 2);
        float height = maxY_ - minY_ + (padding * 2);

        // Write SVG header
        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
        file << "width=\"" << static_cast<int>(width) << "\" ";
        file << "height=\"" << static_cast<int>(height) << "\" ";
        file << "viewBox=\"" << (minX_ - padding) << " " << (minY_ - padding) << " ";
        file << width << " " << height << "\">\n";

        // Write background
        writeBackground(file, padding);

        // Rendering order matches CityMap.new():
        // 1. Roads (main streets)
        writeRoads(file);

        // 2. Patches (buildings by ward type)
        writePatches(file);

        // 3. Walls (if enabled)
        if (model_.wall) {
            writeWall(file, *model_.wall, false);
        }

        // 4. Citadel walls (if exists)
        if (model_.citadel && model_.citadel->ward) {
            auto* castle = dynamic_cast<wards::Castle*>(model_.citadel->ward);
            if (castle && castle->wall) {
                writeWall(file, *castle->wall, true);
            }
        }

        // Close SVG
        file << "</svg>\n";

        file.close();
        return true;
    }

private:
    const Model& model_;
    Palette palette_;
    Brush brush_;
    float minX_, minY_, maxX_, maxY_;

    // ====================
    // HELPER METHODS - Bounds calculation
    // ====================

    void calculateBounds() {
        bool first = true;

        for (const auto& patch : model_.patches) {
            for (const auto& vertex : patch.shape.vertices) {
                if (first) {
                    minX_ = maxX_ = vertex.x;
                    minY_ = maxY_ = vertex.y;
                    first = false;
                } else {
                    minX_ = std::min(minX_, vertex.x);
                    minY_ = std::min(minY_, vertex.y);
                    maxX_ = std::max(maxX_, vertex.x);
                    maxY_ = std::max(maxY_, vertex.y);
                }
            }
        }
    }

    // ====================
    // SVG ELEMENT WRITERS
    // ====================

    void writeBackground(std::ofstream& file, float padding) {
        float width = maxX_ - minX_ + (padding * 2);
        float height = maxY_ - minY_ + (padding * 2);

        file << "  <rect x=\"" << (minX_ - padding) << "\" y=\"" << (minY_ - padding) << "\" ";
        file << "width=\"" << width << "\" height=\"" << height << "\" ";
        file << "fill=\"" << Brush::colorToSvg(palette_.paper) << "\" />\n";
    }

    /**
     * Draw roads (main streets)
     * Matches CityMap.drawRoad()
     */
    void writeRoads(std::ofstream& file) {
        file << "  <!-- Roads -->\n";
        file << "  <g id=\"roads\">\n";

        for (const auto& road : model_.arteries) {
            if (road.size() < 2) continue;

            // Outer stroke (medium color, wider)
            float outerWidth = Ward::MAIN_STREET + Brush::NORMAL_STROKE;
            file << "    <polyline points=\"";
            for (size_t i = 0; i < road.size(); ++i) {
                if (i > 0) file << " ";
                file << road[i].x << "," << road[i].y;
            }
            file << "\" stroke=\"" << Brush::colorToSvg(palette_.medium) << "\" ";
            file << "stroke-width=\"" << outerWidth << "\" fill=\"none\" ";
            file << "stroke-linecap=\"butt\" stroke-linejoin=\"round\" />\n";

            // Inner stroke (paper color, narrower)
            float innerWidth = Ward::MAIN_STREET - Brush::NORMAL_STROKE;
            file << "    <polyline points=\"";
            for (size_t i = 0; i < road.size(); ++i) {
                if (i > 0) file << " ";
                file << road[i].x << "," << road[i].y;
            }
            file << "\" stroke=\"" << Brush::colorToSvg(palette_.paper) << "\" ";
            file << "stroke-width=\"" << innerWidth << "\" fill=\"none\" ";
            file << "stroke-linecap=\"butt\" stroke-linejoin=\"round\" />\n";
        }

        file << "  </g>\n";
    }

    /**
     * Draw patches by ward type
     * Matches CityMap.new() switch statement
     */
    void writePatches(std::ofstream& file) {
        file << "  <!-- Patches -->\n";
        file << "  <g id=\"patches\">\n";

        for (const auto& patch : model_.patches) {
            if (!patch.ward) continue;

            const char* label = patch.ward->getLabel();
            if (!label) continue;

            std::string wardType(label);

            if (wardType == "Castle") {
                // Castle: thicker stroke
                drawBuilding(file, patch.ward->geometry, palette_.light, palette_.dark,
                            Brush::NORMAL_STROKE * 2);
            }
            else if (wardType == "Cathedral") {
                // Cathedral: normal stroke
                drawBuilding(file, patch.ward->geometry, palette_.light, palette_.dark,
                            Brush::NORMAL_STROKE);
            }
            else if (wardType == "Park") {
                // Park: medium fill, no stroke
                brush_.setColor(palette_.medium, -1, 0);
                for (const auto& grove : patch.ward->geometry) {
                    writePolygon(file, grove, palette_.medium, 0, 0);
                }
            }
            else if (wardType == "Market" || wardType == "Craftsmen" ||
                     wardType == "Merchant" || wardType == "Gate" ||
                     wardType == "Slum" || wardType == "Administration" ||
                     wardType == "Military" || wardType == "Patriciate" ||
                     wardType == "Farm") {
                // Standard wards: light fill with dark stroke
                for (const auto& building : patch.ward->geometry) {
                    writePolygon(file, building, palette_.light, palette_.dark,
                                Brush::NORMAL_STROKE);
                }
            }
        }

        file << "  </g>\n";
    }

    /**
     * Draw a building with outline effect
     * Matches CityMap.drawBuilding()
     */
    void drawBuilding(std::ofstream& file, const std::vector<Polygon>& blocks,
                      uint32_t fill, uint32_t line, float thickness) {
        // First pass: outer stroke
        for (const auto& block : blocks) {
            writePolygon(file, block, 0, line, thickness * 2);
        }

        // Second pass: fill only
        for (const auto& block : blocks) {
            writePolygon(file, block, fill, 0, 0);
        }
    }

    /**
     * Draw wall with towers and gates
     * Matches CityMap.drawWall()
     */
    void writeWall(std::ofstream& file, const CurtainWall& wall, bool large) {
        file << "  <!-- Wall -->\n";
        file << "  <g id=\"wall\">\n";

        // Draw wall outline
        if (!wall.shape.vertices.empty()) {
            file << "    <polygon points=\"";
            for (size_t i = 0; i < wall.shape.vertices.size(); ++i) {
                if (i > 0) file << " ";
                file << wall.shape.vertices[i].x << "," << wall.shape.vertices[i].y;
            }
            file << "\" fill=\"none\" stroke=\"" << Brush::colorToSvg(palette_.dark) << "\" ";
            file << "stroke-width=\"" << Brush::THICK_STROKE << "\" />\n";
        }

        // Draw gates
        for (const auto& gate : wall.gates) {
            drawGate(file, wall.shape, gate);
        }

        // Draw towers
        float towerRadius = Brush::THICK_STROKE * (large ? 1.5f : 1.0f);
        for (const auto& tower : wall.towers) {
            drawTower(file, tower, towerRadius);
        }

        file << "  </g>\n";
    }

    /**
     * Draw a tower
     * Matches CityMap.drawTower()
     */
    void drawTower(std::ofstream& file, const Point& p, float r) {
        file << "    <circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" ";
        file << "r=\"" << r << "\" fill=\"" << Brush::colorToSvg(palette_.dark) << "\" />\n";
    }

    /**
     * Draw a gate
     * Matches CityMap.drawGate()
     */
    void drawGate(std::ofstream& file, const Polygon& wall, const Point& gate) {
        // Find direction along wall at gate position
        Point dir = findWallDirection(wall, gate);
        dir = dir.normalize(Brush::THICK_STROKE * 1.5f);

        Point start = gate - dir;
        Point end = gate + dir;

        file << "    <line x1=\"" << start.x << "\" y1=\"" << start.y << "\" ";
        file << "x2=\"" << end.x << "\" y2=\"" << end.y << "\" ";
        file << "stroke=\"" << Brush::colorToSvg(palette_.dark) << "\" ";
        file << "stroke-width=\"" << (Brush::THICK_STROKE * 2) << "\" ";
        file << "stroke-linecap=\"butt\" />\n";
    }

    /**
     * Find wall direction at a gate point
     * Approximates wall.next(gate).subtract(wall.prev(gate))
     */
    Point findWallDirection(const Polygon& wall, const Point& gate) {
        if (wall.vertices.size() < 2) return Point(1, 0);

        // Find closest vertex
        size_t closest = 0;
        float minDist = Point::distance(wall.vertices[0], gate);
        for (size_t i = 1; i < wall.vertices.size(); ++i) {
            float d = Point::distance(wall.vertices[i], gate);
            if (d < minDist) {
                minDist = d;
                closest = i;
            }
        }

        size_t prev = (closest + wall.vertices.size() - 1) % wall.vertices.size();
        size_t next = (closest + 1) % wall.vertices.size();

        return wall.vertices[next] - wall.vertices[prev];
    }

    /**
     * Write a polygon element with fill and stroke
     */
    void writePolygon(std::ofstream& file, const Polygon& poly,
                      uint32_t fill, uint32_t stroke, float strokeWidth) {
        if (poly.vertices.empty()) return;

        file << "    <polygon points=\"";
        for (size_t i = 0; i < poly.vertices.size(); ++i) {
            if (i > 0) file << " ";
            file << poly.vertices[i].x << "," << poly.vertices[i].y;
        }
        file << "\"";

        if (fill != 0) {
            file << " fill=\"" << Brush::colorToSvg(fill) << "\"";
        } else {
            file << " fill=\"none\"";
        }

        if (strokeWidth > 0 && stroke != 0) {
            file << " stroke=\"" << Brush::colorToSvg(stroke) << "\"";
            file << " stroke-width=\"" << strokeWidth << "\"";
        }

        file << " />\n";
    }
};

} // namespace towngenerator
