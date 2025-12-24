#pragma once

#include <cstdint>

class SceneBuilder;

/**
 * Interface for scene/character controls.
 * Used by GuiIKTab to access animated character and skeleton.
 */
class ISceneControl {
public:
    virtual ~ISceneControl() = default;

    // Scene builder access (for animated character)
    // Note: Only mutable version required by interface; const is optional
    virtual SceneBuilder& getSceneBuilder() = 0;

    // Viewport dimensions (needed for skeleton overlay projection)
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;
};
