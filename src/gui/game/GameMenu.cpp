#include "GameMenu.h"

#include "gui/GuiStyle.h"
#include "scene/InputSystem.h"
#include "core/GameSettings.h"
#include "core/PerformanceToggles.h"

#include <imgui.h>

#include <array>
#include <cstring>

namespace {

// Curated player-facing quality toggles: internal PerformanceToggles name
// paired with a friendly label. Everything else stays debug-only.
struct QualityEntry {
    const char* toggleName;
    const char* label;
};

constexpr std::array<QualityEntry, 6> kQualityEntries = {{
    {"shadowPass", "Shadows"},
    {"bloom", "Bloom"},
    {"ssr", "Screen-Space Reflections"},
    {"froxelFog", "Volumetric Fog"},
    {"grassDraw", "Grass"},
    {"leavesDraw", "Tree Leaves"},
}};

// A wide, tall button suited to menu navigation.
bool menuButton(const char* label, float width) {
    return ImGui::Button(label, ImVec2(width, ImGui::GetFontSize() * 2.2f));
}

}  // anonymous namespace

void GameMenu::open() {
    if (state_ != State::Hidden) return;
    state_ = State::PauseRoot;

    // Release relative mouse mode so the cursor can reach the menu; remember
    // whether mouse-look was on so closing restores it.
    if (hooks_.input) {
        restoreMouseLook_ = hooks_.input->isMouseLookEnabled();
        hooks_.input->setMouseLookEnabled(false);
    }
}

void GameMenu::close() {
    if (state_ == State::Hidden) return;
    state_ = State::Hidden;

    if (hooks_.input && restoreMouseLook_) {
        hooks_.input->setMouseLookEnabled(true);
    }
    restoreMouseLook_ = false;
}

void GameMenu::handleEscape() {
    switch (state_) {
        case State::Hidden:    open(); break;
        case State::Settings:  state_ = State::PauseRoot; break;
        case State::PauseRoot: close(); break;
    }
}

void GameMenu::render() {
    if (state_ == State::Hidden) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Full-viewport dim behind all ImGui windows (in front of the scene).
    ImGui::GetBackgroundDrawList(const_cast<ImGuiViewport*>(viewport))
        ->AddRectFilled(viewport->Pos,
                        ImVec2(viewport->Pos.x + viewport->Size.x,
                               viewport->Pos.y + viewport->Size.y),
                        IM_COL32(6, 8, 12, 170));

    // Centered borderless window.
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                                   viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking;

    // Deliberate, clean look: solid panel, generous padding, soft corners.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ImGui::GetFontSize() * 3.0f, ImGui::GetFontSize() * 2.2f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(8.0f, ImGui::GetFontSize() * 0.7f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.10f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.28f, 0.34f, 0.60f));

    if (ImGui::Begin("##GameMenu", nullptr, flags)) {
        // No dedicated large font is loaded, so scale the window's text up.
        ImGui::SetWindowFontScale(state_ == State::PauseRoot ? 1.6f : 1.25f);

        if (state_ == State::PauseRoot) {
            renderPauseRoot();
        } else {
            renderSettings();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void GameMenu::renderPauseRoot() {
    const float buttonWidth = ImGui::GetFontSize() * 11.0f;

    ImGui::PushStyleColor(ImGuiCol_Text, GuiStyle::kHeaderAccent);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         (buttonWidth - ImGui::CalcTextSize("PAUSED").x) * 0.5f);
    ImGui::TextUnformatted("PAUSED");
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));

    if (menuButton("Resume", buttonWidth)) {
        close();
    }
    if (menuButton("Settings", buttonWidth)) {
        state_ = State::Settings;
    }

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.4f));

    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));
    if (menuButton("Quit", buttonWidth)) {
        if (hooks_.requestQuit) hooks_.requestQuit();
    }
    ImGui::PopStyleColor(2);
}

void GameMenu::renderSettings() {
    const float contentWidth = ImGui::GetFontSize() * 18.0f;

    ImGui::PushStyleColor(ImGuiCol_Text, GuiStyle::kHeaderAccent);
    ImGui::TextUnformatted("SETTINGS");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(contentWidth, ImGui::GetFontSize() * 0.3f));

    ImGui::PushItemWidth(contentWidth * 0.55f);

    // Widgets read and write the persistent GameSettings struct (single
    // source of truth); every change is also applied to the live system
    // immediately, and `changed` triggers a save through the hook below.
    GameSettings* settings = hooks_.settings;
    bool changed = false;

    // Mouse
    GuiStyle::sectionHeader("Mouse");
    if (hooks_.input && settings) {
        if (ImGui::SliderFloat("Sensitivity", &settings->mouseSensitivity,
                               0.1f, 3.0f, "%.2f")) {
            hooks_.input->setMouseSensitivity(settings->mouseSensitivity);
            changed = true;
        }
        if (ImGui::Checkbox("Invert Y Axis", &settings->invertMouseY)) {
            hooks_.input->setInvertMouseY(settings->invertMouseY);
            changed = true;
        }
    } else {
        ImGui::TextColored(GuiStyle::kDim, "Input system unavailable");
    }

    // Display
    GuiStyle::sectionHeader("Display");
    if (hooks_.window && settings) {
        // Track external transitions (e.g. the OS window button) so the
        // checkbox always reflects the real window state.
        settings->fullscreen =
            (SDL_GetWindowFlags(hooks_.window) & SDL_WINDOW_FULLSCREEN) != 0;
        if (ImGui::Checkbox("Fullscreen", &settings->fullscreen)) {
            SDL_SetWindowFullscreen(hooks_.window, settings->fullscreen);
            changed = true;
        }
    }

    // Quality (curated subset of the data-driven performance toggles)
    GuiStyle::sectionHeader("Quality");
    PerformanceToggles* perf =
        hooks_.performanceToggles ? hooks_.performanceToggles() : nullptr;
    if (perf) {
        auto toggles = perf->getAllToggles();
        for (const QualityEntry& entry : kQualityEntries) {
            for (const auto& toggle : toggles) {
                if (toggle.name != entry.toggleName) continue;
                if (ImGui::Checkbox(entry.label, toggle.value) && settings) {
                    // Persist only toggles the player explicitly changed.
                    settings->qualityOverrides[entry.toggleName] = *toggle.value;
                    changed = true;
                }
                break;
            }
        }
    } else {
        ImGui::TextColored(GuiStyle::kDim, "Still loading...");
    }

    ImGui::PopItemWidth();

    if (changed && hooks_.settingsChanged) {
        hooks_.settingsChanged();
    }

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));
    if (menuButton("Back", ImGui::GetFontSize() * 8.0f)) {
        state_ = State::PauseRoot;
    }
}
