#pragma once

#include "TreeParameters.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <SDL3/SDL.h>

// Tree preset data loader
// Loads ez-tree format JSON presets and converts them to TreeParameters
// Values are scaled from ez-tree scale (30-70m trees) to game scale (~3-7m trees)

struct TreePreset {
    std::string name;
    std::string filename;
    TreeParameters params;
};

namespace TreePresets {

// Scale factor to convert ez-tree units to game units
// Ez-tree units appear to be roughly 1:1 with meters for realistic tree sizes
// A "large oak" at 48 units = ~15m tall tree (reasonable for oak)
constexpr float SCALE_FACTOR = 0.3f;

// Convert ez-tree JSON to TreeParameters
inline TreeParameters loadFromJson(const nlohmann::json& j) {
    TreeParameters p;

    // Seed
    if (j.contains("seed")) {
        p.seed = j["seed"].get<uint32_t>();
    }

    // Tree type
    if (j.contains("type")) {
        std::string type = j["type"].get<std::string>();
        p.treeType = (type == "evergreen") ? TreeType::Evergreen : TreeType::Deciduous;
    }

    // Bark parameters
    if (j.contains("bark")) {
        const auto& bark = j["bark"];
        if (bark.contains("type")) {
            std::string barkType = bark["type"].get<std::string>();
            if (barkType == "oak") p.barkType = BarkType::Oak;
            else if (barkType == "birch") p.barkType = BarkType::Birch;
            else if (barkType == "pine") p.barkType = BarkType::Pine;
            else if (barkType == "willow") p.barkType = BarkType::Willow;
        }
        if (bark.contains("tint")) {
            uint32_t tint = bark["tint"].get<uint32_t>();
            p.barkTint.r = ((tint >> 16) & 0xFF) / 255.0f;
            p.barkTint.g = ((tint >> 8) & 0xFF) / 255.0f;
            p.barkTint.b = (tint & 0xFF) / 255.0f;
        }
        if (bark.contains("flatShading")) {
            p.barkFlatShading = bark["flatShading"].get<bool>();
        }
        if (bark.contains("textured")) {
            p.barkTextured = bark["textured"].get<bool>();
        }
        if (bark.contains("textureScale")) {
            const auto& ts = bark["textureScale"];
            p.barkTextureScale.x = ts["x"].get<float>();
            p.barkTextureScale.y = ts["y"].get<float>();
        }
    }

    // Branch parameters
    if (j.contains("branch")) {
        const auto& branch = j["branch"];

        if (branch.contains("levels")) {
            p.branchLevels = branch["levels"].get<int>();
        }

        // Load per-level parameters

        // Load angle per level
        if (branch.contains("angle")) {
            const auto& angle = branch["angle"];
            for (int i = 1; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (angle.contains(key)) {
                    p.branchParams[i].angle = angle[key].get<float>();
                }
            }
        }

        // Load children per level
        if (branch.contains("children")) {
            const auto& children = branch["children"];
            for (int i = 0; i <= 2; i++) {
                std::string key = std::to_string(i);
                if (children.contains(key)) {
                    p.branchParams[i].children = children[key].get<int>();
                }
            }
        }

        // Load force/growth influence
        if (branch.contains("force")) {
            const auto& force = branch["force"];
            if (force.contains("strength")) {
                p.growthInfluence = force["strength"].get<float>();
            }
            if (force.contains("direction")) {
                const auto& dir = force["direction"];
                p.growthDirection.x = dir["x"].get<float>();
                p.growthDirection.y = dir["y"].get<float>();
                p.growthDirection.z = dir["z"].get<float>();
            }
        }

        // Load gnarliness per level
        if (branch.contains("gnarliness")) {
            const auto& gnarliness = branch["gnarliness"];
            for (int i = 0; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (gnarliness.contains(key)) {
                    // ez-tree can have negative gnarliness, we use absolute value
                    p.branchParams[i].gnarliness = std::abs(gnarliness[key].get<float>());
                }
            }
        }

        // Load length per level (scaled)
        if (branch.contains("length")) {
            const auto& length = branch["length"];
            for (int i = 0; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (length.contains(key)) {
                    p.branchParams[i].length = length[key].get<float>() * SCALE_FACTOR;
                }
            }
        }

        // Load radius per level
        // IMPORTANT: Only level 0 (trunk) is absolute and needs scaling
        // Levels 1-3 are MULTIPLIERS on parent radius (no scaling needed)
        if (branch.contains("radius")) {
            const auto& radius = branch["radius"];
            for (int i = 0; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (radius.contains(key)) {
                    float r = radius[key].get<float>();
                    // Only scale level 0 (trunk absolute radius)
                    p.branchParams[i].radius = (i == 0) ? (r * SCALE_FACTOR) : r;
                }
            }
        }

        // Load sections per level
        if (branch.contains("sections")) {
            const auto& sections = branch["sections"];
            for (int i = 0; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (sections.contains(key)) {
                    p.branchParams[i].sections = sections[key].get<int>();
                }
            }
        }

        // Load segments per level
        if (branch.contains("segments")) {
            const auto& segments = branch["segments"];
            for (int i = 0; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (segments.contains(key)) {
                    p.branchParams[i].segments = segments[key].get<int>();
                }
            }
        }

        // Load start per level
        if (branch.contains("start")) {
            const auto& start = branch["start"];
            for (int i = 1; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (start.contains(key)) {
                    p.branchParams[i].start = start[key].get<float>();
                }
            }
        }

        // Load taper per level
        if (branch.contains("taper")) {
            const auto& taper = branch["taper"];
            for (int i = 0; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (taper.contains(key)) {
                    p.branchParams[i].taper = taper[key].get<float>();
                }
            }
        }

        // Load twist per level
        if (branch.contains("twist")) {
            const auto& twist = branch["twist"];
            for (int i = 0; i <= 3; i++) {
                std::string key = std::to_string(i);
                if (twist.contains(key)) {
                    p.branchParams[i].twist = twist[key].get<float>();
                }
            }
        }
    }

    // Leaf parameters
    if (j.contains("leaves")) {
        const auto& leaves = j["leaves"];

        if (leaves.contains("type")) {
            std::string leafType = leaves["type"].get<std::string>();
            if (leafType == "oak") p.leafType = LeafType::Oak;
            else if (leafType == "ash") p.leafType = LeafType::Ash;
            else if (leafType == "aspen") p.leafType = LeafType::Aspen;
            else if (leafType == "pine") p.leafType = LeafType::Pine;
        }

        if (leaves.contains("billboard")) {
            std::string billboard = leaves["billboard"].get<std::string>();
            p.leafBillboard = (billboard == "double") ? BillboardMode::Double : BillboardMode::Single;
        }

        if (leaves.contains("tint")) {
            uint32_t tint = leaves["tint"].get<uint32_t>();
            p.leafTint.r = ((tint >> 16) & 0xFF) / 255.0f;
            p.leafTint.g = ((tint >> 8) & 0xFF) / 255.0f;
            p.leafTint.b = (tint & 0xFF) / 255.0f;
        }

        if (leaves.contains("angle")) {
            p.leafAngle = leaves["angle"].get<float>();
        }

        if (leaves.contains("count")) {
            p.leavesPerBranch = leaves["count"].get<int>();
        }

        if (leaves.contains("start")) {
            p.leafStart = leaves["start"].get<float>();
        }

        if (leaves.contains("size")) {
            // Scale leaf size proportionally
            p.leafSize = leaves["size"].get<float>() * SCALE_FACTOR;
        }

        if (leaves.contains("sizeVariance")) {
            p.leafSizeVariance = leaves["sizeVariance"].get<float>();
        }

        if (leaves.contains("alphaTest")) {
            p.leafAlphaTest = leaves["alphaTest"].get<float>();
        }
    }

    return p;
}

// Load a preset from a JSON file
inline bool loadPresetFromFile(const std::string& filepath, TreePreset& outPreset) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open preset file: %s", filepath.c_str());
            return false;
        }

        nlohmann::json j;
        file >> j;

        outPreset.filename = filepath;
        outPreset.params = loadFromJson(j);

        // Extract name from filename
        std::filesystem::path path(filepath);
        outPreset.name = path.stem().string();
        // Convert underscore to space and capitalize
        for (size_t i = 0; i < outPreset.name.size(); i++) {
            if (outPreset.name[i] == '_') {
                outPreset.name[i] = ' ';
            } else if (i == 0 || outPreset.name[i-1] == ' ') {
                outPreset.name[i] = static_cast<char>(std::toupper(outPreset.name[i]));
            }
        }

        return true;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error loading preset %s: %s", filepath.c_str(), e.what());
        return false;
    }
}

// Load all presets from a directory
inline std::vector<TreePreset> loadPresetsFromDirectory(const std::string& directory) {
    std::vector<TreePreset> presets;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".json") {
                TreePreset preset;
                if (loadPresetFromFile(entry.path().string(), preset)) {
                    presets.push_back(std::move(preset));
                }
            }
        }

        // Sort by name
        std::sort(presets.begin(), presets.end(),
            [](const TreePreset& a, const TreePreset& b) {
                return a.name < b.name;
            });

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error scanning preset directory %s: %s", directory.c_str(), e.what());
    }

    SDL_Log("Loaded %zu tree presets from %s", presets.size(), directory.c_str());
    return presets;
}

} // namespace TreePresets
