#include "DwellingSVG.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>
#include <map>
#include <cmath>

namespace dwelling {

// Room colors based on type
static const char* getRoomColor(RoomType type) {
    switch (type) {
        case RoomType::Hall:       return "#e8dcc8";  // Warm beige
        case RoomType::Kitchen:    return "#ffe4b5";  // Moccasin
        case RoomType::DiningRoom: return "#deb887";  // Burlywood
        case RoomType::LivingRoom: return "#f5deb3";  // Wheat
        case RoomType::Bedroom:    return "#e6e6fa";  // Lavender
        case RoomType::Bathroom:   return "#afeeee";  // Pale turquoise
        case RoomType::Study:      return "#d3d3d3";  // Light gray
        case RoomType::Storage:    return "#c0c0c0";  // Silver
        case RoomType::Library:    return "#d2b48c";  // Tan
        case RoomType::Chapel:     return "#f0e68c";  // Khaki
        case RoomType::Gallery:    return "#fafad2";  // Light goldenrod
        case RoomType::Workshop:   return "#bc8f8f";  // Rosy brown
        case RoomType::Corridor:   return "#dcdcdc";  // Gainsboro
        case RoomType::Stairhall:  return "#d8bfd8";  // Thistle
        case RoomType::Armoury:    return "#a9a9a9";  // Dark gray
        case RoomType::Salon:      return "#ffe4c4";  // Bisque
        case RoomType::Nursery:    return "#ffb6c1";  // Light pink
        case RoomType::Pantry:     return "#f5f5dc";  // Beige
        case RoomType::Attic:      return "#e0d8c8";  // Pale wood
        case RoomType::Cellar:     return "#b0a090";  // Stone
        default:                   return "#f5f5dc";  // Beige
    }
}

// Build wall segments, excluding door positions
struct WallSegment {
    float x1, y1, x2, y2;
    bool isExterior;
};

// Door position for arc rendering
struct DoorPosition {
    float x, y;           // Hinge position
    float angle;          // Door swing angle
    float width;          // Door width
    bool isExterior;
    DoorType type;
};

// Generate SVG for regular stairs (series of lines)
static std::string regularStairsSVG(float cx, float cy, float cellSize, Dir direction, bool goingUp,
                                    const char* stairColor) {
    std::ostringstream svg;
    svg << std::fixed << std::setprecision(2);

    float stairWidth = cellSize * 0.7f;
    float stairLength = cellSize * 0.8f;
    int numSteps = 5;
    float stepSize = stairLength / numSteps;

    // Calculate rotation based on direction
    float angle = 0.0f;
    switch (direction) {
        case Dir::North: angle = -90.0f; break;
        case Dir::East: angle = 0.0f; break;
        case Dir::South: angle = 90.0f; break;
        case Dir::West: angle = 180.0f; break;
    }

    svg << "    <g transform=\"translate(" << cx << "," << cy << ") rotate(" << angle << ")\">\n";

    // Draw stair outline
    svg << "      <rect x=\"" << (-stairLength / 2) << "\" y=\"" << (-stairWidth / 2)
        << "\" width=\"" << stairLength << "\" height=\"" << stairWidth
        << "\" fill=\"" << stairColor << "\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";

    // Draw step lines
    for (int i = 1; i < numSteps; ++i) {
        float x = -stairLength / 2 + i * stepSize;
        svg << "      <line x1=\"" << x << "\" y1=\"" << (-stairWidth / 2)
            << "\" x2=\"" << x << "\" y2=\"" << (stairWidth / 2)
            << "\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";
    }

    // Draw direction arrow
    float arrowX = goingUp ? (stairLength / 4) : (-stairLength / 4);
    float arrowSize = stairWidth * 0.2f;
    svg << "      <path d=\"M " << (arrowX - arrowSize) << " 0 L " << (arrowX + arrowSize)
        << " 0 M " << arrowX << " " << (-arrowSize) << " L " << (arrowX + arrowSize)
        << " 0 L " << arrowX << " " << arrowSize
        << "\" fill=\"none\" stroke=\"#333\" stroke-width=\"1\"/>\n";

    svg << "    </g>\n";
    return svg.str();
}

// Generate SVG for spiral stairs
static std::string spiralStairsSVG(float cx, float cy, float cellSize, bool goingUp,
                                   const char* stairColor) {
    std::ostringstream svg;
    svg << std::fixed << std::setprecision(2);

    float radius = cellSize * 0.35f;
    float innerRadius = radius * 0.3f;

    // Draw outer circle
    svg << "    <circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << radius
        << "\" fill=\"" << stairColor << "\" stroke=\"#666\" stroke-width=\"1\"/>\n";

    // Draw inner circle (pole)
    svg << "    <circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << innerRadius
        << "\" fill=\"#888\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";

    // Draw spiral lines (suggesting steps)
    int numLines = 6;
    for (int i = 0; i < numLines; ++i) {
        float angle = (static_cast<float>(i) / numLines) * 2.0f * 3.14159f;
        float x1 = cx + innerRadius * std::cos(angle);
        float y1 = cy + innerRadius * std::sin(angle);
        float x2 = cx + radius * std::cos(angle);
        float y2 = cy + radius * std::sin(angle);
        svg << "    <line x1=\"" << x1 << "\" y1=\"" << y1
            << "\" x2=\"" << x2 << "\" y2=\"" << y2
            << "\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";
    }

    // Draw direction arrow (curved)
    float arrowAngle = goingUp ? 0.0f : 3.14159f;
    float ax = cx + (radius * 0.7f) * std::cos(arrowAngle);
    float ay = cy + (radius * 0.7f) * std::sin(arrowAngle);
    svg << "    <text x=\"" << ax << "\" y=\"" << (ay + 3)
        << "\" font-size=\"8\" fill=\"#333\" text-anchor=\"middle\">"
        << (goingUp ? "↑" : "↓") << "</text>\n";

    return svg.str();
}

// Generate SVG path for door arc
static std::string doorArcPath(const DoorPosition& door) {
    if (door.type == DoorType::Doorway) return "";

    float radius = door.width;
    float startAngle = door.angle;
    float endAngle = door.angle + 1.57f;  // 90 degrees

    float x1 = door.x + radius * std::cos(startAngle);
    float y1 = door.y + radius * std::sin(startAngle);
    float x2 = door.x + radius * std::cos(endAngle);
    float y2 = door.y + radius * std::sin(endAngle);

    std::ostringstream path;
    path << std::fixed << std::setprecision(2);
    path << "M " << door.x << " " << door.y;
    path << " L " << x1 << " " << y1;
    path << " A " << radius << " " << radius << " 0 0 1 " << x2 << " " << y2;
    path << " Z";
    return path.str();
}

static std::vector<WallSegment> buildWallSegments(
    const Plan& plan,
    float cellSize,
    float offsetX,
    float offsetY
) {
    std::vector<WallSegment> walls;

    // Track door edges for gap creation
    std::set<std::pair<std::pair<int,int>, std::pair<int,int>>> doorEdges;
    for (const Door& door : plan.doors()) {
        auto key = std::make_pair(
            std::make_pair(door.edge.a.i, door.edge.a.j),
            std::make_pair(door.edge.b.i, door.edge.b.j)
        );
        doorEdges.insert(key);
        // Also insert reversed edge
        auto revKey = std::make_pair(
            std::make_pair(door.edge.b.i, door.edge.b.j),
            std::make_pair(door.edge.a.i, door.edge.a.j)
        );
        doorEdges.insert(revKey);
    }

    // Track window edges
    std::set<std::pair<std::pair<int,int>, std::pair<int,int>>> windowEdges;
    for (const Window& window : plan.windows()) {
        auto key = std::make_pair(
            std::make_pair(window.edge.a.i, window.edge.a.j),
            std::make_pair(window.edge.b.i, window.edge.b.j)
        );
        windowEdges.insert(key);
        auto revKey = std::make_pair(
            std::make_pair(window.edge.b.i, window.edge.b.j),
            std::make_pair(window.edge.a.i, window.edge.a.j)
        );
        windowEdges.insert(revKey);
    }

    // Process exterior walls (contour)
    for (const Edge& e : plan.contour()) {
        auto key = std::make_pair(
            std::make_pair(e.a.i, e.a.j),
            std::make_pair(e.b.i, e.b.j)
        );

        bool isDoor = doorEdges.count(key) > 0;
        bool isWindow = windowEdges.count(key) > 0;

        float x1 = offsetX + e.a.j * cellSize;
        float y1 = offsetY + e.a.i * cellSize;
        float x2 = offsetX + e.b.j * cellSize;
        float y2 = offsetY + e.b.i * cellSize;

        if (isDoor) {
            // Create gap for door - split the wall
            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float gapSize = cellSize * 0.35f;  // Door gap size

            float dx = x2 - x1;
            float dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len > 0) {
                dx /= len;
                dy /= len;
            }

            // First segment (before door)
            float seg1EndX = mx - dx * gapSize;
            float seg1EndY = my - dy * gapSize;
            walls.push_back({x1, y1, seg1EndX, seg1EndY, true});

            // Second segment (after door)
            float seg2StartX = mx + dx * gapSize;
            float seg2StartY = my + dy * gapSize;
            walls.push_back({seg2StartX, seg2StartY, x2, y2, true});
        } else if (isWindow) {
            // Create partial gap for window
            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float gapSize = cellSize * 0.25f;

            float dx = x2 - x1;
            float dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len > 0) {
                dx /= len;
                dy /= len;
            }

            // Segments on either side of window
            walls.push_back({x1, y1, mx - dx * gapSize, my - dy * gapSize, true});
            walls.push_back({mx + dx * gapSize, my + dy * gapSize, x2, y2, true});
        } else {
            walls.push_back({x1, y1, x2, y2, true});
        }
    }

    // Process interior walls (room contours that aren't on exterior)
    std::set<std::pair<std::pair<int,int>, std::pair<int,int>>> exteriorEdges;
    for (const Edge& e : plan.contour()) {
        auto key = std::make_pair(
            std::make_pair(e.a.i, e.a.j),
            std::make_pair(e.b.i, e.b.j)
        );
        exteriorEdges.insert(key);
        auto revKey = std::make_pair(
            std::make_pair(e.b.i, e.b.j),
            std::make_pair(e.a.i, e.a.j)
        );
        exteriorEdges.insert(revKey);
    }

    // Collect all interior edges from room contours
    std::set<std::pair<std::pair<int,int>, std::pair<int,int>>> processedEdges;

    for (const Room* room : plan.rooms()) {
        for (const Edge& e : room->contour()) {
            auto key = std::make_pair(
                std::make_pair(e.a.i, e.a.j),
                std::make_pair(e.b.i, e.b.j)
            );

            // Skip if already processed or is exterior
            if (processedEdges.count(key) || exteriorEdges.count(key)) continue;
            processedEdges.insert(key);

            // Also mark reversed as processed
            auto revKey = std::make_pair(
                std::make_pair(e.b.i, e.b.j),
                std::make_pair(e.a.i, e.a.j)
            );
            processedEdges.insert(revKey);

            bool isDoor = doorEdges.count(key) > 0 || doorEdges.count(revKey) > 0;

            float x1 = offsetX + e.a.j * cellSize;
            float y1 = offsetY + e.a.i * cellSize;
            float x2 = offsetX + e.b.j * cellSize;
            float y2 = offsetY + e.b.i * cellSize;

            if (isDoor) {
                // Create gap for interior door
                float mx = (x1 + x2) / 2;
                float my = (y1 + y2) / 2;
                float gapSize = cellSize * 0.35f;

                float dx = x2 - x1;
                float dy = y2 - y1;
                float len = std::sqrt(dx*dx + dy*dy);
                if (len > 0) {
                    dx /= len;
                    dy /= len;
                }

                walls.push_back({x1, y1, mx - dx * gapSize, my - dy * gapSize, false});
                walls.push_back({mx + dx * gapSize, my + dy * gapSize, x2, y2, false});
            } else {
                walls.push_back({x1, y1, x2, y2, false});
            }
        }
    }

    return walls;
}

void writeFloorPlanSVG(
    const std::string& filename,
    const House& house,
    int floorIndex,
    const RenderOptions& options
) {
    const Plan* plan = house.floor(floorIndex);
    if (!plan) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid floor index: %d", floorIndex);
        return;
    }

    std::ofstream file(filename);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not write to: %s", filename.c_str());
        return;
    }

    float cellSize = options.cellSize;
    float padding = options.padding;
    float width = house.gridWidth() * cellSize + padding * 2;
    float height = house.gridHeight() * cellSize + padding * 2;

    file << std::fixed << std::setprecision(2);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << width << "\" height=\"" << height << "\" "
         << "viewBox=\"0 0 " << width << " " << height << "\">\n";

    // Metadata
    file << "  <!-- " << house.name() << " - Floor " << floorIndex << " -->\n";
    file << "  <!-- Generated by dwelling_generator -->\n\n";

    // Background
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"" << options.backgroundColor << "\"/>\n\n";

    // Draw room fills
    file << "  <g id=\"room-fills\">\n";
    for (const Room* room : plan->rooms()) {
        // Build polygon from room contour
        std::ostringstream points;
        bool first = true;
        for (const Edge& e : room->contour()) {
            if (first) {
                points << (padding + e.a.j * cellSize) << "," << (padding + e.a.i * cellSize);
                first = false;
            }
            points << " " << (padding + e.b.j * cellSize) << "," << (padding + e.b.i * cellSize);
        }

        file << "    <polygon points=\"" << points.str() << "\" "
             << "fill=\"" << getRoomColor(room->type()) << "\" "
             << "stroke=\"none\"/>\n";
    }
    file << "  </g>\n\n";

    // Debug grid
    if (options.showGrid) {
        file << "  <g id=\"debug-grid\" stroke=\"#ddd\" stroke-width=\"0.5\">\n";
        for (int i = 0; i <= house.gridHeight(); ++i) {
            float y = padding + i * cellSize;
            file << "    <line x1=\"" << padding << "\" y1=\"" << y
                 << "\" x2=\"" << (padding + house.gridWidth() * cellSize)
                 << "\" y2=\"" << y << "\"/>\n";
        }
        for (int j = 0; j <= house.gridWidth(); ++j) {
            float x = padding + j * cellSize;
            file << "    <line x1=\"" << x << "\" y1=\"" << padding
                 << "\" x2=\"" << x
                 << "\" y2=\"" << (padding + house.gridHeight() * cellSize) << "\"/>\n";
        }
        file << "  </g>\n\n";
    }

    // Draw walls (with door gaps)
    auto walls = buildWallSegments(*plan, cellSize, padding, padding);
    file << "  <g id=\"walls\" stroke=\"" << options.wallColor
         << "\" stroke-width=\"" << options.wallThickness
         << "\" stroke-linecap=\"round\">\n";
    for (const auto& wall : walls) {
        // Exterior walls are thicker
        float thickness = wall.isExterior ? options.wallThickness : options.wallThickness * 0.6f;
        file << "    <line x1=\"" << wall.x1 << "\" y1=\"" << wall.y1
             << "\" x2=\"" << wall.x2 << "\" y2=\"" << wall.y2
             << "\" stroke-width=\"" << thickness << "\"/>\n";
    }
    file << "  </g>\n\n";

    // Draw windows
    if (options.showWindows) {
        file << "  <g id=\"windows\" stroke=\"" << options.windowColor
             << "\" stroke-width=\"" << (options.wallThickness * 1.5f)
             << "\" stroke-linecap=\"round\">\n";
        for (const Window& window : plan->windows()) {
            float x1 = padding + window.edge.a.j * cellSize;
            float y1 = padding + window.edge.a.i * cellSize;
            float x2 = padding + window.edge.b.j * cellSize;
            float y2 = padding + window.edge.b.i * cellSize;

            // Window is in the center of the edge
            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float dx = x2 - x1;
            float dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len > 0) {
                dx /= len;
                dy /= len;
            }
            float winSize = cellSize * 0.25f;

            file << "    <line x1=\"" << (mx - dx * winSize) << "\" y1=\"" << (my - dy * winSize)
                 << "\" x2=\"" << (mx + dx * winSize) << "\" y2=\"" << (my + dy * winSize) << "\"/>\n";
        }
        file << "  </g>\n\n";
    }

    // Draw door arcs
    if (options.doorMode == DoorMode::Arc) {
        file << "  <g id=\"doors\" fill=\"none\" stroke=\"" << options.doorColor
             << "\" stroke-width=\"1\">\n";
        for (const Door& door : plan->doors()) {
            if (door.type == DoorType::Doorway) continue;

            float x1 = padding + door.edge.a.j * cellSize;
            float y1 = padding + door.edge.a.i * cellSize;
            float x2 = padding + door.edge.b.j * cellSize;
            float y2 = padding + door.edge.b.i * cellSize;

            // Door center and direction
            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float dx = x2 - x1;
            float dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len > 0) { dx /= len; dy /= len; }

            float doorWidth = cellSize * 0.3f;
            float hingeX = mx - dx * doorWidth * 0.4f;
            float hingeY = my - dy * doorWidth * 0.4f;

            // Calculate door swing arc
            float perpX = -dy;
            float perpY = dx;
            float arcEndX = hingeX + perpX * doorWidth;
            float arcEndY = hingeY + perpY * doorWidth;

            // Draw door (line + arc)
            file << "    <line x1=\"" << hingeX << "\" y1=\"" << hingeY
                 << "\" x2=\"" << arcEndX << "\" y2=\"" << arcEndY
                 << "\" stroke-width=\"2\"/>\n";

            // Draw arc showing door swing
            file << "    <path d=\"M " << (hingeX + dx * doorWidth * 0.8f) << " " << (hingeY + dy * doorWidth * 0.8f)
                 << " A " << doorWidth << " " << doorWidth << " 0 0 1 " << arcEndX << " " << arcEndY
                 << "\" stroke-dasharray=\"2,2\"/>\n";
        }
        file << "  </g>\n\n";
    }

    // Draw stairs
    if (!plan->stairs().empty()) {
        file << "  <g id=\"stairs\">\n";
        for (const Stair& stair : plan->stairs()) {
            float cx = padding + (stair.cell.j + 0.5f) * cellSize;
            float cy = padding + (stair.cell.i + 0.5f) * cellSize;

            if (stair.type == StairType::Spiral) {
                file << spiralStairsSVG(cx, cy, cellSize, stair.goingUp, options.stairColor);
            } else {
                file << regularStairsSVG(cx, cy, cellSize, stair.direction, stair.goingUp, options.stairColor);
            }
        }
        file << "  </g>\n\n";
    }

    // Draw room labels
    if (options.showRoomLabels) {
        file << "  <g id=\"room-labels\" font-family=\"sans-serif\" font-size=\"10\" "
             << "fill=\"" << options.roomLabelColor << "\" text-anchor=\"middle\">\n";
        for (const Room* room : plan->rooms()) {
            // Find center of room
            float cx = 0, cy = 0;
            for (const Cell* c : room->area()) {
                cx += c->j + 0.5f;
                cy += c->i + 0.5f;
            }
            cx = padding + (cx / room->size()) * cellSize;
            cy = padding + (cy / room->size()) * cellSize;

            std::string label = room->name().empty() ? roomTypeName(room->type()) : room->name();
            file << "    <text x=\"" << cx << "\" y=\"" << (cy + 3) << "\">" << label << "</text>\n";
        }
        file << "  </g>\n\n";
    }

    // Title
    file << "  <text x=\"" << (width / 2) << "\" y=\"15\" "
         << "font-family=\"sans-serif\" font-size=\"12\" font-weight=\"bold\" "
         << "text-anchor=\"middle\" fill=\"#333\">"
         << house.name() << " - Floor " << floorIndex << "</text>\n";

    file << "</svg>\n";

    SDL_Log("Wrote floor plan SVG: %s", filename.c_str());
}

void writeAllFloorsSVG(
    const std::string& filename,
    const House& house,
    const RenderOptions& options
) {
    std::ofstream file(filename);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not write to: %s", filename.c_str());
        return;
    }

    float cellSize = options.cellSize;
    float padding = options.padding;
    float floorWidth = house.gridWidth() * cellSize + padding * 2;
    float floorHeight = house.gridHeight() * cellSize + padding * 2;

    int numFloors = house.numFloors();
    int cols = std::min(numFloors, 3);
    int rows = (numFloors + cols - 1) / cols;

    float totalWidth = cols * floorWidth + padding;
    float totalHeight = rows * floorHeight + padding + 30;  // Extra for title

    file << std::fixed << std::setprecision(2);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << totalWidth << "\" height=\"" << totalHeight << "\" "
         << "viewBox=\"0 0 " << totalWidth << " " << totalHeight << "\">\n";

    // Title
    file << "  <text x=\"" << (totalWidth / 2) << "\" y=\"20\" "
         << "font-family=\"sans-serif\" font-size=\"16\" font-weight=\"bold\" "
         << "text-anchor=\"middle\" fill=\"#333\">" << house.name() << "</text>\n\n";

    // Draw each floor
    for (int f = 0; f < numFloors; ++f) {
        int col = f % cols;
        int row = f / cols;
        float offsetX = col * floorWidth + padding / 2;
        float offsetY = row * floorHeight + 30;

        file << "  <g id=\"floor-" << f << "\" transform=\"translate(" << offsetX << "," << offsetY << ")\">\n";

        const Plan* plan = house.floor(f);
        if (!plan) continue;

        // Background
        file << "    <rect width=\"" << (floorWidth - padding) << "\" height=\"" << (floorHeight - padding)
             << "\" fill=\"" << options.backgroundColor << "\" rx=\"5\"/>\n";

        // Room fills
        for (const Room* room : plan->rooms()) {
            std::ostringstream points;
            bool first = true;
            for (const Edge& e : room->contour()) {
                if (first) {
                    points << (padding + e.a.j * cellSize) << "," << (padding + e.a.i * cellSize);
                    first = false;
                }
                points << " " << (padding + e.b.j * cellSize) << "," << (padding + e.b.i * cellSize);
            }
            file << "    <polygon points=\"" << points.str() << "\" "
                 << "fill=\"" << getRoomColor(room->type()) << "\"/>\n";
        }

        // Walls
        auto walls = buildWallSegments(*plan, cellSize, padding, padding);
        for (const auto& wall : walls) {
            float thickness = wall.isExterior ? options.wallThickness : options.wallThickness * 0.6f;
            file << "    <line x1=\"" << wall.x1 << "\" y1=\"" << wall.y1
                 << "\" x2=\"" << wall.x2 << "\" y2=\"" << wall.y2
                 << "\" stroke=\"" << options.wallColor
                 << "\" stroke-width=\"" << thickness
                 << "\" stroke-linecap=\"round\"/>\n";
        }

        // Windows
        for (const Window& window : plan->windows()) {
            float x1 = padding + window.edge.a.j * cellSize;
            float y1 = padding + window.edge.a.i * cellSize;
            float x2 = padding + window.edge.b.j * cellSize;
            float y2 = padding + window.edge.b.i * cellSize;
            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float dx = x2 - x1;
            float dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len > 0) { dx /= len; dy /= len; }
            float winSize = cellSize * 0.25f;

            file << "    <line x1=\"" << (mx - dx * winSize) << "\" y1=\"" << (my - dy * winSize)
                 << "\" x2=\"" << (mx + dx * winSize) << "\" y2=\"" << (my + dy * winSize)
                 << "\" stroke=\"" << options.windowColor
                 << "\" stroke-width=\"" << (options.wallThickness * 1.5f)
                 << "\" stroke-linecap=\"round\"/>\n";
        }

        // Stairs
        for (const Stair& stair : plan->stairs()) {
            float cx = padding + (stair.cell.j + 0.5f) * cellSize;
            float cy = padding + (stair.cell.i + 0.5f) * cellSize;
            if (stair.type == StairType::Spiral) {
                file << spiralStairsSVG(cx, cy, cellSize, stair.goingUp, options.stairColor);
            } else {
                file << regularStairsSVG(cx, cy, cellSize, stair.direction, stair.goingUp, options.stairColor);
            }
        }

        // Floor label
        file << "    <text x=\"" << ((floorWidth - padding) / 2) << "\" y=\"" << (floorHeight - padding - 5)
             << "\" font-family=\"sans-serif\" font-size=\"10\" text-anchor=\"middle\" fill=\"#666\">"
             << "Floor " << f << "</text>\n";

        file << "  </g>\n\n";
    }

    file << "</svg>\n";

    SDL_Log("Wrote all floors SVG: %s (%d floors)", filename.c_str(), numFloors);
}

void writeOrthoViewSVG(
    const std::string& filename,
    const House& house,
    const RenderOptions& options
) {
    std::ofstream file(filename);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not write to: %s", filename.c_str());
        return;
    }

    float cellSize = options.cellSize;
    float padding = options.padding;
    float floorHeight = cellSize * 0.8f;  // Height of one floor in 3D

    // Isometric projection angles
    float isoAngleX = 0.866f;  // cos(30)
    float isoAngleY = 0.5f;    // sin(30)

    // Calculate bounds
    float maxX = 0, maxY = 0;
    int numFloors = house.numFloors();

    for (int f = 0; f < numFloors; ++f) {
        float floorZ = f * floorHeight;
        for (int i = 0; i <= house.gridHeight(); ++i) {
            for (int j = 0; j <= house.gridWidth(); ++j) {
                float x = j * cellSize;
                float y = i * cellSize;
                // Isometric projection
                float isoX = (x - y) * isoAngleX;
                float isoY = (x + y) * isoAngleY - floorZ;
                maxX = std::max(maxX, std::abs(isoX));
                maxY = std::max(maxY, isoY);
            }
        }
    }

    float width = maxX * 2 + padding * 4;
    float height = maxY + numFloors * floorHeight + padding * 4;
    float centerX = width / 2;
    float centerY = height - padding * 2;

    file << std::fixed << std::setprecision(2);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << width << "\" height=\"" << height << "\" "
         << "viewBox=\"0 0 " << width << " " << height << "\">\n";

    file << "  <!-- " << house.name() << " - Orthographic View -->\n\n";

    // Background
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"" << options.backgroundColor << "\"/>\n\n";

    // Title
    file << "  <text x=\"" << (width / 2) << "\" y=\"20\" "
         << "font-family=\"sans-serif\" font-size=\"14\" font-weight=\"bold\" "
         << "text-anchor=\"middle\" fill=\"#333\">" << house.name() << " - 3D View</text>\n\n";

    // Lambda to project 3D to isometric 2D
    auto project = [&](float x, float y, float z) -> std::pair<float, float> {
        float isoX = centerX + (x - y) * isoAngleX;
        float isoY = centerY - (x + y) * isoAngleY - z;
        return {isoX, isoY};
    };

    // Draw floors from bottom to top
    for (int f = 0; f < numFloors; ++f) {
        float floorZ = f * floorHeight;
        const Plan* plan = house.floor(f);
        if (!plan) continue;

        file << "  <g id=\"floor-3d-" << f << "\">\n";

        // Draw floor surface for each room
        for (const Room* room : plan->rooms()) {
            // Build polygon in 3D space
            std::ostringstream points;
            bool first = true;
            for (const Edge& e : room->contour()) {
                float x = e.a.j * cellSize;
                float y = e.a.i * cellSize;
                auto [px, py] = project(x, y, floorZ);
                if (first) {
                    points << px << "," << py;
                    first = false;
                } else {
                    points << " " << px << "," << py;
                }
            }

            file << "    <polygon points=\"" << points.str() << "\" "
                 << "fill=\"" << getRoomColor(room->type()) << "\" "
                 << "stroke=\"" << options.wallColor << "\" "
                 << "stroke-width=\"0.5\"/>\n";
        }

        // Draw walls as vertical rectangles
        for (const Edge& e : plan->contour()) {
            float x1 = e.a.j * cellSize;
            float y1 = e.a.i * cellSize;
            float x2 = e.b.j * cellSize;
            float y2 = e.b.i * cellSize;

            // Bottom corners
            auto [bx1, by1] = project(x1, y1, floorZ);
            auto [bx2, by2] = project(x2, y2, floorZ);
            // Top corners
            auto [tx1, ty1] = project(x1, y1, floorZ + floorHeight);
            auto [tx2, ty2] = project(x2, y2, floorZ + floorHeight);

            // Determine wall color based on direction (for shading)
            const char* wallFill = "#a0a0a0";
            if (e.dir == Dir::South || e.dir == Dir::East) {
                wallFill = "#808080";  // Darker for south/east facing
            }

            std::ostringstream wallPoints;
            wallPoints << bx1 << "," << by1 << " "
                      << bx2 << "," << by2 << " "
                      << tx2 << "," << ty2 << " "
                      << tx1 << "," << ty1;

            file << "    <polygon points=\"" << wallPoints.str() << "\" "
                 << "fill=\"" << wallFill << "\" "
                 << "stroke=\"" << options.wallColor << "\" "
                 << "stroke-width=\"0.5\"/>\n";
        }

        // Draw windows on walls
        for (const Window& window : plan->windows()) {
            float x1 = window.edge.a.j * cellSize;
            float y1 = window.edge.a.i * cellSize;
            float x2 = window.edge.b.j * cellSize;
            float y2 = window.edge.b.i * cellSize;

            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float windowBottom = floorZ + floorHeight * 0.3f;
            float windowTop = floorZ + floorHeight * 0.8f;

            auto [wx1, wy1] = project(mx, my, windowBottom);
            auto [wx2, wy2] = project(mx, my, windowTop);

            file << "    <line x1=\"" << wx1 << "\" y1=\"" << wy1
                 << "\" x2=\"" << wx2 << "\" y2=\"" << wy2
                 << "\" stroke=\"" << options.windowColor
                 << "\" stroke-width=\"3\"/>\n";
        }

        file << "  </g>\n\n";
    }

    // Draw roof on top floor
    {
        float roofZ = numFloors * floorHeight;
        float roofPeak = roofZ + floorHeight * 0.6f;
        const Plan* topPlan = house.floor(numFloors - 1);

        if (topPlan) {
            // Simple flat roof with slight elevation
            for (const Room* room : topPlan->rooms()) {
                std::ostringstream points;
                bool first = true;
                for (const Edge& e : room->contour()) {
                    float x = e.a.j * cellSize;
                    float y = e.a.i * cellSize;
                    auto [px, py] = project(x, y, roofZ);
                    if (first) {
                        points << px << "," << py;
                        first = false;
                    } else {
                        points << " " << px << "," << py;
                    }
                }

                file << "  <polygon points=\"" << points.str() << "\" "
                     << "fill=\"#8b4513\" stroke=\"#5a2d0a\" stroke-width=\"1\"/>\n";
            }
        }
    }

    file << "</svg>\n";

    SDL_Log("Wrote ortho view SVG: %s", filename.c_str());
}

void writeFacadeViewSVG(
    const std::string& filename,
    const House& house,
    const RenderOptions& options
) {
    std::ofstream file(filename);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not write to: %s", filename.c_str());
        return;
    }

    float cellSize = options.cellSize;
    float padding = options.padding;
    float floorHeight = cellSize * 1.2f;  // Height of one floor in elevation view
    int numFloors = house.numFloors();

    // Find building width (from south-facing edges)
    int minJ = house.gridWidth(), maxJ = 0;
    const Plan* basePlan = house.floor(0);
    if (!basePlan) return;

    for (const Edge& e : basePlan->contour()) {
        if (e.dir == Dir::South) {  // South-facing edges are visible
            minJ = std::min(minJ, std::min(e.a.j, e.b.j));
            maxJ = std::max(maxJ, std::max(e.a.j, e.b.j));
        }
    }

    float buildingWidth = (maxJ - minJ) * cellSize;
    float buildingHeight = numFloors * floorHeight;
    float roofHeight = floorHeight * 0.5f;

    float width = buildingWidth + padding * 4;
    float height = buildingHeight + roofHeight + padding * 4;

    file << std::fixed << std::setprecision(2);
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         << "width=\"" << width << "\" height=\"" << height << "\" "
         << "viewBox=\"0 0 " << width << " " << height << "\">\n";

    file << "  <!-- " << house.name() << " - Facade View -->\n\n";

    // Background (sky)
    file << "  <rect width=\"100%\" height=\"100%\" fill=\"#87CEEB\"/>\n";

    // Ground
    float groundY = height - padding;
    file << "  <rect x=\"0\" y=\"" << groundY << "\" width=\"" << width
         << "\" height=\"" << padding << "\" fill=\"#228B22\"/>\n\n";

    float baseX = padding * 2;
    float baseY = groundY;

    // Draw each floor
    for (int f = 0; f < numFloors; ++f) {
        const Plan* plan = house.floor(f);
        if (!plan) continue;

        float floorTop = baseY - (f + 1) * floorHeight;
        float floorBottom = baseY - f * floorHeight;

        file << "  <g id=\"facade-floor-" << f << "\">\n";

        // Find cells that have south-facing walls (are on the south edge of the building)
        std::set<int> southWallColumns;
        for (const Edge& e : plan->contour()) {
            if (e.dir == Dir::South) {
                // South-facing edge - the wall is visible from south
                // The column is determined by the j coordinate
                southWallColumns.insert(e.a.j);
            }
        }

        // Merge adjacent columns into wall segments
        std::vector<std::pair<int, int>> wallSegments;
        int segStart = -1;
        int lastCol = -2;
        for (int col : southWallColumns) {
            if (col == lastCol + 1) {
                // Continue segment
            } else {
                // End previous segment if any
                if (segStart >= 0) {
                    wallSegments.push_back({segStart, lastCol + 1});
                }
                segStart = col;
            }
            lastCol = col;
        }
        if (segStart >= 0) {
            wallSegments.push_back({segStart, lastCol + 1});
        }

        // Draw wall rectangles
        for (const auto& seg : wallSegments) {
            float x1 = baseX + (seg.first - minJ) * cellSize;
            float x2 = baseX + (seg.second - minJ) * cellSize;

            // Wall fill (brick-like texture via color)
            const char* wallColor = (f == 0) ? "#8B4513" : "#A0522D";  // Darker base, lighter upper
            file << "    <rect x=\"" << x1 << "\" y=\"" << floorTop
                 << "\" width=\"" << (x2 - x1) << "\" height=\"" << floorHeight
                 << "\" fill=\"" << wallColor << "\" stroke=\"#5a2d0a\" stroke-width=\"1\"/>\n";
        }

        // Draw windows on south-facing walls
        for (const Window& window : plan->windows()) {
            if (window.edge.dir == Dir::South) {
                float wx = baseX + (window.edge.a.j - minJ + 0.5f) * cellSize;
                float wy = floorTop + floorHeight * 0.25f;
                float ww = cellSize * 0.6f;
                float wh = floorHeight * 0.5f;

                // Window frame
                file << "    <rect x=\"" << (wx - ww / 2) << "\" y=\"" << wy
                     << "\" width=\"" << ww << "\" height=\"" << wh
                     << "\" fill=\"" << options.windowColor << "\" stroke=\"#333\" stroke-width=\"1\"/>\n";

                // Window panes (cross)
                file << "    <line x1=\"" << wx << "\" y1=\"" << wy
                     << "\" x2=\"" << wx << "\" y2=\"" << (wy + wh)
                     << "\" stroke=\"#333\" stroke-width=\"1\"/>\n";
                file << "    <line x1=\"" << (wx - ww / 2) << "\" y1=\"" << (wy + wh / 2)
                     << "\" x2=\"" << (wx + ww / 2) << "\" y2=\"" << (wy + wh / 2)
                     << "\" stroke=\"#333\" stroke-width=\"1\"/>\n";
            }
        }

        // Draw door on ground floor (entrance)
        if (f == 0 && plan->entrance()) {
            const Door* entrance = plan->entrance();
            if (entrance->edge.dir == Dir::South) {
                float dx = baseX + (entrance->edge.a.j - minJ + 0.5f) * cellSize;
                float dw = cellSize * 0.5f;
                float dh = floorHeight * 0.7f;

                file << "    <rect x=\"" << (dx - dw / 2) << "\" y=\"" << (floorBottom - dh)
                     << "\" width=\"" << dw << "\" height=\"" << dh
                     << "\" fill=\"" << options.doorColor << "\" stroke=\"#333\" stroke-width=\"1\"/>\n";

                // Door handle
                file << "    <circle cx=\"" << (dx + dw / 4) << "\" cy=\"" << (floorBottom - dh / 2)
                     << "\" r=\"2\" fill=\"#FFD700\"/>\n";
            }
        }

        file << "  </g>\n\n";
    }

    // Draw roof
    float roofTop = baseY - numFloors * floorHeight;
    {
        // Use full building width for roof
        float roofX1 = baseX - cellSize * 0.2f;
        float roofX2 = baseX + buildingWidth + cellSize * 0.2f;
        float roofMid = (roofX1 + roofX2) / 2;

        // Triangular roof
        std::ostringstream roofPoints;
        roofPoints << roofX1 << "," << roofTop << " "
                  << roofMid << "," << (roofTop - roofHeight) << " "
                  << roofX2 << "," << roofTop;

        file << "  <polygon points=\"" << roofPoints.str()
             << "\" fill=\"#8B0000\" stroke=\"#5a0a0a\" stroke-width=\"2\"/>\n";
    }

    // Title
    file << "  <text x=\"" << (width / 2) << "\" y=\"25\" "
         << "font-family=\"sans-serif\" font-size=\"14\" font-weight=\"bold\" "
         << "text-anchor=\"middle\" fill=\"#333\">" << house.name() << " - Front Elevation</text>\n";

    file << "</svg>\n";

    SDL_Log("Wrote facade view SVG: %s", filename.c_str());
}

} // namespace dwelling
