#include "SettlementRegistry.h"
#include "WorldCoords.h"

#include <SDL3/SDL_log.h>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool SettlementRegistry::loadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SettlementRegistry: Failed to open %s", path.c_str());
        return false;
    }

    try {
        json j;
        file >> j;

        float terrainSize = j.value("terrain_size", WorldCoords::kTerrainSize);
        if (terrainSize != WorldCoords::kTerrainSize) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SettlementRegistry: terrain_size %f differs from expected %f",
                        terrainSize, WorldCoords::kTerrainSize);
        }

        settlements_.clear();

        for (const auto& s : j.value("settlements", json::array())) {
            Settlement settlement;
            settlement.id = s.value("id", 0u);
            settlement.type = s.value("type", "Settlement");
            glm::vec2 contentPos(s.value("x", 0.0f), s.value("z", 0.0f));
            settlement.worldPos = WorldCoords::contentToWorld(contentPos);
            settlement.radius = s.value("radius", 0.0f);
            settlement.score = s.value("score", 0);
            for (const auto& f : s.value("features", json::array())) {
                settlement.features.push_back(f.get<std::string>());
            }
            settlements_.push_back(std::move(settlement));
        }

        loaded_ = true;
        SDL_Log("SettlementRegistry: Loaded %zu settlements from %s",
                settlements_.size(), path.c_str());
        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SettlementRegistry: JSON parse error: %s", e.what());
        return false;
    }
}

void SettlementRegistry::applyTownExtents(const std::string& townsDir) {
    size_t expanded = 0;
    for (auto& settlement : settlements_) {
        std::string path = townsDir + "/town_" + std::to_string(settlement.id) + ".geojson";
        std::ifstream file(path);
        if (!file.is_open()) continue;
        try {
            json j;
            file >> j;
            float extent = j.value("properties", json::object()).value("extent_radius", 0.0f);
            if (extent > settlement.radius) {
                settlement.radius = extent;
                expanded++;
            }
        } catch (const std::exception& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SettlementRegistry: Failed to parse %s: %s", path.c_str(), e.what());
        }
    }
    if (expanded > 0) {
        SDL_Log("SettlementRegistry: Expanded %zu settlement radii to town extents", expanded);
    }
}
