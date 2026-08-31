#pragma once

class Camera;
class ITimeSystem;

/**
 * GameHud - Player-facing gameplay HUD overlay.
 *
 * Rendered with ImGui background draw-list primitives only: no windows, no
 * focus, no input capture. Drawn every frame by GuiSystem alongside GameMenu,
 * independent of the F1 debug-GUI visibility flag (GuiSystem skips it while
 * the pause menu is open, and it can be toggled via the "Toggle HUD" debug
 * command).
 *
 * Current content, all sourced from systems that exist today:
 *  - Compass strip (top-center): heading band sliding with camera yaw.
 *  - Time-of-day indicator: sun/moon glyph plus a clock readout.
 */
class GameHud {
public:
    // Draw the overlay. Call inside the ImGui frame.
    void render(const Camera& camera, const ITimeSystem& time);

    bool isVisible() const { return visible_; }
    void toggleVisible() { visible_ = !visible_; }
    void setVisible(bool v) { visible_ = v; }

private:
    bool visible_ = true;  // default ON
};
