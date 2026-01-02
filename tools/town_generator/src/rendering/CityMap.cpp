#include <town_generator/rendering/CityMap.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/CurtainWall.h>
#include <town_generator/building/Patch.h>
#include <town_generator/wards/Ward.h>
#include <town_generator/wards/Castle.h>
#include <town_generator/wards/Cathedral.h>
#include <town_generator/wards/Market.h>
#include <town_generator/wards/CraftsmenWard.h>
#include <town_generator/wards/MerchantWard.h>
#include <town_generator/wards/PatriciateWard.h>
#include <town_generator/wards/CommonWard.h>
#include <town_generator/wards/AdministrationWard.h>
#include <town_generator/wards/MilitaryWard.h>
#include <town_generator/wards/GateWard.h>
#include <town_generator/wards/Slum.h>
#include <town_generator/wards/Farm.h>
#include <town_generator/wards/Park.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace town_generator {

Palette CityMap::palette = Palette::DEFAULT();

CityMap::CityMap(Model* model)
    : model_(model)
    , minX_(std::numeric_limits<float>::max())
    , minY_(std::numeric_limits<float>::max())
    , maxX_(std::numeric_limits<float>::lowest())
    , maxY_(std::numeric_limits<float>::lowest()) {
    calculateBounds();
}

void CityMap::calculateBounds() {
    // Calculate bounds from all patches
    for (auto* patch : model_->patches) {
        for (const auto& v : patch->shape.vertices) {
            minX_ = std::min(minX_, v.x);
            minY_ = std::min(minY_, v.y);
            maxX_ = std::max(maxX_, v.x);
            maxY_ = std::max(maxY_, v.y);
        }
    }

    // Add some padding
    float padding = (maxX_ - minX_) * 0.05f;
    minX_ -= padding;
    minY_ -= padding;
    maxX_ += padding;
    maxY_ += padding;
}

std::string CityMap::renderToSVG() const {
    float width = maxX_ - minX_;
    float height = maxY_ - minY_;

    // Scale to reasonable SVG size (e.g., 800px wide)
    float scale = 800.0f / width;
    float svgWidth = width * scale;
    float svgHeight = height * scale;

    SVGWriter svg(svgWidth, svgHeight, minX_, minY_, maxX_, maxY_);

    // Background
    Polygon background;
    background.vertices = {
        Point(minX_, minY_),
        Point(maxX_, minY_),
        Point(maxX_, maxY_),
        Point(minX_, maxY_)
    };
    svg.drawPolygon(background, palette.paper);

    // Draw roads first (below buildings)
    svg.beginGroup("roads");
    for (const auto& road : model_->roads) {
        drawRoad(svg, road);
    }
    svg.endGroup();

    // Draw patches/buildings
    svg.beginGroup("buildings");
    for (auto* patch : model_->patches) {
        if (!patch->ward) continue;

        Ward* ward = patch->ward;

        // Check ward type and draw accordingly
        if (dynamic_cast<Castle*>(ward)) {
            drawBuilding(svg, ward->geometry, palette.light, palette.dark,
                         Brush::NORMAL_STROKE * 2);
        } else if (dynamic_cast<Cathedral*>(ward)) {
            drawBuilding(svg, ward->geometry, palette.light, palette.dark,
                         Brush::NORMAL_STROKE);
        } else if (dynamic_cast<Park*>(ward)) {
            // Parks use medium color without stroke
            for (const auto& grove : ward->geometry) {
                svg.drawPolygon(grove, palette.medium);
            }
        } else if (dynamic_cast<Market*>(ward) ||
                   dynamic_cast<CraftsmenWard*>(ward) ||
                   dynamic_cast<MerchantWard*>(ward) ||
                   dynamic_cast<GateWard*>(ward) ||
                   dynamic_cast<Slum*>(ward) ||
                   dynamic_cast<AdministrationWard*>(ward) ||
                   dynamic_cast<MilitaryWard*>(ward) ||
                   dynamic_cast<PatriciateWard*>(ward) ||
                   dynamic_cast<Farm*>(ward)) {
            // Standard buildings with light fill and dark stroke
            for (const auto& building : ward->geometry) {
                svg.drawPolygon(building, palette.light, palette.dark, Brush::NORMAL_STROKE);
            }
        }
    }
    svg.endGroup();

    // Draw walls
    svg.beginGroup("walls");
    if (model_->wall != nullptr) {
        drawWall(svg, model_->wall, false);
    }

    if (model_->citadel != nullptr) {
        Castle* castle = dynamic_cast<Castle*>(model_->citadel->ward);
        if (castle && castle->wall) {
            drawWall(svg, castle->wall, true);
        }
    }
    svg.endGroup();

    return svg.toString();
}

bool CityMap::saveToFile(const std::string& filename) const {
    float width = maxX_ - minX_;
    float height = maxY_ - minY_;
    float scale = 800.0f / width;
    float svgWidth = width * scale;
    float svgHeight = height * scale;

    SVGWriter svg(svgWidth, svgHeight, minX_, minY_, maxX_, maxY_);

    // Background
    Polygon background;
    background.vertices = {
        Point(minX_, minY_),
        Point(maxX_, minY_),
        Point(maxX_, maxY_),
        Point(minX_, maxY_)
    };
    svg.drawPolygon(background, palette.paper);

    // Draw roads
    svg.beginGroup("roads");
    for (const auto& road : model_->roads) {
        drawRoad(svg, road);
    }
    svg.endGroup();

    // Draw buildings
    svg.beginGroup("buildings");
    for (auto* patch : model_->patches) {
        if (!patch->ward) continue;

        Ward* ward = patch->ward;

        if (dynamic_cast<Castle*>(ward)) {
            drawBuilding(svg, ward->geometry, palette.light, palette.dark,
                         Brush::NORMAL_STROKE * 2);
        } else if (dynamic_cast<Cathedral*>(ward)) {
            drawBuilding(svg, ward->geometry, palette.light, palette.dark,
                         Brush::NORMAL_STROKE);
        } else if (dynamic_cast<Park*>(ward)) {
            for (const auto& grove : ward->geometry) {
                svg.drawPolygon(grove, palette.medium);
            }
        } else if (dynamic_cast<Market*>(ward) ||
                   dynamic_cast<CraftsmenWard*>(ward) ||
                   dynamic_cast<MerchantWard*>(ward) ||
                   dynamic_cast<GateWard*>(ward) ||
                   dynamic_cast<Slum*>(ward) ||
                   dynamic_cast<AdministrationWard*>(ward) ||
                   dynamic_cast<MilitaryWard*>(ward) ||
                   dynamic_cast<PatriciateWard*>(ward) ||
                   dynamic_cast<Farm*>(ward)) {
            for (const auto& building : ward->geometry) {
                svg.drawPolygon(building, palette.light, palette.dark, Brush::NORMAL_STROKE);
            }
        }
    }
    svg.endGroup();

    // Draw walls
    svg.beginGroup("walls");
    if (model_->wall != nullptr) {
        drawWall(svg, model_->wall, false);
    }

    if (model_->citadel != nullptr) {
        Castle* castle = dynamic_cast<Castle*>(model_->citadel->ward);
        if (castle && castle->wall) {
            drawWall(svg, castle->wall, true);
        }
    }
    svg.endGroup();

    return svg.saveToFile(filename);
}

void CityMap::drawRoad(SVGWriter& svg, const Polygon& road) const {
    // Draw road outline (wider, medium color)
    svg.drawPolyline(road, palette.medium, Ward::MAIN_STREET + Brush::NORMAL_STROKE, "butt");

    // Draw road fill (narrower, paper color)
    svg.drawPolyline(road, palette.paper, Ward::MAIN_STREET - Brush::NORMAL_STROKE, "butt");
}

void CityMap::drawWall(SVGWriter& svg, CurtainWall* wall, bool large) const {
    // Draw wall outline
    svg.drawPolygon(wall->shape, 0xFFFFFFFF, palette.dark, Brush::THICK_STROKE);

    // Draw gates
    for (const auto& gate : wall->gates) {
        drawGate(svg, wall->shape, gate);
    }

    // Draw towers
    float towerRadius = Brush::THICK_STROKE * (large ? 1.5f : 1.0f);
    for (const auto& tower : wall->towers) {
        drawTower(svg, tower, towerRadius);
    }
}

void CityMap::drawTower(SVGWriter& svg, const Point& p, float r) const {
    svg.drawCircle(p.x, p.y, r, palette.dark);
}

void CityMap::drawGate(SVGWriter& svg, const Polygon& wall, const Point& gate) const {
    Point nextP = wall.next(gate);
    Point prevP = wall.prev(gate);
    Point dir = nextP.subtract(prevP);

    float len = dir.length();
    if (len > 0) {
        dir = dir.scale(Brush::THICK_STROKE * 1.5f / len);
    }

    Point p1 = gate.subtract(dir);
    Point p2 = gate.add(dir);

    svg.drawLine(p1.x, p1.y, p2.x, p2.y, palette.dark, Brush::THICK_STROKE * 2, "butt");
}

void CityMap::drawBuilding(SVGWriter& svg, const std::vector<Polygon>& blocks,
                            uint32_t fill, uint32_t line, float thickness) const {
    // First pass: draw outlines (thicker strokes)
    for (const auto& block : blocks) {
        svg.drawPolygon(block, 0xFFFFFFFF, line, thickness * 2);
    }

    // Second pass: draw fills (no stroke)
    for (const auto& block : blocks) {
        svg.drawPolygon(block, fill);
    }
}

} // namespace town_generator
