#pragma once

#include "SceneEditorState.h"
#include <glm/glm.hpp>

class Camera;
class ISceneControl;

/**
 * 3D Transform Gizmo for scene editor using ImGuizmo.
 * Renders translate/rotate/scale gizmos over the viewport for the selected entity.
 */
namespace GuiGizmo {

    /**
     * Render the transform gizmo for the currently selected entity.
     * Should be called after ImGui::NewFrame() and before ImGui::Render().
     * The gizmo renders over the main viewport.
     *
     * @param camera The scene camera (for view/projection matrices)
     * @param sceneControl Scene control for ECS world access
     * @param state Editor state (selection, transform mode)
     * @return true if the gizmo was manipulated this frame
     */
    bool render(const Camera& camera, ISceneControl& sceneControl, SceneEditorState& state);

    /**
     * Check if the mouse is currently over the gizmo.
     * Use this to prevent camera controls when manipulating gizmos.
     */
    bool isOver();

    /**
     * Check if the gizmo is currently being used (dragged).
     */
    bool isUsing();

}
