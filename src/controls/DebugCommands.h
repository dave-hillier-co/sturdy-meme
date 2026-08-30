#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <string>
#include <vector>

/**
 * DebugCommand - A single debug keybinding/action.
 *
 * The Application builds a table of these once at startup; the SDL key-down
 * handler dispatches through the table, and the GUI debug tab renders the
 * same table as an always-up-to-date shortcut cheatsheet.
 */
struct DebugCommand {
    std::string id;        // Stable identifier, e.g. "time.preset.noon"
    std::string label;     // Human-readable description for the cheatsheet
    std::string category;  // Grouping for the cheatsheet, e.g. "Time"
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;  // SDL_SCANCODE_UNKNOWN if unbound
    std::function<void()> action;
};

namespace DebugCommands {

/// Runs the first command bound to the given scancode. Returns true if one ran.
bool dispatchKey(const std::vector<DebugCommand>& commands, SDL_Scancode key);

/// Display name for a command's key ("R", "F1", "unbound" for SDL_SCANCODE_UNKNOWN).
std::string keyName(const DebugCommand& command);

}  // namespace DebugCommands
