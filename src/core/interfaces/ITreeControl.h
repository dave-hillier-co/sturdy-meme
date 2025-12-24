#pragma once

class TreeSystem;
class RendererSystems;

/**
 * Interface for tree/vegetation controls.
 * Used by GuiTreeTab to control tree generation and LOD settings.
 */
class ITreeControl {
public:
    virtual ~ITreeControl() = default;

    // Tree system access (for tree editor)
    virtual TreeSystem* getTreeSystem() = 0;
    virtual const TreeSystem* getTreeSystem() const = 0;

    // Systems access (for TreeLODSystem)
    virtual RendererSystems& getSystems() = 0;
    virtual const RendererSystems& getSystems() const = 0;
};
