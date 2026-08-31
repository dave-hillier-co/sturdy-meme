#include "GameSettings.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>

namespace {

constexpr const char* kFileName = "game_settings.json";

constexpr const char* kMouseSensitivityKey = "mouseSensitivity";
constexpr const char* kInvertMouseYKey = "invertMouseY";
constexpr const char* kFullscreenKey = "fullscreen";
constexpr const char* kQualityKey = "quality";

bool isKnownKey(const std::string& key) {
    return key == kMouseSensitivityKey || key == kInvertMouseYKey ||
           key == kFullscreenKey || key == kQualityKey;
}

}  // anonymous namespace

std::string GameSettings::defaultFilePath() {
    const char* basePath = SDL_GetBasePath();
    return basePath ? std::string(basePath) + kFileName : std::string(kFileName);
}

GameSettings GameSettings::loadFromFile(const std::string& path) {
    GameSettings settings;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return settings;  // First run: defaults, no warning.
    }

    std::ifstream file(path);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "GameSettings: could not open %s, using defaults", path.c_str());
        return settings;
    }

    nlohmann::json root = nlohmann::json::parse(file, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "GameSettings: %s is malformed, using defaults", path.c_str());
        return settings;
    }

    if (auto it = root.find(kMouseSensitivityKey);
        it != root.end() && it->is_number()) {
        settings.mouseSensitivity = it->get<float>();
    }
    if (auto it = root.find(kInvertMouseYKey); it != root.end() && it->is_boolean()) {
        settings.invertMouseY = it->get<bool>();
    }
    if (auto it = root.find(kFullscreenKey); it != root.end() && it->is_boolean()) {
        settings.fullscreen = it->get<bool>();
    }
    if (auto it = root.find(kQualityKey); it != root.end() && it->is_object()) {
        for (const auto& [name, value] : it->items()) {
            if (value.is_boolean()) {
                settings.qualityOverrides[name] = value.get<bool>();
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "GameSettings: ignoring non-boolean quality entry '%s'",
                            name.c_str());
            }
        }
    }

    // Preserve anything this build does not understand for the next save.
    for (const auto& [key, value] : root.items()) {
        if (!isKnownKey(key)) {
            settings.unknownKeys[key] = value;
        }
    }

    return settings;
}

bool GameSettings::saveToFile(const std::string& path) const {
    nlohmann::json root = unknownKeys.is_object() ? unknownKeys
                                                  : nlohmann::json::object();
    root[kMouseSensitivityKey] = mouseSensitivity;
    root[kInvertMouseYKey] = invertMouseY;
    root[kFullscreenKey] = fullscreen;

    nlohmann::json quality = nlohmann::json::object();
    for (const auto& [name, enabled] : qualityOverrides) {
        quality[name] = enabled;
    }
    root[kQualityKey] = std::move(quality);

    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "GameSettings: could not write %s", path.c_str());
        return false;
    }
    file << root.dump(2) << '\n';
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "GameSettings: write to %s failed", path.c_str());
        return false;
    }
    return true;
}
