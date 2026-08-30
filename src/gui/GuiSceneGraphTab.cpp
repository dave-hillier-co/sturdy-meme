#include "GuiSceneGraphTab.h"
#include "GuiStyle.h"
#include "GuiPropertyEditors.h"
#include "core/interfaces/ISceneControl.h"
#include "scene/SceneBuilder.h"
#include "ecs/Components.h"
#include "Mesh.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <cmath>

namespace {
    // Extract position from transform matrix
    glm::vec3 extractPosition(const glm::mat4& transform) {
        return glm::vec3(transform[3]);
    }

    // Get a display name for a renderable based on its properties
    const char* getObjectTypeName(const ecs::RenderData& obj) {
        if (obj.gpuSkinned) {
            return "Character";
        }
        if (obj.emissiveIntensity > 0.0f) {
            return "Emissive";
        }
        if (obj.alphaTestThreshold > 0.0f) {
            return "Alpha-Test";
        }
        return "Object";
    }
}

void GuiSceneGraphTab::render(ISceneControl& sceneControl, SceneGraphTabState& state) {
    SceneBuilder& sceneBuilder = sceneControl.getSceneBuilder();

    // Reconstruct render rows from the ECS each frame so the inspector reflects
    // live component state (transform, opacity, material) rather than a mirror.
    // Indices stay parallel to getSceneEntities() so selection is stable.
    std::vector<ecs::RenderData> renderables;
    const ecs::World* world = sceneBuilder.getECSWorld();
    if (world) {
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
    }

    ImGui::Spacing();

    // Header with object count
    GuiStyle::sectionHeader("SCENE GRAPH");
    ImGui::TextDisabled("(%zu objects)", renderables.size());
    ImGui::Spacing();

    // Filter input
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter objects...", state.filterText, sizeof(state.filterText));

    ImGui::Spacing();

    // Object list (left side) - using child window for scroll
    float listHeight = ImGui::GetContentRegionAvail().y * 0.45f;
    if (ImGui::BeginChild("ObjectList", ImVec2(-1, listHeight), true)) {
        for (size_t i = 0; i < renderables.size(); ++i) {
            const ecs::RenderData& obj = renderables[i];

            // Build display name
            char displayName[128];
            const char* typeName = getObjectTypeName(obj);
            snprintf(displayName, sizeof(displayName), "[%zu] %s", i, typeName);

            // Apply filter
            if (state.filterText[0] != '\0') {
                bool matches = false;
                // Case-insensitive search
                char lowerFilter[256];
                char lowerName[128];
                strncpy(lowerFilter, state.filterText, sizeof(lowerFilter) - 1);
                lowerFilter[sizeof(lowerFilter) - 1] = '\0';
                strncpy(lowerName, displayName, sizeof(lowerName) - 1);
                lowerName[sizeof(lowerName) - 1] = '\0';

                for (char* p = lowerFilter; *p; ++p) *p = static_cast<char>(tolower(*p));
                for (char* p = lowerName; *p; ++p) *p = static_cast<char>(tolower(*p));

                if (strstr(lowerName, lowerFilter) != nullptr) {
                    matches = true;
                }
                // Also match by type name
                char lowerType[64];
                strncpy(lowerType, typeName, sizeof(lowerType) - 1);
                lowerType[sizeof(lowerType) - 1] = '\0';
                for (char* p = lowerType; *p; ++p) *p = static_cast<char>(tolower(*p));
                if (strstr(lowerType, lowerFilter) != nullptr) {
                    matches = true;
                }

                if (!matches) continue;
            }

            bool isSelected = (state.selectedObjectIndex == static_cast<int>(i));

            // Color code by type
            ImVec4 itemColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);  // Default white
            if (obj.gpuSkinned) {
                itemColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);  // Green for characters
            } else if (obj.emissiveIntensity > 0.0f) {
                itemColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);  // Yellow for emissive
            } else if (!obj.castsShadow) {
                itemColor = ImVec4(0.6f, 0.6f, 0.8f, 1.0f);  // Blue-ish for non-shadow casters
            }

            ImGui::PushStyleColor(ImGuiCol_Text, itemColor);

            if (ImGui::Selectable(displayName, isSelected)) {
                state.selectedObjectIndex = static_cast<int>(i);
            }

            ImGui::PopStyleColor();

            // Tooltip with quick info
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                glm::vec3 pos = extractPosition(obj.transform);
                ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
                ImGui::Text("Material ID: %u", obj.materialId);
                if (obj.emissiveIntensity > 0.0f) {
                    ImGui::Text("Emissive: %.2f", obj.emissiveIntensity);
                }
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Properties panel for selected object
    GuiStyle::sectionHeader("PROPERTIES");

    ImGui::Spacing();

    if (state.selectedObjectIndex >= 0 && state.selectedObjectIndex < static_cast<int>(renderables.size())) {
        const ecs::RenderData& selected = renderables[static_cast<size_t>(state.selectedObjectIndex)];

        // Highlight indicator for selected object
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();

        // Draw selection indicator bar at the side
        float barWidth = 4.0f;
        ImVec2 barStart(windowPos.x, ImGui::GetCursorScreenPos().y);
        ImVec2 barEnd(windowPos.x + barWidth, barStart.y + 200.0f);
        drawList->AddRectFilled(barStart, barEnd, IM_COL32(100, 200, 100, 255));

        if (ImGui::BeginChild("Properties", ImVec2(-1, -1), false)) {
            // Transform section (shared read-only display)
            GuiPropertyEditors::renderTransformDisplay(selected.transform, state.showTransformSection);

            // Material section (shared read-only display)
            GuiPropertyEditors::renderMaterialDisplay(selected, state.showMaterialSection);

            // Info section
            if (ImGui::CollapsingHeader("Info", state.showInfoSection ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                state.showInfoSection = true;

                ImGui::Text("Casts Shadow: %s", selected.castsShadow ? "Yes" : "No");
                ImGui::Text("PBR Flags: 0x%X", selected.pbrFlags);

                if (selected.mesh) {
                    ImGui::Spacing();
                    ImGui::Text("Mesh Info");
                    ImGui::Indent();
                    ImGui::Text("Index Count: %u", selected.mesh->getIndexCount());
                    ImGui::Text("Vertex Count: %u", selected.mesh->getVertexCount());
                    ImGui::Unindent();
                }

                // Index info
                ImGui::Spacing();
                ImGui::Text("Object Index: %d", state.selectedObjectIndex);
                if (state.selectedObjectIndex >= 0 &&
                    static_cast<size_t>(state.selectedObjectIndex) < renderables.size() &&
                    renderables[static_cast<size_t>(state.selectedObjectIndex)].gpuSkinned) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "(GPU Skinned Character)");
                }
            }
        }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Select an object to view properties");

        ImGui::Spacing();
        ImGui::TextDisabled("Tips:");
        ImGui::BulletText("Click on an object in the list");
        ImGui::BulletText("Use filter to search by type");
        ImGui::BulletText("Types: Player, Tree, Emissive, etc.");
    }
}
