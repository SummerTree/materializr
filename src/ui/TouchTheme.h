#pragma once
// im-touch shell style pack (docs/im-touch-ui-plan.md, Phase 1).
//
// RAII scope that restyles ImGui for the tablet shell — bigger touch targets,
// rounded chrome, the mockup's near-black palette — pushed around the shell's
// windows each frame and popped before anything desktop-styled renders.
// Header-only; geometry scales with uiScale().
//
// Dark-first (the mockup); a light variant derives from ThemeManager later
// (Phase 5) — the constants live here in one place for that pass.

#include "../ui_scale.h"
#include <imgui.h>

namespace materializr {
namespace touchui {

// Palette (mockup). Exposed for the widget kit's custom draws.
inline ImVec4 chromeBg()     { return ImVec4(0.043f, 0.051f, 0.067f, 1.0f); } // #0B0D11
inline ImVec4 panelBg()      { return ImVec4(0.063f, 0.075f, 0.094f, 1.0f); } // #101318
inline ImVec4 rowBg()        { return ImVec4(0.102f, 0.118f, 0.145f, 1.0f); } // #1A1E25
inline ImVec4 hairline()     { return ImVec4(0.133f, 0.145f, 0.169f, 1.0f); } // #22252B
inline ImVec4 accentFill()   { return ImVec4(0.561f, 0.706f, 0.949f, 1.0f); } // #8FB4F2
inline ImVec4 accentDeep()   { return ImVec4(0.239f, 0.435f, 0.851f, 1.0f); } // #3D6FD9
inline ImVec4 textPrimary()  { return ImVec4(0.933f, 0.941f, 0.953f, 1.0f); } // #EEF0F3
inline ImVec4 textDim()      { return ImVec4(0.541f, 0.561f, 0.596f, 1.0f); } // #8A8F98
inline ImVec4 onAccent()     { return ImVec4(0.051f, 0.075f, 0.125f, 1.0f); } // text on accent

// Push the shell style. Pair with pop(); prefer the Scope below.
inline void push() {
    const float s = uiScale();
    ImGuiStyle& st = ImGui::GetStyle();
    (void)st;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,   12.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   10.0f * s);
    // Dialogs & floating windows share the shell look — rounded and padded.
    // (The theme is pushed for the WHOLE frame while im-touch is on, so every
    // dialog inherits it; the shell's edge-flush bars opt back out with a
    // local WindowRounding=0 push.)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  12.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(16.0f * s, 13.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2(14.0f * s, 9.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(10.0f * s, 10.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,   14.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,     24.0f * s);

    ImGui::PushStyleColor(ImGuiCol_TitleBg,          chromeBg());
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,    panelBg());
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, chromeBg());
    ImGui::PushStyleColor(ImGuiCol_WindowBg,       chromeBg());
    ImGui::PushStyleColor(ImGuiCol_PopupBg,        panelBg());
    ImGui::PushStyleColor(ImGuiCol_Border,         hairline());
    ImGui::PushStyleColor(ImGuiCol_Separator,      hairline());
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        rowBg());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.16f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.17f, 0.20f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,         rowBg());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.16f, 0.19f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.20f, 0.24f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,           textPrimary());
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,   textDim());
    ImGui::PushStyleColor(ImGuiCol_CheckMark,      accentFill());
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.14f, 0.16f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.17f, 0.20f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,         rowBg());
}

inline void pop() {
    ImGui::PopStyleColor(19);
    ImGui::PopStyleVar(9);
}

struct Scope {
    Scope()  { push(); }
    ~Scope() { pop(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

} // namespace touchui
} // namespace materializr
