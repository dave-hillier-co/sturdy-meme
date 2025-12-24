#pragma once

class SceneBuilder;

/**
 * Interface for player-related controls.
 * Used by GuiPlayerTab to control player-specific settings.
 */
class IPlayerControl {
public:
    virtual ~IPlayerControl() = default;

    // Scene builder access (for player cape)
    virtual SceneBuilder& getSceneBuilder() = 0;
    virtual const SceneBuilder& getSceneBuilder() const = 0;
};
