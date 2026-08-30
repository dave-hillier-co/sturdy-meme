#include "DebugCommands.h"

namespace DebugCommands {

bool dispatchKey(const std::vector<DebugCommand>& commands, SDL_Scancode key) {
    if (key == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    for (const auto& command : commands) {
        if (command.key == key && command.action) {
            command.action();
            return true;
        }
    }
    return false;
}

std::string keyName(const DebugCommand& command) {
    if (command.key == SDL_SCANCODE_UNKNOWN) {
        return "unbound";
    }
    const char* name = SDL_GetScancodeName(command.key);
    if (!name || name[0] == '\0') {
        return "unknown";
    }
    return name;
}

}  // namespace DebugCommands
