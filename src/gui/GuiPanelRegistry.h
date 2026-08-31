#pragma once

#include <functional>
#include <string>

class Camera;

/**
 * Menu bar category a panel is listed under.
 * One top-level menu is generated per category, in this order.
 */
enum class MenuCategory {
    View,
    Environment,
    Rendering,
    Character,
    Scene,
    Debug
};

/**
 * Per-frame data handed to every panel draw callback. Panels receive their
 * system dependencies at construction time (bound in GuiSystem's registry),
 * so this carries only data that genuinely changes each frame.
 */
struct GuiFrameContext {
    const Camera& camera;
    float deltaTime;
    float fps;
};

/**
 * A registered debug panel. The registry loop renders each open panel as
 * ImGui::Begin(title, &open) { draw(ctx); } ImGui::End(), and the menu bar
 * shows one checkbox item per panel under its category. Type-erased
 * std::function only - no inheritance.
 */
struct PanelDesc {
    std::string title;
    MenuCategory category;
    std::function<void(const GuiFrameContext&)> draw;
    bool open = false;
};
