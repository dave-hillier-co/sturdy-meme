/**
 * Terrain Patch Generator
 *
 * Generates terrain-aware Voronoi patches for town placement.
 * Takes heightmap, river data, and settlement location as input.
 * Outputs SVG preview showing patches with natural boundaries.
 *
 * Natural boundaries considered:
 * - Coastlines (sea level threshold)
 * - Rivers (from GeoJSON)
 * - Terrain terraces (slope discontinuities)
 */

#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <random>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <memory>

using json = nlohmann::json;

// ============================================================================
// Configuration
// ============================================================================

struct Config {
    std::string heightmapPath;
    std::string riversPath;
    std::string outputPath = "terrain_patches.svg";

    // Settlement center position (world coordinates)
    float centerX = 0.0f;
    float centerZ = 0.0f;
    float radius = 500.0f;  // Town radius in meters

    // Terrain parameters
    float terrainSize = 16384.0f;
    float seaLevel = 0.0f;         // Height below which is water
    float minAltitude = -15.0f;
    float maxAltitude = 200.0f;

    // Patch generation
    int numPatches = 15;
    int seed = -1;

    // SVG output
    float svgWidth = 1024.0f;
    float svgHeight = 1024.0f;
};

// ============================================================================
// Data Structures
// ============================================================================

struct RiverSegment {
    std::vector<glm::vec2> points;  // World XZ coordinates
    std::vector<float> widths;
    float flow = 0.0f;
};

struct Heightmap {
    std::vector<uint16_t> data;
    uint32_t width = 0;
    uint32_t height = 0;

    float sample(float u, float v) const {
        if (data.empty()) return 0.0f;

        u = glm::clamp(u, 0.0f, 1.0f);
        v = glm::clamp(v, 0.0f, 1.0f);

        float fx = u * (width - 1);
        float fy = v * (height - 1);

        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, static_cast<int>(width - 1));
        int y1 = std::min(y0 + 1, static_cast<int>(height - 1));

        float tx = fx - x0;
        float ty = fy - y0;

        float h00 = data[y0 * width + x0] / 65535.0f;
        float h10 = data[y0 * width + x1] / 65535.0f;
        float h01 = data[y1 * width + x0] / 65535.0f;
        float h11 = data[y1 * width + x1] / 65535.0f;

        float h0 = h00 * (1.0f - tx) + h10 * tx;
        float h1 = h01 * (1.0f - tx) + h11 * tx;

        return h0 * (1.0f - ty) + h1 * ty;
    }

    float sampleWorld(float worldX, float worldZ, float terrainSize) const {
        float u = worldX / terrainSize;
        float v = worldZ / terrainSize;
        return sample(u, v);
    }

    float toWorldHeight(float normalizedHeight, float minAlt, float maxAlt) const {
        return minAlt + normalizedHeight * (maxAlt - minAlt);
    }
};

struct TerrainPatch {
    std::vector<glm::vec2> vertices;  // Polygon vertices in local coords
    glm::vec2 center;
    float avgHeight = 0.0f;
    float avgSlope = 0.0f;
    bool isWater = false;
    bool bordersWater = false;
    bool bordersRiver = false;
    int id = 0;
};

// ============================================================================
// Heightmap Loading
// ============================================================================

bool loadHeightmap(const std::string& path, Heightmap& hm) {
    std::vector<unsigned char> pngData;
    unsigned w, h;

    unsigned error = lodepng::decode(pngData, w, h, path, LCT_GREY, 16);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to load heightmap %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    hm.width = w;
    hm.height = h;
    hm.data.resize(w * h);

    // Convert from big-endian 16-bit
    for (uint32_t i = 0; i < w * h; ++i) {
        uint16_t hi = pngData[i * 2];
        uint16_t lo = pngData[i * 2 + 1];
        hm.data[i] = (hi << 8) | lo;
    }

    SDL_Log("Loaded heightmap: %ux%u", w, h);
    return true;
}

// ============================================================================
// River Loading (GeoJSON)
// ============================================================================

std::vector<RiverSegment> loadRivers(const std::string& path) {
    std::vector<RiverSegment> rivers;

    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open rivers file: %s", path.c_str());
        return rivers;
    }

    try {
        json geojson = json::parse(file);

        if (geojson["type"] != "FeatureCollection") {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Rivers file is not a FeatureCollection");
            return rivers;
        }

        for (const auto& feature : geojson["features"]) {
            if (feature["geometry"]["type"] != "LineString") {
                continue;
            }

            RiverSegment seg;

            const auto& coords = feature["geometry"]["coordinates"];
            for (const auto& coord : coords) {
                float x = coord[0].get<float>();
                float z = coord[1].get<float>();
                seg.points.emplace_back(x, z);
            }

            // Load widths if available
            if (feature.contains("properties")) {
                const auto& props = feature["properties"];
                if (props.contains("widths") && props["widths"].is_array()) {
                    for (const auto& w : props["widths"]) {
                        seg.widths.push_back(w.get<float>());
                    }
                }
                if (props.contains("flow")) {
                    seg.flow = props["flow"].get<float>();
                }
            }

            // Default widths if not provided
            while (seg.widths.size() < seg.points.size()) {
                seg.widths.push_back(5.0f);  // Default 5m width
            }

            if (seg.points.size() >= 2) {
                rivers.push_back(std::move(seg));
            }
        }

        SDL_Log("Loaded %zu river segments", rivers.size());

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to parse rivers GeoJSON: %s", e.what());
    }

    return rivers;
}

// ============================================================================
// Coastline Detection
// ============================================================================

struct Coastline {
    std::vector<std::vector<glm::vec2>> contours;
};

Coastline detectCoastline(const Heightmap& hm, const Config& cfg,
                          glm::vec2 center, float radius) {
    Coastline coast;

    // Sample area around the settlement
    float margin = radius * 1.5f;
    float minX = center.x - margin;
    float maxX = center.x + margin;
    float minZ = center.y - margin;
    float maxZ = center.y + margin;

    // Convert sea level to normalized height
    float heightScale = cfg.maxAltitude - cfg.minAltitude;
    float seaNorm = (cfg.seaLevel - cfg.minAltitude) / heightScale;

    // Sample grid for marching squares
    int gridRes = 64;
    float cellSize = (2.0f * margin) / gridRes;

    std::vector<std::vector<bool>> isLand(gridRes + 1, std::vector<bool>(gridRes + 1));

    for (int j = 0; j <= gridRes; ++j) {
        for (int i = 0; i <= gridRes; ++i) {
            float worldX = minX + i * cellSize;
            float worldZ = minZ + j * cellSize;
            float h = hm.sampleWorld(worldX, worldZ, cfg.terrainSize);
            isLand[j][i] = (h > seaNorm);
        }
    }

    // Simple contour extraction - find boundary edges
    std::vector<glm::vec2> boundaryPoints;

    for (int j = 0; j < gridRes; ++j) {
        for (int i = 0; i < gridRes; ++i) {
            // Check for land/water transitions
            bool bl = isLand[j][i];
            bool br = isLand[j][i + 1];
            bool tl = isLand[j + 1][i];
            bool tr = isLand[j + 1][i + 1];

            int count = (bl ? 1 : 0) + (br ? 1 : 0) + (tl ? 1 : 0) + (tr ? 1 : 0);

            if (count > 0 && count < 4) {
                // Mixed cell - on boundary
                float worldX = minX + (i + 0.5f) * cellSize;
                float worldZ = minZ + (j + 0.5f) * cellSize;
                boundaryPoints.emplace_back(worldX, worldZ);
            }
        }
    }

    if (!boundaryPoints.empty()) {
        coast.contours.push_back(std::move(boundaryPoints));
    }

    SDL_Log("Detected coastline with %zu contour segments", coast.contours.size());

    return coast;
}

// ============================================================================
// Slope Analysis
// ============================================================================

float computeSlope(const Heightmap& hm, float worldX, float worldZ,
                   const Config& cfg) {
    float eps = cfg.terrainSize / hm.width;  // One pixel in world coords

    float hC = hm.sampleWorld(worldX, worldZ, cfg.terrainSize);
    float hL = hm.sampleWorld(worldX - eps, worldZ, cfg.terrainSize);
    float hR = hm.sampleWorld(worldX + eps, worldZ, cfg.terrainSize);
    float hU = hm.sampleWorld(worldX, worldZ - eps, cfg.terrainSize);
    float hD = hm.sampleWorld(worldX, worldZ + eps, cfg.terrainSize);

    float heightScale = cfg.maxAltitude - cfg.minAltitude;

    float dhdx = (hR - hL) * heightScale / (2.0f * eps);
    float dhdz = (hD - hU) * heightScale / (2.0f * eps);

    return std::sqrt(dhdx * dhdx + dhdz * dhdz);
}

// ============================================================================
// Voronoi Patch Generation
// ============================================================================

struct VoronoiSeed {
    glm::vec2 pos;
    int id;
};

// Simple distance-to-segment function
float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    glm::vec2 ab = b - a;
    glm::vec2 ap = p - a;

    float t = glm::dot(ap, ab) / glm::dot(ab, ab);
    t = glm::clamp(t, 0.0f, 1.0f);

    glm::vec2 closest = a + t * ab;
    return glm::length(p - closest);
}

float distanceToRiver(glm::vec2 p, const std::vector<RiverSegment>& rivers) {
    float minDist = std::numeric_limits<float>::max();

    for (const auto& river : rivers) {
        for (size_t i = 0; i + 1 < river.points.size(); ++i) {
            float d = distanceToSegment(p, river.points[i], river.points[i + 1]);
            minDist = std::min(minDist, d);
        }
    }

    return minDist;
}

std::vector<VoronoiSeed> generateSeeds(const Config& cfg,
                                        const Heightmap& hm,
                                        const std::vector<RiverSegment>& rivers,
                                        const Coastline& coast) {
    std::vector<VoronoiSeed> seeds;

    std::mt19937 rng(cfg.seed > 0 ? cfg.seed : std::random_device{}());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    glm::vec2 center(cfg.centerX, cfg.centerZ);

    // Generate seeds using Poisson-like distribution
    // Bias towards flat areas and away from water

    float heightScale = cfg.maxAltitude - cfg.minAltitude;
    float seaNorm = (cfg.seaLevel - cfg.minAltitude) / heightScale;

    int attempts = 0;
    int maxAttempts = cfg.numPatches * 100;

    // First seed at center
    {
        VoronoiSeed s;
        s.pos = center;
        s.id = 0;

        float h = hm.sampleWorld(s.pos.x, s.pos.y, cfg.terrainSize);
        if (h > seaNorm) {
            seeds.push_back(s);
        }
    }

    // Spiral pattern for remaining seeds
    float baseAngle = dist(rng) * glm::pi<float>();

    while (seeds.size() < static_cast<size_t>(cfg.numPatches) && attempts < maxAttempts) {
        attempts++;

        // Spiral outward from center
        float t = static_cast<float>(seeds.size()) / cfg.numPatches;
        float r = cfg.radius * std::sqrt(t) * (0.8f + 0.4f * dist(rng));
        float angle = baseAngle + std::sqrt(static_cast<float>(seeds.size())) * 2.4f;
        angle += dist(rng) * 0.3f;

        glm::vec2 pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * r;

        // Check if on land
        float h = hm.sampleWorld(pos.x, pos.y, cfg.terrainSize);
        if (h <= seaNorm) {
            continue;  // In water
        }

        // Check distance from rivers - don't place seeds too close
        float riverDist = distanceToRiver(pos, rivers);
        if (riverDist < 20.0f) {
            continue;  // Too close to river
        }

        // Check minimum distance from other seeds
        float minSeedDist = cfg.radius / std::sqrt(static_cast<float>(cfg.numPatches)) * 0.5f;
        bool tooClose = false;
        for (const auto& existing : seeds) {
            if (glm::length(pos - existing.pos) < minSeedDist) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) {
            continue;
        }

        // Prefer flatter areas
        float slope = computeSlope(hm, pos.x, pos.y, cfg);
        if (slope > 0.5f && dist(rng) > 0.3f) {
            continue;  // Too steep, mostly reject
        }

        VoronoiSeed s;
        s.pos = pos;
        s.id = static_cast<int>(seeds.size());
        seeds.push_back(s);
    }

    SDL_Log("Generated %zu patch seeds", seeds.size());

    return seeds;
}

// Simple Voronoi cell computation using brute force
std::vector<TerrainPatch> computeVoronoiPatches(
    const std::vector<VoronoiSeed>& seeds,
    const Config& cfg,
    const Heightmap& hm,
    const std::vector<RiverSegment>& rivers) {

    std::vector<TerrainPatch> patches;

    if (seeds.empty()) {
        return patches;
    }

    glm::vec2 center(cfg.centerX, cfg.centerZ);
    float margin = cfg.radius * 1.5f;

    // For each seed, compute its Voronoi cell by sampling
    int resolution = 128;
    float cellSize = (2.0f * margin) / resolution;

    // Grid of which seed owns each cell
    std::vector<std::vector<int>> ownership(resolution, std::vector<int>(resolution, -1));

    float minX = center.x - margin;
    float minZ = center.y - margin;

    float heightScale = cfg.maxAltitude - cfg.minAltitude;
    float seaNorm = (cfg.seaLevel - cfg.minAltitude) / heightScale;

    // Assign each grid cell to nearest seed
    for (int j = 0; j < resolution; ++j) {
        for (int i = 0; i < resolution; ++i) {
            float worldX = minX + (i + 0.5f) * cellSize;
            float worldZ = minZ + (j + 0.5f) * cellSize;
            glm::vec2 p(worldX, worldZ);

            // Check if in water
            float h = hm.sampleWorld(worldX, worldZ, cfg.terrainSize);
            if (h <= seaNorm) {
                ownership[j][i] = -2;  // Water
                continue;
            }

            // Check if within radius
            if (glm::length(p - center) > cfg.radius * 1.3f) {
                continue;
            }

            // Find nearest seed
            float minDist = std::numeric_limits<float>::max();
            int nearest = -1;

            for (const auto& seed : seeds) {
                float d = glm::length(p - seed.pos);
                if (d < minDist) {
                    minDist = d;
                    nearest = seed.id;
                }
            }

            ownership[j][i] = nearest;
        }
    }

    // Extract patch boundaries using marching squares-like approach
    for (const auto& seed : seeds) {
        TerrainPatch patch;
        patch.id = seed.id;
        patch.center = seed.pos;

        // Collect boundary points
        std::vector<glm::vec2> boundaryPoints;

        for (int j = 0; j < resolution - 1; ++j) {
            for (int i = 0; i < resolution - 1; ++i) {
                int o00 = ownership[j][i];
                int o10 = ownership[j][i + 1];
                int o01 = ownership[j + 1][i];
                int o11 = ownership[j + 1][i + 1];

                // Check if this cell is on boundary of this patch
                bool hasThis = (o00 == seed.id) || (o10 == seed.id) ||
                               (o01 == seed.id) || (o11 == seed.id);
                bool hasOther = (o00 != seed.id && o00 >= -1) ||
                                (o10 != seed.id && o10 >= -1) ||
                                (o01 != seed.id && o01 >= -1) ||
                                (o11 != seed.id && o11 >= -1);

                if (hasThis && hasOther) {
                    float worldX = minX + (i + 0.5f) * cellSize;
                    float worldZ = minZ + (j + 0.5f) * cellSize;
                    boundaryPoints.emplace_back(worldX, worldZ);
                }

                // Check for water boundary
                bool hasWater = (o00 == -2) || (o10 == -2) || (o01 == -2) || (o11 == -2);
                if (hasThis && hasWater) {
                    patch.bordersWater = true;
                }
            }
        }

        if (boundaryPoints.empty()) {
            continue;
        }

        // Sort boundary points by angle from center
        std::sort(boundaryPoints.begin(), boundaryPoints.end(),
            [&seed](const glm::vec2& a, const glm::vec2& b) {
                float angleA = std::atan2(a.y - seed.pos.y, a.x - seed.pos.x);
                float angleB = std::atan2(b.y - seed.pos.y, b.x - seed.pos.x);
                return angleA < angleB;
            });

        patch.vertices = std::move(boundaryPoints);

        // Compute average height and slope
        float h = hm.sampleWorld(seed.pos.x, seed.pos.y, cfg.terrainSize);
        patch.avgHeight = cfg.minAltitude + h * heightScale;
        patch.avgSlope = computeSlope(hm, seed.pos.x, seed.pos.y, cfg);

        // Check if borders river
        float riverDist = distanceToRiver(seed.pos, rivers);
        patch.bordersRiver = (riverDist < cfg.radius / cfg.numPatches);

        patches.push_back(std::move(patch));
    }

    SDL_Log("Computed %zu Voronoi patches", patches.size());

    return patches;
}

// ============================================================================
// SVG Output
// ============================================================================

std::string colorForPatch(const TerrainPatch& patch) {
    if (patch.bordersWater) {
        return "#a0c4e8";  // Light blue for waterfront
    }
    if (patch.bordersRiver) {
        return "#90d4a8";  // Light green for riverside
    }

    // Color by height - brown to green gradient
    float t = glm::clamp((patch.avgHeight + 15.0f) / 100.0f, 0.0f, 1.0f);

    int r = static_cast<int>(180 - t * 60);
    int g = static_cast<int>(160 + t * 40);
    int b = static_cast<int>(120 - t * 40);

    char buf[16];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return buf;
}

void saveSVG(const std::string& path,
             const Config& cfg,
             const std::vector<TerrainPatch>& patches,
             const std::vector<RiverSegment>& rivers,
             const Coastline& coast) {

    std::ofstream out(path);
    if (!out.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create SVG file: %s", path.c_str());
        return;
    }

    glm::vec2 center(cfg.centerX, cfg.centerZ);
    float margin = cfg.radius * 1.5f;

    // Transform from world coords to SVG coords
    auto toSVG = [&](glm::vec2 world) -> glm::vec2 {
        float x = (world.x - center.x + margin) / (2.0f * margin) * cfg.svgWidth;
        float y = (world.y - center.y + margin) / (2.0f * margin) * cfg.svgHeight;
        return glm::vec2(x, y);
    };

    // SVG header
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << cfg.svgWidth << "\" height=\"" << cfg.svgHeight << "\" "
        << "viewBox=\"0 0 " << cfg.svgWidth << " " << cfg.svgHeight << "\">\n";

    // Background
    out << "  <rect width=\"100%\" height=\"100%\" fill=\"#e8e0d0\"/>\n";

    // Title
    out << "  <text x=\"10\" y=\"25\" font-family=\"sans-serif\" font-size=\"14\" "
        << "fill=\"#333\">Terrain Patches - Center: ("
        << cfg.centerX << ", " << cfg.centerZ << ") Radius: " << cfg.radius << "m</text>\n";

    // Water/coast areas (simplified - just mark approximate water region)
    out << "  <g id=\"water\" opacity=\"0.5\">\n";
    for (const auto& contour : coast.contours) {
        if (contour.size() < 3) continue;

        out << "    <path d=\"M";
        for (size_t i = 0; i < contour.size(); ++i) {
            glm::vec2 p = toSVG(contour[i]);
            out << (i > 0 ? " L" : "") << p.x << "," << p.y;
        }
        out << "\" fill=\"#6090c0\" stroke=\"#4070a0\" stroke-width=\"1\"/>\n";
    }
    out << "  </g>\n";

    // Patches
    out << "  <g id=\"patches\">\n";
    for (const auto& patch : patches) {
        if (patch.vertices.size() < 3) continue;

        std::string color = colorForPatch(patch);

        out << "    <path d=\"M";
        for (size_t i = 0; i < patch.vertices.size(); ++i) {
            glm::vec2 p = toSVG(patch.vertices[i]);
            out << (i > 0 ? " L" : "") << p.x << "," << p.y;
        }
        out << " Z\" fill=\"" << color << "\" stroke=\"#604020\" stroke-width=\"1.5\" "
            << "fill-opacity=\"0.7\"/>\n";

        // Patch center marker
        glm::vec2 c = toSVG(patch.center);
        out << "    <circle cx=\"" << c.x << "\" cy=\"" << c.y
            << "\" r=\"4\" fill=\"#402010\"/>\n";

        // Patch ID label
        out << "    <text x=\"" << c.x << "\" y=\"" << (c.y - 8)
            << "\" font-family=\"sans-serif\" font-size=\"10\" "
            << "text-anchor=\"middle\" fill=\"#402010\">" << patch.id << "</text>\n";
    }
    out << "  </g>\n";

    // Rivers
    out << "  <g id=\"rivers\">\n";
    for (const auto& river : rivers) {
        if (river.points.size() < 2) continue;

        // Check if river is within view
        bool inView = false;
        for (const auto& p : river.points) {
            if (glm::length(p - center) < margin * 1.2f) {
                inView = true;
                break;
            }
        }
        if (!inView) continue;

        out << "    <path d=\"M";
        for (size_t i = 0; i < river.points.size(); ++i) {
            glm::vec2 p = toSVG(river.points[i]);
            out << (i > 0 ? " L" : "") << p.x << "," << p.y;
        }

        // Use average width for stroke
        float avgWidth = 5.0f;
        if (!river.widths.empty()) {
            avgWidth = 0.0f;
            for (float w : river.widths) avgWidth += w;
            avgWidth /= river.widths.size();
        }
        float strokeWidth = avgWidth / (2.0f * margin) * cfg.svgWidth;
        strokeWidth = glm::clamp(strokeWidth, 2.0f, 20.0f);

        out << "\" fill=\"none\" stroke=\"#4080c0\" stroke-width=\""
            << strokeWidth << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
    }
    out << "  </g>\n";

    // Town radius circle
    glm::vec2 centerSVG = toSVG(center);
    float radiusSVG = cfg.radius / (2.0f * margin) * cfg.svgWidth;
    out << "  <circle cx=\"" << centerSVG.x << "\" cy=\"" << centerSVG.y
        << "\" r=\"" << radiusSVG << "\" fill=\"none\" stroke=\"#800000\" "
        << "stroke-width=\"2\" stroke-dasharray=\"10,5\"/>\n";

    // Center marker
    out << "  <circle cx=\"" << centerSVG.x << "\" cy=\"" << centerSVG.y
        << "\" r=\"6\" fill=\"#c00000\"/>\n";

    // Legend
    float legendY = cfg.svgHeight - 80;
    out << "  <g id=\"legend\" transform=\"translate(10," << legendY << ")\">\n";
    out << "    <rect x=\"0\" y=\"0\" width=\"180\" height=\"70\" "
        << "fill=\"white\" fill-opacity=\"0.8\" stroke=\"#999\"/>\n";
    out << "    <text x=\"5\" y=\"15\" font-family=\"sans-serif\" font-size=\"11\" "
        << "font-weight=\"bold\">Legend</text>\n";
    out << "    <rect x=\"5\" y=\"22\" width=\"15\" height=\"10\" fill=\"#a0c4e8\"/>\n";
    out << "    <text x=\"25\" y=\"31\" font-family=\"sans-serif\" font-size=\"10\">"
        << "Waterfront patch</text>\n";
    out << "    <rect x=\"5\" y=\"36\" width=\"15\" height=\"10\" fill=\"#90d4a8\"/>\n";
    out << "    <text x=\"25\" y=\"45\" font-family=\"sans-serif\" font-size=\"10\">"
        << "Riverside patch</text>\n";
    out << "    <line x1=\"5\" y1=\"55\" x2=\"20\" y2=\"55\" stroke=\"#4080c0\" "
        << "stroke-width=\"3\"/>\n";
    out << "    <text x=\"25\" y=\"58\" font-family=\"sans-serif\" font-size=\"10\">"
        << "River</text>\n";
    out << "  </g>\n";

    out << "</svg>\n";

    SDL_Log("Saved SVG: %s", path.c_str());
}

// ============================================================================
// Command Line Parsing
// ============================================================================

void printUsage(const char* prog) {
    SDL_Log("Usage: %s [options]", prog);
    SDL_Log("Options:");
    SDL_Log("  --heightmap <path>    Path to 16-bit PNG heightmap (required)");
    SDL_Log("  --rivers <path>       Path to rivers.geojson");
    SDL_Log("  --output <path>       Output SVG path (default: terrain_patches.svg)");
    SDL_Log("  --center <x,z>        Settlement center in world coords");
    SDL_Log("  --radius <meters>     Town radius (default: 500)");
    SDL_Log("  --patches <n>         Number of patches (default: 15)");
    SDL_Log("  --terrain-size <m>    Terrain size in meters (default: 16384)");
    SDL_Log("  --sea-level <m>       Sea level height (default: 0)");
    SDL_Log("  --min-alt <m>         Minimum altitude (default: -15)");
    SDL_Log("  --max-alt <m>         Maximum altitude (default: 200)");
    SDL_Log("  --seed <n>            Random seed");
    SDL_Log("  --svg-size <w,h>      SVG dimensions (default: 1024,1024)");
}

bool parseArgs(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--heightmap" && i + 1 < argc) {
            cfg.heightmapPath = argv[++i];
        }
        else if (arg == "--rivers" && i + 1 < argc) {
            cfg.riversPath = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc) {
            cfg.outputPath = argv[++i];
        }
        else if (arg == "--center" && i + 1 < argc) {
            std::string val = argv[++i];
            size_t comma = val.find(',');
            if (comma != std::string::npos) {
                cfg.centerX = std::stof(val.substr(0, comma));
                cfg.centerZ = std::stof(val.substr(comma + 1));
            }
        }
        else if (arg == "--radius" && i + 1 < argc) {
            cfg.radius = std::stof(argv[++i]);
        }
        else if (arg == "--patches" && i + 1 < argc) {
            cfg.numPatches = std::stoi(argv[++i]);
        }
        else if (arg == "--terrain-size" && i + 1 < argc) {
            cfg.terrainSize = std::stof(argv[++i]);
        }
        else if (arg == "--sea-level" && i + 1 < argc) {
            cfg.seaLevel = std::stof(argv[++i]);
        }
        else if (arg == "--min-alt" && i + 1 < argc) {
            cfg.minAltitude = std::stof(argv[++i]);
        }
        else if (arg == "--max-alt" && i + 1 < argc) {
            cfg.maxAltitude = std::stof(argv[++i]);
        }
        else if (arg == "--seed" && i + 1 < argc) {
            cfg.seed = std::stoi(argv[++i]);
        }
        else if (arg == "--svg-size" && i + 1 < argc) {
            std::string val = argv[++i];
            size_t comma = val.find(',');
            if (comma != std::string::npos) {
                cfg.svgWidth = std::stof(val.substr(0, comma));
                cfg.svgHeight = std::stof(val.substr(comma + 1));
            }
        }
    }

    if (cfg.heightmapPath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Heightmap path is required");
        printUsage(argv[0]);
        return false;
    }

    return true;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    Config cfg;

    if (!parseArgs(argc, argv, cfg)) {
        return 1;
    }

    SDL_Log("Terrain Patch Generator");
    SDL_Log("  Heightmap: %s", cfg.heightmapPath.c_str());
    SDL_Log("  Rivers: %s", cfg.riversPath.empty() ? "(none)" : cfg.riversPath.c_str());
    SDL_Log("  Center: (%.1f, %.1f)", cfg.centerX, cfg.centerZ);
    SDL_Log("  Radius: %.1f m", cfg.radius);
    SDL_Log("  Patches: %d", cfg.numPatches);

    // Load heightmap
    Heightmap hm;
    if (!loadHeightmap(cfg.heightmapPath, hm)) {
        return 1;
    }

    // Load rivers
    std::vector<RiverSegment> rivers;
    if (!cfg.riversPath.empty()) {
        rivers = loadRivers(cfg.riversPath);
    }

    // Detect coastline
    glm::vec2 center(cfg.centerX, cfg.centerZ);
    Coastline coast = detectCoastline(hm, cfg, center, cfg.radius);

    // Generate patch seeds
    std::vector<VoronoiSeed> seeds = generateSeeds(cfg, hm, rivers, coast);

    // Compute Voronoi patches
    std::vector<TerrainPatch> patches = computeVoronoiPatches(seeds, cfg, hm, rivers);

    // Output SVG
    saveSVG(cfg.outputPath, cfg, patches, rivers, coast);

    SDL_Log("Done!");

    return 0;
}
