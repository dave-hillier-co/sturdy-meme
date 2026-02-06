#pragma once

#include "SceneEditorState.h"

class ISceneControl;

namespace GuiInspectorPanel {
    /**
     * Render the inspector panel showing properties of selected entity.
     * Supports editing transforms, materials, and component properties.
     *
     * @param sceneControl Scene control interface for ECS World access
     * @param state Editor state for selection tracking
     */
    void render(ISceneControl& sceneControl, SceneEditorState& state);
}
