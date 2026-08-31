#include "GuiPropertyEditors.h"
#include "core/interfaces/ISceneControl.h"
#include "scene/SceneBuilder.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"
#include "Mesh.h"

#include <imgui.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace {

// Extract position from transform matrix
glm::vec3 extractPosition(const glm::mat4& transform) {
    return glm::vec3(transform[3]);
}

// Extract scale (approximate - assumes uniform or near-uniform scale)
glm::vec3 extractScale(const glm::mat4& transform) {
    glm::vec3 scale;
    scale.x = glm::length(glm::vec3(transform[0]));
    scale.y = glm::length(glm::vec3(transform[1]));
    scale.z = glm::length(glm::vec3(transform[2]));
    return scale;
}

// Extract Euler angles using quaternion conversion
glm::vec3 extractEulerAngles(const glm::mat4& transform) {
    glm::vec3 scale = extractScale(transform);
    glm::mat3 rotMat(
        glm::vec3(transform[0]) / scale.x,
        glm::vec3(transform[1]) / scale.y,
        glm::vec3(transform[2]) / scale.z
    );

    glm::quat q = glm::quat_cast(rotMat);
    return glm::degrees(glm::eulerAngles(q));
}

} // anonymous namespace

void GuiPropertyEditors::drawColorPreview(const glm::vec3& color, float size) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 col = IM_COL32(
        static_cast<int>(std::clamp(color.r, 0.0f, 1.0f) * 255),
        static_cast<int>(std::clamp(color.g, 0.0f, 1.0f) * 255),
        static_cast<int>(std::clamp(color.b, 0.0f, 1.0f) * 255),
        255
    );
    drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col);
    drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(100, 100, 100, 255));
    ImGui::Dummy(ImVec2(size, size));
}

bool GuiPropertyEditors::editVec3(const char* label, glm::vec3& value, float speed, float min, float max) {
    bool changed = false;
    ImGui::PushID(label);

    ImGui::Text("%s", label);
    ImGui::SameLine(100);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::SetNextItemWidth(60);
    changed |= ImGui::DragFloat("##X", &value.x, speed, min, max, "X:%.2f");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
    ImGui::SetNextItemWidth(60);
    changed |= ImGui::DragFloat("##Y", &value.y, speed, min, max, "Y:%.2f");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(60);
    changed |= ImGui::DragFloat("##Z", &value.z, speed, min, max, "Z:%.2f");
    ImGui::PopStyleColor();

    ImGui::PopID();
    return changed;
}

bool GuiPropertyEditors::editColor(const char* label, glm::vec3& color) {
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine(100);

    float col[3] = { color.r, color.g, color.b };
    bool changed = ImGui::ColorEdit3("##color", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    if (changed) {
        color = glm::vec3(col[0], col[1], col[2]);
    }
    ImGui::SameLine();
    drawColorPreview(color);

    ImGui::PopID();
    return changed;
}

void GuiPropertyEditors::renderTransformEditor(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Transform", state.showTransformSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    // Transform mode selector
    ImGui::Text("Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Translate", state.transformMode == SceneEditorState::TransformMode::Translate)) {
        state.transformMode = SceneEditorState::TransformMode::Translate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", state.transformMode == SceneEditorState::TransformMode::Rotate)) {
        state.transformMode = SceneEditorState::TransformMode::Rotate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", state.transformMode == SceneEditorState::TransformMode::Scale)) {
        state.transformMode = SceneEditorState::TransformMode::Scale;
    }

    // Space selector
    ImGui::Text("Space:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Local", state.transformSpace == SceneEditorState::TransformSpace::Local)) {
        state.transformSpace = SceneEditorState::TransformSpace::Local;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("World", state.transformSpace == SceneEditorState::TransformSpace::World)) {
        state.transformSpace = SceneEditorState::TransformSpace::World;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Edit LocalTransform if available
    if (world.has<ecs::LocalTransform>(entity)) {
        auto& local = world.get<ecs::LocalTransform>(entity);

        if (editVec3("Position", local.position, 0.1f)) {
            // Mark for world transform update
            ecs::systems::updateWorldTransforms(world);
        }

        // Convert quaternion to euler for editing
        glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(local.rotation));
        if (editVec3("Rotation", eulerDegrees, 1.0f, -360.0f, 360.0f)) {
            local.rotation = glm::quat(glm::radians(eulerDegrees));
            ecs::systems::updateWorldTransforms(world);
        }

        if (editVec3("Scale", local.scale, 0.01f, 0.01f, 100.0f)) {
            ecs::systems::updateWorldTransforms(world);
        }

        // Uniform scale checkbox
        static bool uniformScale = true;
        ImGui::Checkbox("Uniform Scale", &uniformScale);
        if (uniformScale) {
            ImGui::SameLine();
            float avgScale = (local.scale.x + local.scale.y + local.scale.z) / 3.0f;
            ImGui::SetNextItemWidth(100);
            if (ImGui::DragFloat("##uniformScale", &avgScale, 0.01f, 0.01f, 100.0f)) {
                local.scale = glm::vec3(avgScale);
                ecs::systems::updateWorldTransforms(world);
            }
        }
    } else if (world.has<ecs::Transform>(entity)) {
        // Read-only world transform display
        const auto& transform = world.get<ecs::Transform>(entity);
        glm::vec3 pos = transform.position();

        // Decompose matrix for display
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(transform.matrix, scale, rotation, translation, skew, perspective);

        glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(rotation));

        ImGui::TextDisabled("(World Transform - Read Only)");
        ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
        ImGui::Text("Rotation: %.1f, %.1f, %.1f", eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
        ImGui::Text("Scale: %.2f, %.2f, %.2f", scale.x, scale.y, scale.z);

        // Offer to add LocalTransform
        if (ImGui::Button("Add LocalTransform")) {
            world.add<ecs::LocalTransform>(entity, translation, rotation, scale);
        }
    } else {
        ImGui::TextDisabled("No transform component");
        if (ImGui::Button("Add Transform")) {
            world.add<ecs::Transform>(entity);
            world.add<ecs::LocalTransform>(entity);
        }
    }

    ImGui::Spacing();
}

void GuiPropertyEditors::renderMaterialEditor(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Material", state.showMaterialSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    if (world.has<ecs::MaterialRef>(entity)) {
        auto& matRef = world.get<ecs::MaterialRef>(entity);
        ImGui::Text("Material ID: %u", matRef.id);
    }

    if (world.has<ecs::PBRProperties>(entity)) {
        auto& pbr = world.get<ecs::PBRProperties>(entity);

        ImGui::SliderFloat("Roughness", &pbr.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Metallic", &pbr.metallic, 0.0f, 1.0f);

        ImGui::Spacing();
        ImGui::Text("Emissive");
        ImGui::Indent();
        ImGui::SliderFloat("Intensity##emissive", &pbr.emissiveIntensity, 0.0f, 10.0f);
        editColor("Color##emissive", pbr.emissiveColor);
        ImGui::Unindent();

        if (pbr.alphaTestThreshold > 0.0f) {
            ImGui::SliderFloat("Alpha Test", &pbr.alphaTestThreshold, 0.0f, 1.0f);
        }
    } else {
        ImGui::TextDisabled("No PBR properties");
        if (ImGui::Button("Add PBR Properties")) {
            world.add<ecs::PBRProperties>(entity);
        }
    }

    // Opacity
    if (world.has<ecs::Opacity>(entity)) {
        auto& opacity = world.get<ecs::Opacity>(entity);
        ImGui::SliderFloat("Opacity", &opacity.value, 0.0f, 1.0f);
    }

    // Hue Shift
    if (world.has<ecs::HueShift>(entity)) {
        auto& hue = world.get<ecs::HueShift>(entity);
        ImGui::SliderFloat("Hue Shift", &hue.value, -1.0f, 1.0f);
    }

    ImGui::Spacing();
}

void GuiPropertyEditors::renderTransformDisplay(const glm::mat4& transform, bool& showSection) {
    if (!ImGui::CollapsingHeader("Transform", showSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }
    showSection = true;

    glm::vec3 position = extractPosition(transform);
    glm::vec3 scale = extractScale(transform);
    glm::vec3 rotation = extractEulerAngles(transform);

    ImGui::Text("Position");
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "X: %.3f", position.x);
    ImGui::SameLine(100);
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Y: %.3f", position.y);
    ImGui::SameLine(200);
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "Z: %.3f", position.z);
    ImGui::Unindent();

    ImGui::Text("Rotation (deg)");
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "P: %.1f", rotation.x);
    ImGui::SameLine(100);
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Y: %.1f", rotation.y);
    ImGui::SameLine(200);
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "R: %.1f", rotation.z);
    ImGui::Unindent();

    ImGui::Text("Scale");
    ImGui::Indent();
    if (std::abs(scale.x - scale.y) < 0.001f && std::abs(scale.y - scale.z) < 0.001f) {
        ImGui::Text("Uniform: %.3f", scale.x);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "X: %.3f", scale.x);
        ImGui::SameLine(100);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Y: %.3f", scale.y);
        ImGui::SameLine(200);
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "Z: %.3f", scale.z);
    }
    ImGui::Unindent();

    ImGui::Spacing();
}

void GuiPropertyEditors::renderMaterialDisplay(const ecs::RenderData& renderData, bool& showSection) {
    if (!ImGui::CollapsingHeader("Material", showSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }
    showSection = true;

    ImGui::Text("Material ID: %u", renderData.materialId);
    ImGui::Text("Roughness: %.2f", renderData.roughness);
    ImGui::Text("Metallic: %.2f", renderData.metallic);
    ImGui::Text("Opacity: %.2f", renderData.opacity);

    if (renderData.alphaTestThreshold > 0.0f) {
        ImGui::Text("Alpha Test: %.2f", renderData.alphaTestThreshold);
    }

    ImGui::Spacing();

    // Emissive info
    if (renderData.emissiveIntensity > 0.0f) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
        ImGui::Text("Emissive");
        ImGui::PopStyleColor();
        ImGui::Indent();
        ImGui::Text("Intensity: %.2f", renderData.emissiveIntensity);
        ImGui::Text("Color:");
        ImGui::SameLine();
        drawColorPreview(renderData.emissiveColor);
        ImGui::SameLine();
        ImGui::Text("(%.2f, %.2f, %.2f)",
            renderData.emissiveColor.r,
            renderData.emissiveColor.g,
            renderData.emissiveColor.b);
        ImGui::Unindent();
    }

    ImGui::Spacing();
}

void GuiPropertyEditors::renderRenderableInfoDisplay(const ecs::RenderData& renderData,
                                                     int objectIndex, bool& showSection) {
    if (!ImGui::CollapsingHeader("Info", showSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }
    showSection = true;

    ImGui::Text("Casts Shadow: %s", renderData.castsShadow ? "Yes" : "No");
    ImGui::Text("PBR Flags: 0x%X", renderData.pbrFlags);

    if (renderData.mesh) {
        ImGui::Spacing();
        ImGui::Text("Mesh Info");
        ImGui::Indent();
        ImGui::Text("Index Count: %u", renderData.mesh->getIndexCount());
        ImGui::Text("Vertex Count: %u", renderData.mesh->getVertexCount());
        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Text("Object Index: %d", objectIndex);
    if (renderData.gpuSkinned) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "(GPU Skinned Character)");
    }
}

std::vector<ecs::RenderData> GuiPropertyEditors::collectRenderables(ISceneControl& sceneControl) {
    std::vector<ecs::RenderData> renderables;

    // Look the world up per frame through the scene builder — it is bound late
    // during async init and may not exist yet.
    SceneBuilder& sceneBuilder = sceneControl.getSceneBuilder();
    const ecs::World* world = sceneBuilder.getECSWorld();
    if (!world) return renderables;

    const auto& entities = sceneBuilder.getSceneEntities();
    renderables.reserve(entities.size());
    for (ecs::Entity e : entities) {
        if (!world->valid(e)) {
            renderables.emplace_back();
            continue;
        }
        ecs::RenderData row = ecs::extractRenderData(*world, e);
        // extractRenderData does not populate gpuSkinned; derive it from the tag.
        row.gpuSkinned = world->has<ecs::GPUSkinned>(e);
        renderables.push_back(row);
    }
    return renderables;
}

const char* GuiPropertyEditors::renderableTypeName(const ecs::RenderData& renderData) {
    if (renderData.gpuSkinned) {
        return "Character";
    }
    if (renderData.emissiveIntensity > 0.0f) {
        return "Emissive";
    }
    if (renderData.alphaTestThreshold > 0.0f) {
        return "Alpha-Test";
    }
    return "Object";
}
