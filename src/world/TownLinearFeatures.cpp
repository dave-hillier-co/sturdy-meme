#include "TownLinearFeatures.h"
#include "WorldCoords.h"
#include "GeneratedMeshUtil.h"
#include "ecs/EntityFactory.h"

#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>

using json = nlohmann::json;

namespace {

struct TownLine {
    std::vector<glm::vec2> points;  // Content-space
    std::string kind;
};

std::vector<TownLine> loadTownLines(const std::string& path) {
    std::vector<TownLine> lines;
    std::ifstream file(path);
    if (!file.is_open()) return lines;

    try {
        json j;
        file >> j;
        for (const auto& feature : j.value("features", json::array())) {
            if (feature["geometry"]["type"] != "LineString") continue;
            std::string kind = feature["properties"].value("kind", "");
            if (kind.empty() && feature["properties"].contains("type")) {
                // Street-generator networks tag segments with "type" instead
                kind = "street";
            }
            if (kind != "artery" && kind != "road" && kind != "street" &&
                kind != "alley" && kind != "wall") {
                continue;
            }
            TownLine line;
            line.kind = kind;
            for (const auto& coord : feature["geometry"]["coordinates"]) {
                line.points.emplace_back(coord[0].get<float>(), coord[1].get<float>());
            }
            if (line.points.size() >= 2) lines.push_back(std::move(line));
        }
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TownLinearFeatures: failed to parse %s: %s", path.c_str(), e.what());
    }
    return lines;
}

float streetWidthFor(const std::string& kind) {
    if (kind == "artery") return 5.0f;
    if (kind == "road") return 4.0f;
    if (kind == "street") return 3.0f;
    return 2.0f; // alley
}

} // namespace

TownLinearFeatures::Result TownLinearFeatures::generateForSettlement(
    ecs::World& world, RibbonMeshGenerator& ribbonGen, const Settlement& settlement) {
    Result result;
    if (config_.townsDir.empty()) return result;

    std::string path = config_.townsDir + "/town_" + std::to_string(settlement.id) + ".geojson";
    std::vector<TownLine> lines = loadTownLines(path);

    // Fall back to the street generator's network when the town layout has
    // no street lines (settlements outside the Watabou pipeline).
    bool hasStreets = false;
    for (const auto& line : lines) {
        if (line.kind != "wall") { hasStreets = true; break; }
    }
    if (!hasStreets && !config_.streetsDir.empty()) {
        std::string streetsPath = config_.streetsDir + "/settlement_" +
                                  std::to_string(settlement.id) + "/streets.geojson";
        for (auto& line : loadTownLines(streetsPath)) {
            lines.push_back(std::move(line));
        }
    }
    if (lines.empty()) return result;

    // Streets: draped ribbons tinted by prominence
    std::vector<RibbonMeshGenerator::Ribbon> ribbons;
    for (const auto& line : lines) {
        if (line.kind == "wall") continue;
        RibbonMeshGenerator::Ribbon ribbon;
        ribbon.material = config_.streetMaterial;
        ribbon.followTerrain = true;
        ribbon.widths = {streetWidthFor(line.kind)};
        if (line.kind == "artery") ribbon.color = config_.arteryColor;
        else if (line.kind == "alley") ribbon.color = config_.alleyColor;
        else ribbon.color = config_.streetColor;
        for (const auto& p : line.points) {
            glm::vec2 w = WorldCoords::contentToWorld(p);
            ribbon.points.emplace_back(w.x, 0.0f, w.y);
        }
        ribbons.push_back(std::move(ribbon));
    }
    if (!ribbons.empty()) {
        auto ribbonResult = ribbonGen.generate(world, ribbons);
        result.streetChunks = ribbonResult.chunksBuilt;
        result.entities.insert(result.entities.end(),
                               ribbonResult.entities.begin(), ribbonResult.entities.end());
    }

    // Walls: extruded segment runs with colliders
    if (config_.createMeshes && config_.getTerrainHeight) {
        std::vector<MeshGeometry> pendingGeo;
        struct PendingCollider {
            glm::vec3 center;
            glm::vec3 halfExtents;
            float yaw;
        };
        std::vector<PendingCollider> pendingColliders;
        MeshGeometry geo;

        for (const auto& line : lines) {
            if (line.kind != "wall") continue;
            for (size_t i = 0; i + 1 < line.points.size(); ++i) {
                glm::vec2 a = WorldCoords::contentToWorld(line.points[i]);
                glm::vec2 b = WorldCoords::contentToWorld(line.points[i + 1]);
                glm::vec2 ab = b - a;
                float length = glm::length(ab);
                if (length < 0.1f) continue;
                glm::vec2 dir = ab / length;
                glm::vec2 mid = (a + b) * 0.5f;

                // Base the wall at the lower end so it never floats; sink it
                // slightly for slope coverage.
                float ha = config_.getTerrainHeight(a.x, a.y);
                float hb = config_.getTerrainHeight(b.x, b.y);
                float base = std::min(ha, hb) - 0.5f;
                float top = std::max(ha, hb) + config_.wallHeight;
                float halfY = (top - base) * 0.5f;
                glm::vec3 center(mid.x, base + halfY, mid.y);

                glm::vec2 perp(-dir.y, dir.x);
                glm::vec3 axisAlong(dir.x * length * 0.5f, 0.0f, dir.y * length * 0.5f);
                glm::vec3 axisAcross(perp.x * config_.wallThickness * 0.5f, 0.0f,
                                     perp.y * config_.wallThickness * 0.5f);
                GeneratedMeshUtil::appendOrientedBox(geo, center, axisAlong, axisAcross, halfY);

                float yaw = std::atan2(-dir.y, dir.x);
                pendingColliders.push_back(
                    {center,
                     glm::vec3(length * 0.5f, halfY, config_.wallThickness * 0.5f),
                     yaw});
                ++result.wallSegments;
            }
        }

        if (!geo.vertices.empty()) {
            pendingGeo.push_back(std::move(geo));
            std::vector<Mesh*> meshes = config_.createMeshes(std::move(pendingGeo));
            ecs::EntityFactory factory(world);
            for (Mesh* mesh : meshes) {
                if (!mesh) continue;
                result.entities.push_back(factory.createBuilding(
                    mesh, config_.wallMaterial, glm::mat4(1.0f), settlement.id));
            }
            if (config_.addCollider) {
                for (const auto& c : pendingColliders) {
                    config_.addCollider(c.center, c.halfExtents, c.yaw);
                }
            }
        }
    }

    return result;
}
