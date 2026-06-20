#pragma once
#include <imgui.h>

namespace materializr {

// A blue accent for headings / section titles that stays READABLE on the active
// theme: the long-standing light blue on a dark background, but a deep blue on a
// light one (where the light blue was nearly invisible). Derived from the live
// window-background luminance, so it tracks the current ImGui theme with no
// global state — just call it wherever a heading used the hard-coded light blue.
inline ImVec4 accentText() {
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float lum = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
    return (lum < 0.5f) ? ImVec4(0.60f, 0.80f, 1.00f, 1.0f)   // dark bg  → light blue
                        : ImVec4(0.10f, 0.34f, 0.70f, 1.0f);  // light bg → deep blue
}

// A muted grey for secondary / de-emphasised text (date headers, dimmed steps,
// "nothing here" notes) that stays READABLE on either theme — a light grey on a
// dark background, a dark grey on a light one. The hard-coded ~0.5 greys read
// fine on dark but washed out on light.
inline ImVec4 dimText() {
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float lum = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
    return (lum < 0.5f) ? ImVec4(0.62f, 0.62f, 0.66f, 1.0f)   // dark bg  → light grey
                        : ImVec4(0.38f, 0.38f, 0.42f, 1.0f);  // light bg → dark grey
}

} // namespace materializr
