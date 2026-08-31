#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>

/**
 * GameSettings - Persistent player-facing settings.
 *
 * A small value struct that is the single source of truth for the pause
 * menu's settings page. Serialized as pretty-printed JSON to
 * game_settings.json next to the executable (same path resolution as
 * imgui_layout.ini). Loaded once at startup; saved whenever the player
 * changes a setting and again on clean shutdown as a catch-all.
 *
 * qualityOverrides stores only the performance toggles the player has
 * explicitly changed (toggle name -> enabled); everything else keeps the
 * compiled-in PerformanceToggles defaults.
 *
 * Unknown top-level keys found in the file are preserved across a
 * load/save round trip so hand-added or future fields are not destroyed.
 */
struct GameSettings {
    float mouseSensitivity = 1.0f;
    bool invertMouseY = false;
    bool fullscreen = false;
    std::map<std::string, bool> qualityOverrides;

    // Top-level keys from the file that this build does not understand,
    // carried through so saving never drops them.
    nlohmann::json unknownKeys = nlohmann::json::object();

    // game_settings.json next to the executable (SDL_GetBasePath), matching
    // how GuiSystem places imgui_layout.ini.
    static std::string defaultFilePath();

    // Missing file: defaults, silently. Malformed file or wrong value types:
    // defaults for the affected fields with a warning logged. Never throws.
    static GameSettings loadFromFile(const std::string& path);

    // Pretty-printed JSON write; logs a warning and returns false on failure.
    bool saveToFile(const std::string& path) const;
};
