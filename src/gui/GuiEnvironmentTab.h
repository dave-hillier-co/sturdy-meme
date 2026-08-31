#pragma once

class IEnvironmentControl;

// State for environment tab toggles that need to persist
struct EnvironmentTabState {
    bool heightFogEnabled = true;
    float cachedLayerDensity = 0.02f;
    bool atmosphereEnabled = true;
    float cachedRayleighScale = 13.558f;
    float cachedMieScale = 3.996f;
};

/**
 * Atmosphere/fog panel. Owns its toggle state; the environment control
 * facade is bound at construction.
 */
class GuiEnvironmentTab {
public:
    explicit GuiEnvironmentTab(IEnvironmentControl& envControl) : envControl_(envControl) {}

    void draw() { render(envControl_, state_); }

private:
    static void render(IEnvironmentControl& envControl, EnvironmentTabState& state);

    IEnvironmentControl& envControl_;
    EnvironmentTabState state_;
};
