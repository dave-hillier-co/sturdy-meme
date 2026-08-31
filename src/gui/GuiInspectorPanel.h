#pragma once

#include "SceneEditorState.h"

class ISceneControl;

namespace GuiInspectorPanel {
    /**
     * Render the inspector panel showing properties of the current selection.
     * An ECS entity selection is editable (transforms, materials, components,
     * tags); a renderable selection (Hierarchy "Renderables" tab) shows
     * read-only Transform/Material/Info sections.
     *
     * @param sceneControl Scene control interface for ECS World access
     * @param state Editor state for selection tracking
     */
    void render(ISceneControl& sceneControl, SceneEditorState& state);
}
