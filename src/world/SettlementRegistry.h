#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

// Loads settlements.json produced by biome_preprocess and exposes the
// settlements in world space for placement, teleporting and debug display.
struct Settlement {
    uint32_t id = 0;
    std::string type;                    // "Town", "Village", "FishingVillage", ...
    glm::vec2 worldPos{0.0f};            // World-space XZ (centered origin)
    float radius = 0.0f;                 // Meters
    int score = 0;
    std::vector<std::string> features;   // "market", "coastal", ...

    std::string displayName() const {
        return type + " " + std::to_string(id);
    }
};

class SettlementRegistry {
public:
    static std::string getSettlementsPath(const std::string& cacheDir) {
        return cacheDir + "/settlements.json";
    }

    // Parse settlements.json; positions are converted from content space
    // (0..terrain_size) to world space (centered). Returns false on error.
    bool loadFromJson(const std::string& path);

    // Town layouts may grow beyond the nominal settlement radius (buildings
    // are kept at realistic sizes). Expand each radius to the extent_radius
    // recorded in town_<id>.geojson so vegetation suppression and markers
    // cover the whole built-up area.
    void applyTownExtents(const std::string& townsDir);

    bool isLoaded() const { return loaded_; }
    const std::vector<Settlement>& settlements() const { return settlements_; }

private:
    std::vector<Settlement> settlements_;
    bool loaded_ = false;
};
