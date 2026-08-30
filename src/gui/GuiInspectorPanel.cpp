#include "GuiInspectorPanel.h"
#include "GuiPropertyEditors.h"
#include "core/interfaces/ISceneControl.h"
#include "scene/SceneBuilder.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/Systems.h"

#include <imgui.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace {

// Shared editors (transform/material sections, color/vec3 widgets) live in
// GuiPropertyEditors and are also used by GuiSceneGraphTab.
using GuiPropertyEditors::editColor;

// Render components section
void renderComponentsSection(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Components", state.showComponentsSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    // List existing components with remove buttons
    ImGui::BeginChild("ComponentList", ImVec2(0, 200), true);

    // Light components
    if (world.has<ecs::PointLightComponent>(entity)) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.1f, 0.5f));
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = world.get<ecs::PointLightComponent>(entity);
            editColor("Color##pl", light.properties.color);
            ImGui::SliderFloat("Intensity##pl", &light.properties.intensity, 0.0f, 20.0f);
            ImGui::SliderFloat("Radius##pl", &light.radius, 0.1f, 100.0f);
            ImGui::Checkbox("Enabled##pl", &light.properties.enabled);
            ImGui::Checkbox("Cast Shadows##pl", &light.properties.castsShadows);

            if (ImGui::Button("Remove##pl")) {
                world.remove<ecs::PointLightComponent>(entity);
            }
        }
        ImGui::PopStyleColor();
    }

    if (world.has<ecs::SpotLightComponent>(entity)) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.1f, 0.5f));
        if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = world.get<ecs::SpotLightComponent>(entity);
            editColor("Color##sl", light.properties.color);
            ImGui::SliderFloat("Intensity##sl", &light.properties.intensity, 0.0f, 20.0f);
            ImGui::SliderFloat("Radius##sl", &light.radius, 0.1f, 100.0f);
            ImGui::SliderFloat("Inner Angle##sl", &light.innerConeAngle, 1.0f, 89.0f);
            ImGui::SliderFloat("Outer Angle##sl", &light.outerConeAngle, 1.0f, 90.0f);
            ImGui::Checkbox("Enabled##sl", &light.properties.enabled);

            if (ImGui::Button("Remove##sl")) {
                world.remove<ecs::SpotLightComponent>(entity);
            }
        }
        ImGui::PopStyleColor();
    }

    if (world.has<ecs::LightFlickerComponent>(entity)) {
        if (ImGui::CollapsingHeader("Light Flicker")) {
            auto& flicker = world.get<ecs::LightFlickerComponent>(entity);
            ImGui::SliderFloat("Amount", &flicker.flickerAmount, 0.0f, 1.0f);
            ImGui::SliderFloat("Speed", &flicker.flickerSpeed, 0.0f, 20.0f);
            ImGui::SliderFloat("Noise Scale", &flicker.noiseScale, 0.1f, 10.0f);

            if (ImGui::Button("Remove##flicker")) {
                world.remove<ecs::LightFlickerComponent>(entity);
            }
        }
    }

    // Selection outline
    if (world.has<ecs::SelectionOutline>(entity)) {
        if (ImGui::CollapsingHeader("Selection Outline")) {
            auto& outline = world.get<ecs::SelectionOutline>(entity);
            editColor("Color##outline", outline.color);
            ImGui::SliderFloat("Thickness", &outline.thickness, 0.5f, 10.0f);
            ImGui::SliderFloat("Pulse Speed", &outline.pulseSpeed, 0.0f, 5.0f);

            if (ImGui::Button("Remove##outline")) {
                world.remove<ecs::SelectionOutline>(entity);
            }
        }
    }

    // LOD Controller
    if (world.has<ecs::LODController>(entity)) {
        if (ImGui::CollapsingHeader("LOD Controller")) {
            auto& lod = world.get<ecs::LODController>(entity);
            ImGui::Text("Current Level: %u", lod.currentLevel);
            ImGui::DragFloat("Near", &lod.thresholds[0], 1.0f, 1.0f, 1000.0f);
            ImGui::DragFloat("Mid", &lod.thresholds[1], 1.0f, 1.0f, 1000.0f);
            ImGui::DragFloat("Far", &lod.thresholds[2], 1.0f, 1.0f, 1000.0f);

            if (ImGui::Button("Remove##lod")) {
                world.remove<ecs::LODController>(entity);
            }
        }
    }

    // Bounding sphere
    if (world.has<ecs::BoundingSphere>(entity)) {
        if (ImGui::CollapsingHeader("Bounding Sphere")) {
            auto& bounds = world.get<ecs::BoundingSphere>(entity);
            ImGui::DragFloat3("Center", &bounds.center.x, 0.1f);
            ImGui::DragFloat("Radius", &bounds.radius, 0.1f, 0.01f, 1000.0f);

            if (ImGui::Button("Remove##bounds")) {
                world.remove<ecs::BoundingSphere>(entity);
            }
        }
    }

    ImGui::EndChild();

    // Add component button
    if (ImGui::Button("Add Component...")) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!world.has<ecs::PointLightComponent>(entity) && ImGui::MenuItem("Point Light")) {
            world.add<ecs::PointLightComponent>(entity, glm::vec3(1.0f), 1.0f, 10.0f);
            world.add<ecs::LightSourceTag>(entity);
        }
        if (!world.has<ecs::SpotLightComponent>(entity) && ImGui::MenuItem("Spot Light")) {
            world.add<ecs::SpotLightComponent>(entity, glm::vec3(1.0f), 1.0f);
            world.add<ecs::LightSourceTag>(entity);
        }
        if (!world.has<ecs::LightFlickerComponent>(entity) && ImGui::MenuItem("Light Flicker")) {
            world.add<ecs::LightFlickerComponent>(entity);
        }
        ImGui::Separator();
        if (!world.has<ecs::SelectionOutline>(entity) && ImGui::MenuItem("Selection Outline")) {
            world.add<ecs::SelectionOutline>(entity);
        }
        if (!world.has<ecs::LODController>(entity) && ImGui::MenuItem("LOD Controller")) {
            world.add<ecs::LODController>(entity);
        }
        if (!world.has<ecs::BoundingSphere>(entity) && ImGui::MenuItem("Bounding Sphere")) {
            world.add<ecs::BoundingSphere>(entity, glm::vec3(0.0f), 1.0f);
        }
        ImGui::Separator();
        if (!world.has<ecs::Opacity>(entity) && ImGui::MenuItem("Opacity")) {
            world.add<ecs::Opacity>(entity, 1.0f);
        }
        if (!world.has<ecs::HueShift>(entity) && ImGui::MenuItem("Hue Shift")) {
            world.add<ecs::HueShift>(entity, 0.0f);
        }
        if (!world.has<ecs::PBRProperties>(entity) && ImGui::MenuItem("PBR Properties")) {
            world.add<ecs::PBRProperties>(entity);
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
}

// Render tags section
void renderTagsSection(ecs::World& world, ecs::Entity entity, SceneEditorState& state) {
    if (!ImGui::CollapsingHeader("Tags", state.showTagsSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }

    // Display current tags
    ImGui::BeginChild("TagList", ImVec2(0, 100), true);

    // Check and display each tag type
    if (world.has<ecs::CastsShadow>(entity)) {
        ImGui::BulletText("Casts Shadow");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##shadow")) {
            world.remove<ecs::CastsShadow>(entity);
        }
    }
    if (world.has<ecs::Visible>(entity)) {
        ImGui::BulletText("Visible");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##visible")) {
            world.remove<ecs::Visible>(entity);
        }
    }
    if (world.has<ecs::Transparent>(entity)) {
        ImGui::BulletText("Transparent");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##transparent")) {
            world.remove<ecs::Transparent>(entity);
        }
    }
    if (world.has<ecs::Reflective>(entity)) {
        ImGui::BulletText("Reflective");
        ImGui::SameLine(200);
        if (ImGui::SmallButton("X##reflective")) {
            world.remove<ecs::Reflective>(entity);
        }
    }
    if (world.has<ecs::LightSourceTag>(entity)) {
        ImGui::BulletText("Light Source");
    }
    if (world.has<ecs::PlayerTag>(entity)) {
        ImGui::BulletText("Player");
    }
    if (world.has<ecs::NPCTag>(entity)) {
        ImGui::BulletText("NPC");
    }

    ImGui::EndChild();

    // Add tag button
    if (ImGui::Button("Add Tag...")) {
        ImGui::OpenPopup("AddTagPopup");
    }

    if (ImGui::BeginPopup("AddTagPopup")) {
        if (!world.has<ecs::CastsShadow>(entity) && ImGui::MenuItem("Casts Shadow")) {
            world.add<ecs::CastsShadow>(entity);
        }
        if (!world.has<ecs::Visible>(entity) && ImGui::MenuItem("Visible")) {
            world.add<ecs::Visible>(entity);
        }
        if (!world.has<ecs::Transparent>(entity) && ImGui::MenuItem("Transparent")) {
            world.add<ecs::Transparent>(entity);
        }
        if (!world.has<ecs::Reflective>(entity) && ImGui::MenuItem("Reflective")) {
            world.add<ecs::Reflective>(entity);
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
}

} // anonymous namespace

void GuiInspectorPanel::render(ISceneControl& sceneControl, SceneEditorState& state) {
    ecs::World* world = sceneControl.getECSWorld();

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
    ImGui::Text("INSPECTOR");
    ImGui::PopStyleColor();

    if (!world) {
        ImGui::TextDisabled("ECS World not available");
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Check if an entity is selected
    if (state.selectedEntity == ecs::NullEntity) {
        ImGui::TextDisabled("No entity selected");
        ImGui::Spacing();
        ImGui::TextDisabled("Select an entity in the Hierarchy panel");
        ImGui::TextDisabled("to view and edit its properties.");
        return;
    }

    // Validate entity
    if (!world->valid(state.selectedEntity)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid entity selected");
        if (ImGui::Button("Clear Selection")) {
            state.clearSelection();
        }
        return;
    }

    // Entity header
    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(state.selectedEntity));

    // Name editing (if DebugName component exists)
    if (world->has<ecs::DebugName>(state.selectedEntity)) {
        auto& debugName = world->get<ecs::DebugName>(state.selectedEntity);
        ImGui::Text("Name: %s", debugName.name ? debugName.name : "(unnamed)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Scrollable content area
    if (ImGui::BeginChild("InspectorContent", ImVec2(0, 0), false)) {
        GuiPropertyEditors::renderTransformEditor(*world, state.selectedEntity, state);
        GuiPropertyEditors::renderMaterialEditor(*world, state.selectedEntity, state);
        renderComponentsSection(*world, state.selectedEntity, state);
        renderTagsSection(*world, state.selectedEntity, state);
    }
    ImGui::EndChild();
}
