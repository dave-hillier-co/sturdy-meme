#include "GuiNPCTab.h"
#include "core/interfaces/IPlayerControl.h"
#include "SceneBuilder.h"
#include "npc/NPCSimulation.h"
#include "ecs/World.h"
#include "ecs/Components.h"

#include "GuiStyle.h"

#include <imgui.h>

void GuiNPCTab::render(IPlayerControl& playerControl) {
    auto& sceneBuilder = playerControl.getSceneBuilder();

    auto* npcSim = sceneBuilder.getNPCSimulation();
    if (!npcSim) {
        ImGui::TextDisabled("NPC simulation not available");
        return;
    }

    GuiStyle::sectionHeader("NPC LOD");

    ecs::World* ecsWorld = sceneBuilder.getECSWorld();
    size_t npcCount = npcSim->getNPCCount();

    // Helper: read an NPC's ECS LOD controller (authoritative per-NPC state).
    auto getLOD = [&](size_t i) -> const ecs::NPCLODController* {
        if (!ecsWorld) return nullptr;
        ecs::Entity e = npcSim->getNPCEntity(i);
        if (e == ecs::NullEntity || !ecsWorld->valid(e) ||
            !ecsWorld->has<ecs::NPCLODController>(e)) {
            return nullptr;
        }
        return &ecsWorld->get<ecs::NPCLODController>(e);
    };

    if (npcCount == 0) {
        ImGui::TextDisabled("No NPCs in scene");
        return;
    }

    // Count NPCs per LOD level
    uint32_t virtualCount = 0, bulkCount = 0, realCount = 0;
    for (size_t i = 0; i < npcCount; ++i) {
        const ecs::NPCLODController* lod = getLOD(i);
        if (!lod) continue;
        switch (lod->level) {
            case ecs::NPCLODLevel::Virtual: virtualCount++; break;
            case ecs::NPCLODLevel::Bulk: bulkCount++; break;
            case ecs::NPCLODLevel::Real: realCount++; break;
        }
    }

    // LOD colors
    ImVec4 colorReal(0.2f, 1.0f, 0.2f, 1.0f);     // Green
    ImVec4 colorBulk(1.0f, 0.8f, 0.2f, 1.0f);     // Yellow
    ImVec4 colorVirtual(1.0f, 0.3f, 0.3f, 1.0f);  // Red

    ImGui::Text("Total NPCs: %zu", npcCount);

    // Summary counts
    ImGui::TextColored(colorReal, "Real (<25m):");
    ImGui::SameLine();
    ImGui::Text("%u", realCount);
    ImGui::SameLine();
    ImGui::TextColored(colorBulk, "  Bulk (25-50m):");
    ImGui::SameLine();
    ImGui::Text("%u", bulkCount);
    ImGui::SameLine();
    ImGui::TextColored(colorVirtual, "  Virtual (>50m):");
    ImGui::SameLine();
    ImGui::Text("%u", virtualCount);

    ImGui::Spacing();

    // Per-NPC details (collapsible)
    if (ImGui::TreeNode("NPC Details")) {
        for (size_t i = 0; i < npcCount; ++i) {
            const ecs::NPCLODController* lod = getLOD(i);

            const char* lodName = "Unknown";
            ImVec4 lodColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            uint32_t frames = 0;

            if (lod) {
                frames = lod->framesSinceUpdate;
                switch (lod->level) {
                    case ecs::NPCLODLevel::Real:
                        lodName = "Real";
                        lodColor = colorReal;
                        break;
                    case ecs::NPCLODLevel::Bulk:
                        lodName = "Bulk";
                        lodColor = colorBulk;
                        break;
                    case ecs::NPCLODLevel::Virtual:
                        lodName = "Virtual";
                        lodColor = colorVirtual;
                        break;
                }
            }

            ImGui::Text("NPC %zu:", i);
            ImGui::SameLine();
            ImGui::TextColored(lodColor, "%s", lodName);
            ImGui::SameLine();
            ImGui::TextDisabled("(frames: %u)", frames);
        }
        ImGui::TreePop();
    }

    // LOD toggle
    bool lodEnabled = npcSim->isLODEnabled();
    if (ImGui::Checkbox("Enable NPC LOD", &lodEnabled)) {
        npcSim->setLODEnabled(lodEnabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Virtual: >50m, no render, update every ~10s\n"
                          "Bulk: 25-50m, reduced updates ~1s\n"
                          "Real: <25m, full animation every frame");
    }
}
