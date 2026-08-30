#pragma once

#include <vector>

class IDebugControl;
struct DebugCommand;

namespace GuiDebugTab {
    void render(IDebugControl& debugControl,
                const std::vector<DebugCommand>* debugCommands = nullptr);
}
