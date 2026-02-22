#include "town_generator/building/City.h"
#include "town_generator/svg/SVGWriter.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// Minimal settlement data loaded from settlements.json
struct MapSettlement {
    uint32_t id;
    std::string type;       // "town", "village", "hamlet", "fishing_village"
    glm::vec2 position;     // World XZ coordinates
    float radius;           // Settlement area radius in meters
    std::vector<std::string> features;
};

// Minimal river segment for SVG rendering
struct RiverSegment {
    std::vector<glm::vec2> points;
    float width;
};

// Minimal lake for SVG rendering
struct LakeData {
    glm::vec2 position;
    float radius;
};

// Road segment for SVG rendering
struct RoadSegment {
    std::vector<glm::vec2> points;
    std::string type;   // "footpath", "bridleway", "lane", "road", "main_road"
    float width;
};

// ---------------------------------------------------------------------------
// Loading functions
// ---------------------------------------------------------------------------

static bool loadSettlements(const std::string& path, std::vector<MapSettlement>& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open settlements: %s", path.c_str());
        return false;
    }
    try {
        nlohmann::json j;
        file >> j;
        for (const auto& s : j["settlements"]) {
            MapSettlement ms;
            ms.id = s["id"].get<uint32_t>();
            ms.type = s["type"].get<std::string>();
            auto& pos = s["position"];
            ms.position.x = pos[0].get<float>();
            ms.position.y = pos[1].get<float>();
            ms.radius = s["radius"].get<float>();
            if (s.contains("features")) {
                for (const auto& f : s["features"]) {
                    ms.features.push_back(f.get<std::string>());
                }
            }
            out.push_back(std::move(ms));
        }
        SDL_Log("Loaded %zu settlements from %s", out.size(), path.c_str());
        return true;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse settlements: %s", e.what());
        return false;
    }
}

static bool loadRoads(const std::string& path, std::vector<RoadSegment>& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No roads file: %s", path.c_str());
        return false;
    }
    try {
        nlohmann::json j;
        file >> j;
        if (j.contains("features")) {
            for (const auto& feature : j["features"]) {
                if (feature["geometry"]["type"] != "LineString") continue;
                RoadSegment road;
                const auto& props = feature["properties"];
                road.type = props.value("type", "lane");
                road.width = props.value("width", 4.0f);
                const auto& coords = feature["geometry"]["coordinates"];
                for (const auto& coord : coords) {
                    road.points.emplace_back(coord[0].get<float>(), coord[1].get<float>());
                }
                out.push_back(std::move(road));
            }
        }
        SDL_Log("Loaded %zu roads from %s", out.size(), path.c_str());
        return true;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse roads: %s", e.what());
        return false;
    }
}

static bool loadRivers(const std::string& path, std::vector<RiverSegment>& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No rivers file: %s", path.c_str());
        return false;
    }
    try {
        nlohmann::json j;
        file >> j;
        if (j.contains("features")) {
            for (const auto& feature : j["features"]) {
                if (feature["geometry"]["type"] != "LineString") continue;
                RiverSegment river;
                const auto& props = feature["properties"];
                river.width = props.value("width", 5.0f);
                const auto& coords = feature["geometry"]["coordinates"];
                for (const auto& coord : coords) {
                    // coords are [x, z, y(altitude)] - we only need x,z
                    river.points.emplace_back(coord[0].get<float>(), coord[1].get<float>());
                }
                out.push_back(std::move(river));
            }
        }
        SDL_Log("Loaded %zu rivers from %s", out.size(), path.c_str());
        return true;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse rivers: %s", e.what());
        return false;
    }
}

static bool loadLakes(const std::string& path, std::vector<LakeData>& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No lakes file: %s", path.c_str());
        return false;
    }
    try {
        nlohmann::json j;
        file >> j;
        if (j.contains("features")) {
            for (const auto& feature : j["features"]) {
                LakeData lake;
                const auto& geom = feature["geometry"];
                const auto& props = feature["properties"];
                lake.radius = props.value("radius", 10.0f);
                if (geom["type"] == "Point") {
                    lake.position.x = geom["coordinates"][0].get<float>();
                    lake.position.y = geom["coordinates"][1].get<float>();
                } else if (geom["type"] == "Polygon") {
                    const auto& coords = geom["coordinates"][0];
                    float sumX = 0, sumY = 0;
                    for (const auto& c : coords) {
                        sumX += c[0].get<float>();
                        sumY += c[1].get<float>();
                    }
                    lake.position.x = sumX / coords.size();
                    lake.position.y = sumY / coords.size();
                }
                out.push_back(lake);
            }
        }
        SDL_Log("Loaded %zu lakes from %s", out.size(), path.c_str());
        return true;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse lakes: %s", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Settlement → town generation parameters
// ---------------------------------------------------------------------------

static int getCellCount(const std::string& type) {
    if (type == "town")             return 50;
    if (type == "village")          return 18;
    if (type == "fishing_village")  return 14;
    if (type == "hamlet")           return 8;
    return 15;
}

static bool hasFeature(const std::vector<std::string>& features, const std::string& name) {
    return std::find(features.begin(), features.end(), name) != features.end();
}

static bool shouldBeCoastal(const MapSettlement& s) {
    return hasFeature(s.features, "coastal") || hasFeature(s.features, "harbour");
}

// ---------------------------------------------------------------------------
// SVG helpers
// ---------------------------------------------------------------------------

static std::string pathFromPoints(const std::vector<glm::vec2>& pts) {
    if (pts.empty()) return "";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "M " << pts[0].x << " " << pts[0].y;
    for (size_t i = 1; i < pts.size(); ++i) {
        ss << " L " << pts[i].x << " " << pts[i].y;
    }
    return ss.str();
}

static float getRoadSvgWidth(const std::string& type) {
    if (type == "main_road") return 8.0f;
    if (type == "road")      return 6.0f;
    if (type == "lane")      return 4.0f;
    if (type == "bridleway") return 3.0f;
    if (type == "footpath")  return 1.5f;
    return 4.0f;
}

static const char* getRoadColor(const std::string& type) {
    if (type == "main_road") return "#8B7355";
    if (type == "road")      return "#A09070";
    if (type == "lane")      return "#B0A080";
    if (type == "bridleway") return "#C0B090";
    if (type == "footpath")  return "#D0C8A0";
    return "#B0A080";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static void printUsage(const char* prog) {
    SDL_Log("Usage: %s [options]", prog);
    SDL_Log("  --settlements <path>  settlements.json (required)");
    SDL_Log("  --roads <path>        roads.geojson");
    SDL_Log("  --rivers <path>       rivers.geojson");
    SDL_Log("  --lakes <path>        lakes.geojson");
    SDL_Log("  --output <path>       Output SVG path (default: town_map.svg)");
    SDL_Log("  --size <int>          SVG pixel dimension (default: 8192)");
    SDL_Log("  --seed <int>          Base random seed (default: 42)");
    SDL_Log("  --terrain-size <float> Terrain size in meters (default: 16384)");
}

int main(int argc, char* argv[]) {
    std::string settlementsPath;
    std::string roadsPath;
    std::string riversPath;
    std::string lakesPath;
    std::string outputPath = "town_map.svg";
    int svgSize = 8192;
    int baseSeed = 42;
    float terrainSize = 16384.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (i + 1 >= argc) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Option %s requires a value", arg.c_str());
            return 1;
        }
        if (arg == "--settlements")       settlementsPath = argv[++i];
        else if (arg == "--roads")        roadsPath = argv[++i];
        else if (arg == "--rivers")       riversPath = argv[++i];
        else if (arg == "--lakes")        lakesPath = argv[++i];
        else if (arg == "--output")       outputPath = argv[++i];
        else if (arg == "--size")         svgSize = std::atoi(argv[++i]);
        else if (arg == "--seed")         baseSeed = std::atoi(argv[++i]);
        else if (arg == "--terrain-size") terrainSize = static_cast<float>(std::atof(argv[++i]));
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    if (settlementsPath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No --settlements path specified");
        printUsage(argv[0]);
        return 1;
    }

    // Load data
    std::vector<MapSettlement> settlements;
    if (!loadSettlements(settlementsPath, settlements)) return 1;

    std::vector<RoadSegment> roads;
    if (!roadsPath.empty()) loadRoads(roadsPath, roads);

    std::vector<RiverSegment> rivers;
    if (!riversPath.empty()) loadRivers(riversPath, rivers);

    std::vector<LakeData> lakes;
    if (!lakesPath.empty()) loadLakes(lakesPath, lakes);

    // Scale factor: world coords → SVG pixels
    float worldToSvg = static_cast<float>(svgSize) / terrainSize;

    // Shared style for all towns
    town_generator::svg::SVGWriter::Style style;

    // Open output file
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open output: %s", outputPath.c_str());
        return 1;
    }

    out << std::fixed << std::setprecision(2);

    // SVG header — viewBox in world coordinates
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << svgSize << "\" height=\"" << svgSize << "\" "
        << "viewBox=\"0 0 " << terrainSize << " " << terrainSize << "\">\n";

    // Shared CSS styles
    out << "  <style>\n";
    out << "    .building { stroke: " << style.buildingStroke
        << "; stroke-width: " << style.buildingStrokeWidth << "; }\n";
    out << "    .special { fill: " << style.wallStroke
        << "; stroke: " << style.buildingStroke
        << "; stroke-width: " << style.buildingStrokeWidth << "; }\n";
    out << "    .road { fill: none; stroke-linecap: round; stroke-linejoin: round; }\n";
    out << "    .tower { fill: " << style.towerFill << "; }\n";
    out << "    .map-road { fill: none; stroke-linecap: round; stroke-linejoin: round; }\n";
    out << "    .map-river { fill: none; stroke: #4A7FB5; stroke-linecap: round; stroke-linejoin: round; }\n";
    out << "    .map-lake { fill: #4A7FB5; fill-opacity: 0.6; stroke: #3A6FA5; stroke-width: 2; }\n";
    out << "  </style>\n";

    // Background
    out << "  <rect width=\"" << terrainSize << "\" height=\"" << terrainSize
        << "\" fill=\"#E8DFC8\"/>\n";

    // --- Layer 1: Rivers ---
    if (!rivers.empty()) {
        out << "  <g id=\"rivers\">\n";
        for (const auto& river : rivers) {
            if (river.points.size() < 2) continue;
            out << "    <path class=\"map-river\" d=\"" << pathFromPoints(river.points)
                << "\" stroke-width=\"" << std::max(river.width, 3.0f) << "\"/>\n";
        }
        out << "  </g>\n";
    }

    // --- Layer 2: Lakes ---
    if (!lakes.empty()) {
        out << "  <g id=\"lakes\">\n";
        for (const auto& lake : lakes) {
            out << "    <circle class=\"map-lake\" cx=\"" << lake.position.x
                << "\" cy=\"" << lake.position.y
                << "\" r=\"" << std::max(lake.radius, 5.0f) << "\"/>\n";
        }
        out << "  </g>\n";
    }

    // --- Layer 3: Roads ---
    if (!roads.empty()) {
        out << "  <g id=\"map-roads\">\n";
        for (const auto& road : roads) {
            if (road.points.size() < 2) continue;
            out << "    <path class=\"map-road\" d=\"" << pathFromPoints(road.points)
                << "\" stroke=\"" << getRoadColor(road.type)
                << "\" stroke-width=\"" << getRoadSvgWidth(road.type) << "\"/>\n";
        }
        out << "  </g>\n";
    }

    // --- Layer 4: Towns ---
    out << "  <g id=\"towns\">\n";

    for (const auto& settlement : settlements) {
        int cells = getCellCount(settlement.type);
        int seed = baseSeed + static_cast<int>(settlement.id) * 31337;

        SDL_Log("Generating town for settlement #%u (%s) at (%.0f, %.0f) with %d cells",
                settlement.id, settlement.type.c_str(),
                settlement.position.x, settlement.position.y, cells);

        try {
            town_generator::building::City city(cells, seed);

            if (shouldBeCoastal(settlement)) {
                city.coastNeeded = true;
            } else {
                city.coastNeeded = false;
            }

            city.build();

            auto content = town_generator::svg::SVGWriter::generateContent(city, style);

            // Scale: fit the town's bounding box into the settlement's diameter
            double townMaxDim = std::max(content.bounds.width, content.bounds.height);
            double targetDiameter = settlement.radius * 2.0;
            double scale = targetDiameter / townMaxDim;

            // Town center in its local coordinate space
            double townCenterX = content.bounds.centerX();
            double townCenterY = content.bounds.centerY();

            // SVG transform: translate to world position, scale, then re-center
            // Applied right-to-left: first translate(-cx,-cy), then scale, then translate(wx,wy)
            out << "    <g transform=\"translate(" << settlement.position.x
                << "," << settlement.position.y
                << ") scale(" << scale
                << ") translate(" << -townCenterX
                << "," << -townCenterY << ")\">\n";

            out << content.svgGroups;

            out << "    </g>\n";

            SDL_Log("  Town #%u: bounds %.0fx%.0f, scale %.4f",
                    settlement.id, content.bounds.width, content.bounds.height, scale);

        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to generate town for settlement #%u: %s",
                         settlement.id, e.what());
        }
    }

    out << "  </g>\n";

    // --- Layer 5: Settlement labels ---
    out << "  <g id=\"labels\" font-family=\"sans-serif\" fill=\"#333\">\n";
    for (const auto& s : settlements) {
        float fontSize = (s.type == "town") ? 80.0f :
                         (s.type == "village" || s.type == "fishing_village") ? 50.0f : 35.0f;
        out << "    <text x=\"" << s.position.x << "\" y=\"" << (s.position.y - s.radius - 20.0f)
            << "\" font-size=\"" << fontSize
            << "\" text-anchor=\"middle\">#" << s.id << " " << s.type << "</text>\n";
    }
    out << "  </g>\n";

    out << "</svg>\n";
    out.close();

    SDL_Log("Wrote full-map SVG: %s (%zu settlements, %zu roads, %zu rivers, %zu lakes)",
            outputPath.c_str(), settlements.size(), roads.size(), rivers.size(), lakes.size());

    return 0;
}
