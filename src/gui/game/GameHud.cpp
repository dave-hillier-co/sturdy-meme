#include "GameHud.h"

#include "../GuiStyle.h"
#include "Camera.h"
#include "core/interfaces/ITimeSystem.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265358979323846f;

ImU32 colU32(const ImVec4& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

// Wrap an angle difference into [-180, 180)
float wrapDegrees(float d) {
    d = std::fmod(d + 180.0f, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d - 180.0f;
}

// Semi-transparent dark backing shared by all HUD elements
constexpr ImU32 kBacking = IM_COL32(12, 14, 18, 96);

void drawCompassStrip(ImDrawList* dl, const ImGuiViewport* viewport, float yaw) {
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float fontSize = ImGui::GetFontSize();

    // Geometry scaled from the viewport, clamped to stay legible
    const float stripW = std::clamp(size.x * 0.30f, 240.0f, 520.0f);
    const float stripH = fontSize * 1.8f;
    const float cx = origin.x + size.x * 0.5f;
    const float top = origin.y + std::max(size.y * 0.035f, fontSize);
    const float bottom = top + stripH;
    const float midY = top + stripH * 0.5f;

    // Backing band
    dl->AddRectFilled(ImVec2(cx - stripW * 0.5f, top), ImVec2(cx + stripW * 0.5f, bottom),
                      kBacking, stripH * 0.5f);

    // Camera bearing: front = (cos yaw, sin yaw) in XZ, north is -Z at
    // yaw = -90 (matching the debug compass), east is +X at yaw = 0.
    // So bearing (0 = N, 90 = E) is yaw + 90.
    float bearing = std::fmod(yaw + 90.0f, 360.0f);
    if (bearing < 0.0f) bearing += 360.0f;

    const float visibleDeg = 120.0f;         // degrees spanned by the strip
    const float halfVisible = visibleDeg * 0.5f;
    const float pxPerDeg = stripW / visibleDeg;
    const float edgePad = stripH * 0.6f;     // keep marks off the rounded caps

    const char* cardinalLabels[4] = {"N", "E", "S", "W"};

    // Ticks every 15 degrees; taller every 45; cardinal letters at 0/90/180/270
    for (int deg = 0; deg < 360; deg += 15) {
        const float delta = wrapDegrees(static_cast<float>(deg) - bearing);
        if (std::fabs(delta) > halfVisible) continue;

        const float x = cx + delta * pxPerDeg;
        if (x < cx - stripW * 0.5f + edgePad || x > cx + stripW * 0.5f - edgePad) continue;

        // Fade marks out toward the strip edges
        const float edgeFade = 1.0f - std::pow(std::fabs(delta) / halfVisible, 3.0f);

        if (deg % 90 == 0) {
            const int cardinal = deg / 90;
            const bool isNorth = (cardinal == 0);
            const float labelSize = fontSize * (isNorth ? 1.0f : 0.9f);
            const ImVec2 textSize =
                ImGui::GetFont()->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, cardinalLabels[cardinal]);
            const ImU32 labelCol = isNorth ? colU32(GuiStyle::kHeaderAccent, edgeFade)
                                           : IM_COL32(225, 228, 235,
                                                      static_cast<int>(215 * edgeFade));
            dl->AddText(ImGui::GetFont(), labelSize,
                        ImVec2(x - textSize.x * 0.5f, midY - textSize.y * 0.5f),
                        labelCol, cardinalLabels[cardinal]);
        } else {
            const bool major = (deg % 45 == 0);
            const float tickH = stripH * (major ? 0.34f : 0.18f);
            const int alpha = static_cast<int>((major ? 190 : 120) * edgeFade);
            dl->AddLine(ImVec2(x, midY - tickH * 0.5f), ImVec2(x, midY + tickH * 0.5f),
                        IM_COL32(210, 214, 222, alpha), major ? 2.0f : 1.0f);
        }
    }

    // Center heading marker: small triangle just below the band
    const float triW = fontSize * 0.38f;
    const float triH = fontSize * 0.34f;
    const ImU32 accent = colU32(GuiStyle::kHeaderAccent, 0.95f);
    dl->AddTriangleFilled(ImVec2(cx, bottom + 2.0f),
                          ImVec2(cx - triW, bottom + 2.0f + triH),
                          ImVec2(cx + triW, bottom + 2.0f + triH), accent);
}

void drawSunGlyph(ImDrawList* dl, ImVec2 center, float r) {
    const ImU32 sunCol = colU32(GuiStyle::kWarning, 0.95f);
    dl->AddCircleFilled(center, r, sunCol);
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * kPi / 4.0f;
        const ImVec2 dir(std::cos(a), std::sin(a));
        dl->AddLine(ImVec2(center.x + dir.x * (r + 1.5f), center.y + dir.y * (r + 1.5f)),
                    ImVec2(center.x + dir.x * (r + 3.5f), center.y + dir.y * (r + 3.5f)),
                    sunCol, 1.2f);
    }
}

void drawMoonGlyph(ImDrawList* dl, ImVec2 center, float r) {
    // Crescent: pale disc with an offset backing-colored bite
    dl->AddCircleFilled(center, r, IM_COL32(200, 208, 228, 235));
    dl->AddCircleFilled(ImVec2(center.x + r * 0.5f, center.y - r * 0.3f), r * 0.85f,
                        IM_COL32(12, 14, 18, 255));
}

void drawTimeIndicator(ImDrawList* dl, const ImGuiViewport* viewport, float timeOfDay) {
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const float fontSize = ImGui::GetFontSize();

    // Clock text
    const float hours = timeOfDay * 24.0f;
    const int h = static_cast<int>(hours) % 24;
    const int m = static_cast<int>((hours - static_cast<float>(h)) * 60.0f) % 60;
    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", h, m);

    const float textScale = 0.9f;
    const ImVec2 textSize =
        ImGui::GetFont()->CalcTextSizeA(fontSize * textScale, FLT_MAX, 0.0f, clock);

    // Pill sits below the compass strip's marker, top-center
    const float glyphR = fontSize * 0.32f;
    const float padX = fontSize * 0.75f;
    const float gap = fontSize * 0.55f;
    const float pillH = fontSize * 1.5f;
    const float pillW = padX * 2.0f + glyphR * 2.0f + gap + textSize.x;
    const float cx = origin.x + size.x * 0.5f;
    const float compassTop = origin.y + std::max(size.y * 0.035f, fontSize);
    const float top = compassTop + fontSize * 1.8f + fontSize * 0.75f;
    const float midY = top + pillH * 0.5f;

    dl->AddRectFilled(ImVec2(cx - pillW * 0.5f, top), ImVec2(cx + pillW * 0.5f, top + pillH),
                      kBacking, pillH * 0.5f);

    const ImVec2 glyphCenter(cx - pillW * 0.5f + padX + glyphR, midY);
    const bool day = timeOfDay > 0.25f && timeOfDay < 0.75f;
    if (day) {
        drawSunGlyph(dl, glyphCenter, glyphR);
    } else {
        drawMoonGlyph(dl, glyphCenter, glyphR);
    }

    dl->AddText(ImGui::GetFont(), fontSize * textScale,
                ImVec2(glyphCenter.x + glyphR + gap, midY - textSize.y * 0.5f),
                IM_COL32(228, 231, 238, 225), clock);
}

}  // anonymous namespace

void GameHud::render(const Camera& camera, const ITimeSystem& time) {
    if (!visible_) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Background draw list: never takes focus or blocks clicks, and debug
    // windows always render above the HUD.
    ImDrawList* dl = ImGui::GetBackgroundDrawList(viewport);

    drawCompassStrip(dl, viewport, camera.getYaw());
    drawTimeIndicator(dl, viewport, time.getTimeOfDay());
}
