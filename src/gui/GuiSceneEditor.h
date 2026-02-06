#pragma once

#include "SceneEditorState.h"

class ISceneControl;

/**
 * Unity-like Scene Editor with dockable panels.
 * Provides a full editor interface with:
 * - Hierarchy panel (entity tree with parent-child relationships)
 * - Inspector panel (property editing for selected entities)
 * - Dockable window layout using ImGui docking
 */
namespace GuiSceneEditor {
    /**
     * Render the complete scene editor with dockable layout.
     * This creates a dockspace and renders hierarchy and inspector panels.
     *
     * @param sceneControl Scene control interface for ECS access
     * @param state Editor state (selection, expand/collapse, etc.)
     * @param showWindow Pointer to bool controlling window visibility
     */
    void render(ISceneControl& sceneControl, SceneEditorState& state, bool* showWindow);

    /**
     * Render just the hierarchy panel (standalone, without docking).
     * Useful if you want to embed it in another window.
     */
    void renderHierarchy(ISceneControl& sceneControl, SceneEditorState& state);

    /**
     * Render just the inspector panel (standalone, without docking).
     * Useful if you want to embed it in another window.
     */
    void renderInspector(ISceneControl& sceneControl, SceneEditorState& state);
}
