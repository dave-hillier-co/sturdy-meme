#pragma once

#include <functional>
#include <string>
#include <vector>

class IDebugControl;
struct DebugCommand;

/**
 * Debug visualizations panel. Application-level actions (ragdoll spawning,
 * world teleport) and the debug command cheatsheet data are injected at
 * construction; IDebugControl covers renderer-side debug toggles.
 */
class GuiDebugTab {
public:
    // World teleport destination (settlements etc.)
    struct TeleportTarget {
        std::string name;
        float worldX = 0.0f;
        float worldZ = 0.0f;
        float radius = 0.0f;
    };

    // Application-provided actions. Any hook may be empty; the panel hides
    // the corresponding UI.
    struct Hooks {
        std::function<void()> spawnRagdoll;
        std::function<int()> ragdollCount;
        std::function<void(float worldX, float worldZ)> teleport;
        std::function<const std::vector<TeleportTarget>&()> teleportTargets;
    };

    GuiDebugTab(IDebugControl& debugControl, Hooks hooks,
                const std::vector<DebugCommand>* debugCommands)
        : debugControl_(debugControl)
        , hooks_(std::move(hooks))
        , debugCommands_(debugCommands) {}

    void draw() { render(debugControl_, hooks_, debugCommands_); }

private:
    static void render(IDebugControl& debugControl, const Hooks& hooks,
                       const std::vector<DebugCommand>* debugCommands);

    IDebugControl& debugControl_;
    Hooks hooks_;
    const std::vector<DebugCommand>* debugCommands_ = nullptr;  // Nullable cheatsheet data
};
