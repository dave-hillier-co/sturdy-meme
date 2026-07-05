/**
 * World Preview
 *
 * Composites all generated world data into a single overview image so the
 * whole pipeline can be validated in seconds without running the app:
 * hillshaded heightmap, sea, biome tint, rivers, lakes, roads and labeled
 * settlements.
 *
 * Inputs (all in content space, 0..terrain-size):
 * - Heightmap (16-bit PNG)
 * - <terrain-data>/biome/biome_map.png   (R=zone, G=subzone, B=settlement proximity)
 * - <terrain-data>/biome/settlements.json
 * - <terrain-data>/watershed/rivers.geojson, lakes.geojson
 * - <terrain-data>/roads/roads.geojson
 *
 * Outputs:
 * - world_preview.png  (composite raster)
 * - world_preview.svg  (vector overlay with settlement labels)
 *
 * Missing inputs degrade gracefully (layer is skipped with a warning) so the
 * preview works after any pipeline stage; only the heightmap is required.
 */

#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <glm/glm.hpp>

#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

namespace {

struct Config {
    std::string heightmapPath;
    std::string terrainDataDir;
    std::string outputDir;

    float terrainSize = 16384.0f;
    float seaLevel = 23.0f;
    float minAltitude = -15.0f;
    float maxAltitude = 220.0f;

    uint32_t imageSize = 1024;
};

struct Heightmap {
    std::vector<uint16_t> data;
    uint32_t width = 0;
    uint32_t height = 0;

    float sample(float u, float v) const {
        u = glm::clamp(u, 0.0f, 1.0f);
        v = glm::clamp(v, 0.0f, 1.0f);
        float fx = u * (width - 1);
        float fy = v * (height - 1);
        uint32_t x0 = static_cast<uint32_t>(fx);
        uint32_t y0 = static_cast<uint32_t>(fy);
        uint32_t x1 = std::min(x0 + 1, width - 1);
        uint32_t y1 = std::min(y0 + 1, height - 1);
        float tx = fx - x0;
        float ty = fy - y0;
        float h00 = data[y0 * width + x0] / 65535.0f;
        float h10 = data[y0 * width + x1] / 65535.0f;
        float h01 = data[y1 * width + x0] / 65535.0f;
        float h11 = data[y1 * width + x1] / 65535.0f;
        return glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), ty);
    }
};

struct BiomeMap {
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;

    uint8_t zoneAt(float u, float v) const {
        uint32_t x = std::min(static_cast<uint32_t>(u * width), width - 1);
        uint32_t y = std::min(static_cast<uint32_t>(v * height), height - 1);
        return rgba[(y * width + x) * 4 + 0];
    }
};

struct Polyline {
    std::vector<glm::vec2> points;  // Content space
    float width = 5.0f;             // Meters
};

struct SettlementInfo {
    uint32_t id = 0;
    std::string type;
    glm::vec2 pos{0.0f};  // Content space
    float radius = 0.0f;
};

struct Image {
    std::vector<uint8_t> rgba;
    uint32_t size = 0;

    void init(uint32_t s) {
        size = s;
        rgba.assign(size * size * 4, 255);
    }

    void set(uint32_t x, uint32_t y, glm::vec3 color) {
        if (x >= size || y >= size) return;
        size_t i = (y * size + x) * 4;
        rgba[i + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
        rgba[i + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
        rgba[i + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
        rgba[i + 3] = 255;
    }

    glm::vec3 get(uint32_t x, uint32_t y) const {
        size_t i = (y * size + x) * 4;
        return glm::vec3(rgba[i], rgba[i + 1], rgba[i + 2]) / 255.0f;
    }

    void blend(uint32_t x, uint32_t y, glm::vec3 color, float alpha) {
        if (x >= size || y >= size) return;
        set(x, y, glm::mix(get(x, y), color, glm::clamp(alpha, 0.0f, 1.0f)));
    }
};

bool loadHeightmap(const std::string& path, Heightmap& hm) {
    std::vector<unsigned char> pngData;
    unsigned w, h;
    unsigned error = lodepng::decode(pngData, w, h, path, LCT_GREY, 16);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }
    hm.width = w;
    hm.height = h;
    hm.data.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < hm.data.size(); ++i) {
        hm.data[i] = static_cast<uint16_t>((pngData[i * 2] << 8) | pngData[i * 2 + 1]);
    }
    SDL_Log("Loaded heightmap: %ux%u", w, h);
    return true;
}

bool loadBiomeMap(const std::string& path, BiomeMap& bm) {
    unsigned w, h;
    unsigned error = lodepng::decode(bm.rgba, w, h, path, LCT_RGBA, 8);
    if (error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No biome map (%s): %s",
                    path.c_str(), lodepng_error_text(error));
        return false;
    }
    bm.width = w;
    bm.height = h;
    SDL_Log("Loaded biome map: %ux%u", w, h);
    return true;
}

std::vector<Polyline> loadLineStrings(const std::string& path, const char* what) {
    std::vector<Polyline> lines;
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No %s (%s)", what, path.c_str());
        return lines;
    }
    try {
        json j = json::parse(file);
        for (const auto& feature : j.value("features", json::array())) {
            if (feature["geometry"]["type"] != "LineString") continue;
            Polyline line;
            for (const auto& coord : feature["geometry"]["coordinates"]) {
                line.points.emplace_back(coord[0].get<float>(), coord[1].get<float>());
            }
            if (feature.contains("properties")) {
                line.width = feature["properties"].value("width", 5.0f);
            }
            if (line.points.size() >= 2) lines.push_back(std::move(line));
        }
        SDL_Log("Loaded %zu %s from %s", lines.size(), what, path.c_str());
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse %s: %s", path.c_str(), e.what());
    }
    return lines;
}

// Building footprint centroids (content space) from town_<id>.geojson layouts
std::vector<glm::vec2> loadTownBuildings(const std::string& townsDir,
                                         const std::vector<SettlementInfo>& settlements) {
    std::vector<glm::vec2> centroids;
    for (const auto& s : settlements) {
        std::string path = townsDir + "/town_" + std::to_string(s.id) + ".geojson";
        std::ifstream file(path);
        if (!file.is_open()) continue;
        try {
            json j = json::parse(file);
            for (const auto& feature : j.value("features", json::array())) {
                if (feature["properties"].value("kind", "") != "building") continue;
                if (feature["geometry"]["type"] != "Polygon") continue;
                const auto& rings = feature["geometry"]["coordinates"];
                if (rings.empty() || rings[0].empty()) continue;
                glm::vec2 sum(0.0f);
                for (const auto& coord : rings[0]) {
                    sum += glm::vec2(coord[0].get<float>(), coord[1].get<float>());
                }
                centroids.push_back(sum / static_cast<float>(rings[0].size()));
            }
        } catch (const std::exception& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse %s: %s",
                        path.c_str(), e.what());
        }
    }
    if (!centroids.empty()) {
        SDL_Log("Loaded %zu town building footprints", centroids.size());
    }
    return centroids;
}

std::vector<SettlementInfo> loadSettlements(const std::string& path) {
    std::vector<SettlementInfo> settlements;
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No settlements (%s)", path.c_str());
        return settlements;
    }
    try {
        json j = json::parse(file);
        for (const auto& s : j.value("settlements", json::array())) {
            SettlementInfo info;
            info.id = s.value("id", 0u);
            info.type = s.value("type", "Settlement");
            info.pos = glm::vec2(s.value("x", 0.0f), s.value("z", 0.0f));
            info.radius = s.value("radius", 0.0f);
            settlements.push_back(std::move(info));
        }
        SDL_Log("Loaded %zu settlements from %s", settlements.size(), path.c_str());
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse %s: %s", path.c_str(), e.what());
    }
    return settlements;
}

// Biome zone ids from tools/biome_preprocess/BiomeGenerator.h (BiomeZone)
glm::vec3 biomeColor(uint8_t zone) {
    switch (zone) {
        case 0: return {0.25f, 0.45f, 0.75f};  // Sea
        case 1: return {0.93f, 0.87f, 0.69f};  // Beach
        case 2: return {0.92f, 0.92f, 0.88f};  // ChalkCliff
        case 3: return {0.60f, 0.65f, 0.55f};  // SaltMarsh
        case 4: return {0.35f, 0.55f, 0.80f};  // River
        case 5: return {0.45f, 0.60f, 0.50f};  // Wetland
        case 6: return {0.65f, 0.75f, 0.45f};  // Grassland
        case 7: return {0.80f, 0.75f, 0.50f};  // Agricultural
        case 8: return {0.25f, 0.50f, 0.25f};  // Woodland
        default: return {1.00f, 0.00f, 1.00f}; // Unknown - loud magenta
    }
}

void drawPolyline(Image& img, const Polyline& line, glm::vec3 color, float minPixelWidth,
                  float terrainSize) {
    float scale = img.size / terrainSize;
    for (size_t i = 0; i + 1 < line.points.size(); ++i) {
        glm::vec2 a = line.points[i] * scale;
        glm::vec2 b = line.points[i + 1] * scale;
        float halfWidth = std::max(line.width * scale * 0.5f, minPixelWidth * 0.5f);

        int minX = static_cast<int>(std::floor(std::min(a.x, b.x) - halfWidth));
        int maxX = static_cast<int>(std::ceil(std::max(a.x, b.x) + halfWidth));
        int minY = static_cast<int>(std::floor(std::min(a.y, b.y) - halfWidth));
        int maxY = static_cast<int>(std::ceil(std::max(a.y, b.y) + halfWidth));

        glm::vec2 ab = b - a;
        float abLen2 = glm::dot(ab, ab);

        for (int y = std::max(minY, 0); y <= maxY && y < static_cast<int>(img.size); ++y) {
            for (int x = std::max(minX, 0); x <= maxX && x < static_cast<int>(img.size); ++x) {
                glm::vec2 p(x + 0.5f, y + 0.5f);
                float t = abLen2 > 0.0f ? glm::clamp(glm::dot(p - a, ab) / abLen2, 0.0f, 1.0f) : 0.0f;
                float dist = glm::length(p - (a + t * ab));
                if (dist <= halfWidth) {
                    img.set(x, y, color);
                }
            }
        }
    }
}

void drawCircleOutline(Image& img, glm::vec2 centerContent, float radiusMeters, glm::vec3 color,
                       float terrainSize) {
    float scale = img.size / terrainSize;
    glm::vec2 c = centerContent * scale;
    float r = std::max(radiusMeters * scale, 4.0f);
    int steps = static_cast<int>(r * 8.0f) + 16;
    for (int i = 0; i < steps; ++i) {
        float angle = (2.0f * 3.14159265f * i) / steps;
        int x = static_cast<int>(c.x + std::cos(angle) * r);
        int y = static_cast<int>(c.y + std::sin(angle) * r);
        img.set(x, y, color);
        img.set(x + 1, y, color);
        img.set(x, y + 1, color);
    }
    // Center dot
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            img.set(static_cast<int>(c.x) + dx, static_cast<int>(c.y) + dy, color);
}

void writeSvg(const std::string& path, const Config& cfg,
              const std::vector<Polyline>& rivers, const std::vector<Polyline>& roads,
              const std::vector<SettlementInfo>& settlements) {
    std::ofstream out(path);
    if (!out.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SVG: %s", path.c_str());
        return;
    }

    const float svgSize = 2048.0f;
    float scale = svgSize / cfg.terrainSize;

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << svgSize << "\" height=\""
        << svgSize << "\" viewBox=\"0 0 " << svgSize << " " << svgSize << "\">\n";
    out << "  <image href=\"world_preview.png\" x=\"0\" y=\"0\" width=\"" << svgSize
        << "\" height=\"" << svgSize << "\"/>\n";

    auto emitLines = [&](const std::vector<Polyline>& lines, const char* id, const char* stroke) {
        out << "  <g id=\"" << id << "\" fill=\"none\" stroke=\"" << stroke
            << "\" stroke-opacity=\"0.7\">\n";
        for (const auto& line : lines) {
            out << "    <path stroke-width=\""
                << glm::clamp(line.width * scale, 0.75f, 6.0f) << "\" d=\"M";
            for (size_t i = 0; i < line.points.size(); ++i) {
                glm::vec2 p = line.points[i] * scale;
                out << (i > 0 ? " L" : "") << p.x << "," << p.y;
            }
            out << "\"/>\n";
        }
        out << "  </g>\n";
    };

    emitLines(rivers, "rivers", "#2060a0");
    emitLines(roads, "roads", "#c06020");

    out << "  <g id=\"settlements\">\n";
    for (const auto& s : settlements) {
        glm::vec2 p = s.pos * scale;
        float r = std::max(s.radius * scale, 5.0f);
        out << "    <circle cx=\"" << p.x << "\" cy=\"" << p.y << "\" r=\"" << r
            << "\" fill=\"none\" stroke=\"#d02020\" stroke-width=\"2\"/>\n";
        out << "    <text x=\"" << p.x << "\" y=\"" << (p.y - r - 4)
            << "\" font-family=\"sans-serif\" font-size=\"14\" text-anchor=\"middle\" "
            << "fill=\"#ffffff\" stroke=\"#000000\" stroke-width=\"0.4\">"
            << s.type << " " << s.id << "</text>\n";
    }
    out << "  </g>\n";
    out << "</svg>\n";
    SDL_Log("Saved SVG: %s", path.c_str());
}

void printUsage(const char* prog) {
    SDL_Log("Usage: %s --heightmap <path> --terrain-data <dir> [options]", prog);
    SDL_Log("Options:");
    SDL_Log("  --output <dir>       Output directory (default: terrain-data dir)");
    SDL_Log("  --terrain-size <m>   Terrain size in meters (default: 16384)");
    SDL_Log("  --sea-level <m>      Sea level in meters (default: 23)");
    SDL_Log("  --min-alt <m>        Minimum altitude in meters (default: -15)");
    SDL_Log("  --max-alt <m>        Maximum altitude in meters (default: 220)");
    SDL_Log("  --image-size <px>    Composite PNG size (default: 1024)");
}

bool parseArgs(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return false; }
        else if (arg == "--heightmap" && i + 1 < argc) cfg.heightmapPath = argv[++i];
        else if (arg == "--terrain-data" && i + 1 < argc) cfg.terrainDataDir = argv[++i];
        else if (arg == "--output" && i + 1 < argc) cfg.outputDir = argv[++i];
        else if (arg == "--terrain-size" && i + 1 < argc) cfg.terrainSize = std::stof(argv[++i]);
        else if (arg == "--sea-level" && i + 1 < argc) cfg.seaLevel = std::stof(argv[++i]);
        else if (arg == "--min-alt" && i + 1 < argc) cfg.minAltitude = std::stof(argv[++i]);
        else if (arg == "--max-alt" && i + 1 < argc) cfg.maxAltitude = std::stof(argv[++i]);
        else if (arg == "--image-size" && i + 1 < argc) cfg.imageSize = std::stoul(argv[++i]);
    }
    if (cfg.heightmapPath.empty() || cfg.terrainDataDir.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--heightmap and --terrain-data are required");
        printUsage(argv[0]);
        return false;
    }
    if (cfg.outputDir.empty()) cfg.outputDir = cfg.terrainDataDir;
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    Config cfg;
    if (!parseArgs(argc, argv, cfg)) return 1;

    Heightmap hm;
    if (!loadHeightmap(cfg.heightmapPath, hm)) return 1;

    BiomeMap biome;
    bool hasBiome = loadBiomeMap(cfg.terrainDataDir + "/biome/biome_map.png", biome);

    auto rivers = loadLineStrings(cfg.terrainDataDir + "/watershed/rivers.geojson", "rivers");
    auto lakes = loadLineStrings(cfg.terrainDataDir + "/watershed/lakes.geojson", "lakes");
    auto roads = loadLineStrings(cfg.terrainDataDir + "/roads/roads.geojson", "roads");
    auto settlements = loadSettlements(cfg.terrainDataDir + "/biome/settlements.json");
    auto townBuildings = loadTownBuildings(cfg.terrainDataDir + "/towns", settlements);

    Image img;
    img.init(cfg.imageSize);

    float heightScale = cfg.maxAltitude - cfg.minAltitude;

    // Base layer: sea, then biome tint (or plain height tint) shaded by hillshade
    for (uint32_t y = 0; y < img.size; ++y) {
        for (uint32_t x = 0; x < img.size; ++x) {
            float u = (x + 0.5f) / img.size;
            float v = (y + 0.5f) / img.size;

            float h = hm.sample(u, v);
            float altitude = cfg.minAltitude + h * heightScale;

            if (altitude <= cfg.seaLevel) {
                // Deeper water is darker
                float depth = glm::clamp((cfg.seaLevel - altitude) / 30.0f, 0.0f, 1.0f);
                img.set(x, y, glm::mix(glm::vec3(0.35f, 0.55f, 0.80f),
                                       glm::vec3(0.10f, 0.25f, 0.50f), depth));
                continue;
            }

            // Hillshade: light from the northwest
            float texel = 1.0f / img.size;
            float hL = hm.sample(u - texel, v);
            float hR = hm.sample(u + texel, v);
            float hU = hm.sample(u, v - texel);
            float hD = hm.sample(u, v + texel);
            float metersPerPixel = cfg.terrainSize / img.size;
            glm::vec3 normal = glm::normalize(glm::vec3(
                (hL - hR) * heightScale / (2.0f * metersPerPixel),
                1.0f,
                (hU - hD) * heightScale / (2.0f * metersPerPixel)));
            glm::vec3 lightDir = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.5f));
            float shade = glm::clamp(glm::dot(normal, lightDir), 0.0f, 1.0f);
            shade = 0.4f + 0.6f * shade;

            glm::vec3 base;
            if (hasBiome) {
                base = biomeColor(biome.zoneAt(u, v));
            } else {
                float t = glm::clamp((altitude - cfg.seaLevel) / (cfg.maxAltitude - cfg.seaLevel),
                                     0.0f, 1.0f);
                base = glm::mix(glm::vec3(0.55f, 0.70f, 0.40f), glm::vec3(0.85f, 0.85f, 0.80f), t);
            }
            img.set(x, y, base * shade);
        }
    }

    // Vector layers
    for (const auto& lake : lakes) drawPolyline(img, lake, {0.20f, 0.40f, 0.70f}, 2.0f, cfg.terrainSize);
    for (const auto& river : rivers) drawPolyline(img, river, {0.15f, 0.35f, 0.70f}, 1.0f, cfg.terrainSize);
    for (const auto& road : roads) drawPolyline(img, road, {0.75f, 0.40f, 0.15f}, 1.0f, cfg.terrainSize);
    float buildingScale = img.size / cfg.terrainSize;
    for (const auto& b : townBuildings) {
        img.set(static_cast<uint32_t>(b.x * buildingScale),
                static_cast<uint32_t>(b.y * buildingScale), {0.30f, 0.15f, 0.10f});
    }
    for (const auto& s : settlements) {
        drawCircleOutline(img, s.pos, s.radius, {0.85f, 0.10f, 0.10f}, cfg.terrainSize);
    }

    std::string pngPath = cfg.outputDir + "/world_preview.png";
    unsigned error = lodepng::encode(pngPath, img.rgba, img.size, img.size);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save %s: %s",
                     pngPath.c_str(), lodepng_error_text(error));
        return 1;
    }
    SDL_Log("Saved PNG: %s", pngPath.c_str());

    writeSvg(cfg.outputDir + "/world_preview.svg", cfg, rivers, roads, settlements);

    SDL_Log("World preview complete: %zu rivers, %zu lakes, %zu roads, %zu settlements%s",
            rivers.size(), lakes.size(), roads.size(), settlements.size(),
            hasBiome ? "" : " (no biome map)");
    return 0;
}
