#pragma once

#include "building/Model.h"
#include "building/Patch.h"
#include "building/CurtainWall.h"
#include "wards/Ward.h"
#include "wards/Farm.h"
#include "wards/Park.h"
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

/**
 * SvgExporter - Export city model to SVG format
 *
 * Generates SVG visualization of the generated medieval city
 * with proper layering and coloring matching the original town generator.
 */
class SvgExporter {
public:
    // ====================
    // COLOR SCHEME (faithful to original)
    // ====================

    static constexpr const char* COLOR_BACKGROUND = "#e8dcc8";  // Parchment
    static constexpr const char* COLOR_BUILDINGS = "#b8a080";   // Tan/brown
    static constexpr const char* COLOR_STREETS = "#d0c0a0";     // Light tan
    static constexpr const char* COLOR_WALLS = "#666666";       // Gray
    static constexpr const char* COLOR_GATES = "#333333";       // Dark gray
    static constexpr const char* COLOR_TOWERS = "#444444";      // Tower gray
    static constexpr const char* COLOR_WATER = "#88aacc";       // Blue
    static constexpr const char* COLOR_PARKS = "#a0c080";       // Green
    static constexpr const char* COLOR_FARMS = "#d0e0b0";       // Light green

    // ====================
    // CONSTRUCTOR
    // ====================

    /**
     * Constructor
     * @param model Reference to the city model to export
     */
    explicit SvgExporter(const Model& model)
        : model_(model)
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

        // Write city elements in proper layering order
        // 1. Farm fields (background layer)
        writeFarmFields(file);

        // 2. Streets/arteries
        writeStreets(file);

        // 3. Park areas
        writeParks(file);

        // 4. Buildings/city blocks
        writeBuildings(file);

        // 5. Walls (on top of buildings)
        if (model_.wallsNeeded && model_.wall) {
            writeWalls(file);
            writeTowers(file);
            writeGates(file);
        }

        // 6. Ward labels (top layer)
        writeLabels(file);

        // Close SVG
        file << "</svg>\n";

        file.close();
        return true;
    }

private:
    const Model& model_;
    float minX_, minY_, maxX_, maxY_;

    // ====================
    // HELPER METHODS - Bounds calculation
    // ====================

    /**
     * Calculate bounding box of entire city
     */
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
    // HELPER METHODS - SVG element writers
    // ====================

    /**
     * Write background rectangle
     */
    void writeBackground(std::ofstream& file, float padding) {
        float width = maxX_ - minX_ + (padding * 2);
        float height = maxY_ - minY_ + (padding * 2);

        file << "  <rect x=\"" << (minX_ - padding) << "\" y=\"" << (minY_ - padding) << "\" ";
        file << "width=\"" << width << "\" height=\"" << height << "\" ";
        file << "fill=\"" << COLOR_BACKGROUND << "\" />\n";
    }

    /**
     * Write farm field polygons (entire patch area for Farm wards)
     */
    void writeFarmFields(std::ofstream& file) {
        file << "  <!-- Farm Fields -->\n";

        for (const auto& patch : model_.patches) {
            if (patch.ward != nullptr) {
                const char* label = patch.ward->getLabel();
                if (label != nullptr && std::string(label) == "Farm") {
                    // Draw the entire patch shape as farm field
                    writePolygon(file, patch.shape, COLOR_FARMS, "1", "none");
                }
            }
        }
    }

    /**
     * Write street/artery paths
     */
    void writeStreets(std::ofstream& file) {
        file << "  <!-- Streets -->\n";

        for (const auto& artery : model_.arteries) {
            if (artery.size() < 2) continue;

            file << "  <polyline points=\"";
            for (size_t i = 0; i < artery.size(); ++i) {
                if (i > 0) file << " ";
                file << artery[i].x << "," << artery[i].y;
            }
            file << "\" stroke=\"" << COLOR_STREETS << "\" ";
            file << "stroke-width=\"2\" fill=\"none\" ";
            file << "stroke-linecap=\"round\" stroke-linejoin=\"round\" />\n";
        }
    }

    /**
     * Write park areas (patch shape for Park wards)
     */
    void writeParks(std::ofstream& file) {
        file << "  <!-- Parks -->\n";

        for (const auto& patch : model_.patches) {
            if (patch.ward != nullptr) {
                const char* label = patch.ward->getLabel();
                if (label != nullptr && std::string(label) == "Park") {
                    // Draw the entire patch shape as park
                    writePolygon(file, patch.shape, COLOR_PARKS, "1", "none");
                }
            }
        }
    }

    /**
     * Write building footprints from ward geometry
     */
    void writeBuildings(std::ofstream& file) {
        file << "  <!-- Buildings -->\n";

        for (const auto& patch : model_.patches) {
            if (patch.ward != nullptr) {
                const char* label = patch.ward->getLabel();

                // Skip Farm and Park wards - they're already drawn
                if (label != nullptr) {
                    std::string wardType(label);
                    if (wardType == "Farm" || wardType == "Park") {
                        continue;
                    }
                }

                // Draw all building polygons from ward geometry
                for (const auto& building : patch.ward->geometry) {
                    writePolygon(file, building, COLOR_BUILDINGS, "0.5", "#000000");
                }
            }
        }
    }

    /**
     * Write city walls
     */
    void writeWalls(std::ofstream& file) {
        if (!model_.wall) return;

        file << "  <!-- Walls -->\n";

        const auto& wall = *model_.wall;
        const auto& vertices = wall.shape.vertices;

        // Draw wall segments (skip gates)
        for (size_t i = 0; i < vertices.size(); ++i) {
            size_t j = (i + 1) % vertices.size();

            // Check if this segment is a wall (not a gate)
            if (i < wall.segments.size() && wall.segments[i]) {
                file << "  <line x1=\"" << vertices[i].x << "\" y1=\"" << vertices[i].y << "\" ";
                file << "x2=\"" << vertices[j].x << "\" y2=\"" << vertices[j].y << "\" ";
                file << "stroke=\"" << COLOR_WALLS << "\" stroke-width=\"3\" ";
                file << "stroke-linecap=\"round\" />\n";
            }
        }
    }

    /**
     * Write wall towers
     */
    void writeTowers(std::ofstream& file) {
        if (!model_.wall) return;

        file << "  <!-- Towers -->\n";

        for (const auto& tower : model_.wall->towers) {
            file << "  <circle cx=\"" << tower.x << "\" cy=\"" << tower.y << "\" ";
            file << "r=\"2\" fill=\"" << COLOR_TOWERS << "\" ";
            file << "stroke=\"#000000\" stroke-width=\"0.5\" />\n";
        }
    }

    /**
     * Write wall gates
     */
    void writeGates(std::ofstream& file) {
        if (!model_.wall) return;

        file << "  <!-- Gates -->\n";

        for (const auto& gate : model_.wall->gates) {
            file << "  <circle cx=\"" << gate.x << "\" cy=\"" << gate.y << "\" ";
            file << "r=\"2.5\" fill=\"" << COLOR_GATES << "\" ";
            file << "stroke=\"#000000\" stroke-width=\"0.5\" />\n";
        }
    }

    /**
     * Write ward labels
     */
    void writeLabels(std::ofstream& file) {
        file << "  <!-- Ward Labels -->\n";

        for (const auto& patch : model_.patches) {
            if (patch.ward != nullptr) {
                const char* label = patch.ward->getLabel();
                if (label != nullptr) {
                    Point centroid = patch.shape.centroid();

                    file << "  <text x=\"" << centroid.x << "\" y=\"" << centroid.y << "\" ";
                    file << "font-family=\"Arial, sans-serif\" font-size=\"8\" ";
                    file << "fill=\"#333333\" opacity=\"0.6\" ";
                    file << "text-anchor=\"middle\" dominant-baseline=\"middle\">";
                    file << label << "</text>\n";
                }
            }
        }
    }

    /**
     * Write a polygon element
     * @param file Output stream
     * @param poly Polygon to draw
     * @param fill Fill color
     * @param strokeWidth Stroke width
     * @param stroke Stroke color
     */
    void writePolygon(std::ofstream& file, const Polygon& poly,
                      const char* fill, const char* strokeWidth, const char* stroke) {
        if (poly.vertices.empty()) return;

        file << "  <polygon points=\"";
        for (size_t i = 0; i < poly.vertices.size(); ++i) {
            if (i > 0) file << " ";
            file << poly.vertices[i].x << "," << poly.vertices[i].y;
        }
        file << "\" fill=\"" << fill << "\" ";
        file << "stroke=\"" << stroke << "\" stroke-width=\"" << strokeWidth << "\" />\n";
    }
};

} // namespace towngenerator
