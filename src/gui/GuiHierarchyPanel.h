#pragma once

#include "SceneEditorState.h"

class ISceneControl;

namespace GuiHierarchyPanel {
    /**
     * Render the hierarchy panel showing entity tree.
     * Supports parent-child relationships, filtering, and drag-drop reparenting.
     *
     * @param sceneControl Scene control interface for ECS World access
     * @param state Editor state for selection and expand/collapse tracking
     */
    void render(ISceneControl& sceneControl, SceneEditorState& state);
}
