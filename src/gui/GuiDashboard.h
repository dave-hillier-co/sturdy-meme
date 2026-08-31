#pragma once

#include "GuiPanelRegistry.h"

class TerrainSystem;
class ITimeSystem;
class Camera;

/**
 * Dashboard panel: frame time graph, terrain stats, time of day.
 * Owns its frame-time history; dependencies are bound at construction.
 */
class GuiDashboard {
public:
    struct State {
        float frameTimeHistory[120] = {0};
        int frameTimeIndex = 0;
        float avgFrameTime = 0.0f;
    };

    GuiDashboard(TerrainSystem& terrain, ITimeSystem& time)
        : terrain_(terrain), time_(time) {}

    void draw(const GuiFrameContext& ctx) {
        render(terrain_, time_, ctx.camera, ctx.deltaTime, ctx.fps, state_);
    }

private:
    static void render(TerrainSystem& terrain, ITimeSystem& time, const Camera& camera,
                       float deltaTime, float fps, State& state);

    TerrainSystem& terrain_;
    ITimeSystem& time_;
    State state_;
};
