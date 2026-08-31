#pragma once

#include "SceneEditorState.h"
#include <glm/glm.hpp>
#include <vector>

namespace ecs {
    struct RenderData;
}
class ISceneControl;

/**
 * Shared property-editor widgets used by the Inspector and Hierarchy panels.
 *
 * Two flavors are provided:
 * - Editor functions operate on live ECS components and allow modification
 *   (used by GuiInspectorPanel for an Entity selection).
 * - Display functions render read-only views of extracted render data
 *   (used by GuiInspectorPanel for a Renderable selection).
 */
namespace GuiPropertyEditors {
    // Draw a small color preview square at the cursor
    void drawColorPreview(const glm::vec3& color, float size = 16.0f);

    // Edit a vec3 with colored X/Y/Z drag fields. Returns true when changed.
    bool editVec3(const char* label, glm::vec3& value, float speed = 0.1f,
                  float min = -10000.0f, float max = 10000.0f);

    // Edit a color with picker and preview square. Returns true when changed.
    bool editColor(const char* label, glm::vec3& color);

    // Editable Transform section for an entity (mode/space selectors,
    // LocalTransform editing, world-transform fallback display).
    void renderTransformEditor(ecs::World& world, ecs::Entity entity, SceneEditorState& state);

    // Editable Material section for an entity (PBR, opacity, hue shift).
    void renderMaterialEditor(ecs::World& world, ecs::Entity entity, SceneEditorState& state);

    // Read-only Transform section decomposed from a world matrix.
    void renderTransformDisplay(const glm::mat4& transform, bool& showSection);

    // Read-only Material section from extracted render data.
    void renderMaterialDisplay(const ecs::RenderData& renderData, bool& showSection);

    // Read-only Info section (shadow/PBR flags, mesh stats) from extracted
    // render data. objectIndex is the renderable index shown for reference.
    void renderRenderableInfoDisplay(const ecs::RenderData& renderData, int objectIndex,
                                     bool& showSection);

    // Rebuild render rows from the ECS each frame so views reflect live
    // component state rather than a mirror. Indices stay parallel to
    // SceneBuilder::getSceneEntities() so a renderable selection is stable.
    // Returns empty when the ECS world is not bound yet.
    std::vector<ecs::RenderData> collectRenderables(ISceneControl& sceneControl);

    // Short type name for a renderable ("Character", "Emissive", ...).
    const char* renderableTypeName(const ecs::RenderData& renderData);
}
