#pragma once

#include <imgui.h>

namespace GuiStyle {
    // Semantic colors shared by all panels. Use these instead of hardcoded
    // ImVec4 literals so the whole debug UI stays visually consistent.
    inline constexpr ImVec4 kHeaderAccent{0.50f, 0.80f, 1.00f, 1.00f};
    inline constexpr ImVec4 kGood{0.40f, 0.90f, 0.40f, 1.00f};
    inline constexpr ImVec4 kWarning{1.00f, 0.80f, 0.40f, 1.00f};
    inline constexpr ImVec4 kBad{1.00f, 0.40f, 0.40f, 1.00f};
    inline constexpr ImVec4 kDim{0.60f, 0.60f, 0.65f, 1.00f};

    void apply();

    // Renders a consistent section header (accent-colored separator text).
    // Replaces the hand-rolled PushStyleColor/Text/PopStyleColor pattern.
    void sectionHeader(const char* label);
}
