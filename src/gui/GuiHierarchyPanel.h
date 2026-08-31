#pragma once

#include "SceneEditorState.h"

class ISceneControl;

namespace GuiHierarchyPanel {
    /**
     * Render the hierarchy panel with two tabbed views:
     * - "Entities": the ECS entity tree with parent-child relationships,
     *   filtering, and drag-drop reparenting.
     * - "Renderables": a flat, filterable list of render rows; selecting one
     *   shows read-only properties in the Inspector.
     * Both tabs write to the single shared selection in SceneEditorState.
     *
     * @param sceneControl Scene control interface for ECS World access
     * @param state Editor state for selection and expand/collapse tracking
     */
    void render(ISceneControl& sceneControl, SceneEditorState& state);

    /**
     * Render a "Create" menu bar for adding new entities.
     * Call this inside a window that has ImGuiWindowFlags_MenuBar.
     */
    void renderCreateMenuBar(ISceneControl& sceneControl, SceneEditorState& state);
}
