// "im-touch" tablet shell (docs/im-touch-ui-plan.md — Phase 0 skeleton with
// the Phase 1 theme/widgets/icons applied).
//
// Replaces the desktop menu bar / dockspace / status bar with fixed chrome
// when Settings → im-touch UI is on: a top app bar (project, undo/redo,
// Focus, overflow menu), a left tool rail (catalogue lands in Phase 3), a
// right side panel (Items/History content lands in Phase 2), and a floating
// FULL pill. The 3D viewport is pinned into the remaining center rect by
// renderViewport() via m_touchVp* (set here, read there, every frame).
//
// All shell windows are ##-named + NoSavedSettings so they never touch
// imgui.ini — toggling back to the desktop shell restores its saved layout
// untouched.

#include "app/Application.h"
#include "app/Window.h"
#include "core/History.h"
#include "ui/AboutDialog.h"
#include "ui/Toolbar.h"       // ToolAction for the starter rail entries
#include "ui/TouchIcons.h"
#include "ui/TouchTheme.h"
#include "ui/TouchWidgets.h"
#include "ui_scale.h"

#include <imgui.h>
#include <string>

namespace {

constexpr ImGuiWindowFlags kShellWin =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;

} // namespace

namespace materializr {

void Application::renderTouchShell() {
    touchui::Scope style; // TouchTheme push/pop around the whole shell

    const float s = uiScale();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 wp = vp->WorkPos;
    const ImVec2 ws = vp->WorkSize;

    const float topH   = 60.0f * s;
    const float railW  = m_leftPanelHidden  ? 0.0f : 92.0f * s;
    const float rightW = m_rightPanelHidden ? 0.0f : 320.0f * s;

    // ── Top app bar ─────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos(wp);
    ImGui::SetNextWindowSize(ImVec2(ws.x, topH));
    if (ImGui::Begin("##TouchTopBar", nullptr, kShellWin)) {
        const float pad = 14.0f * s;
        const float bh  = 44.0f * s;
        const float cy  = (topH - bh) * 0.5f; // vertical center for controls

        // Logo chip + name + / project (basename, "New project" unsaved).
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 win = ImGui::GetWindowPos();
            const float chip = 30.0f * s;
            const ImVec2 c0(win.x + pad, win.y + (topH - chip) * 0.5f);
            dl->AddRectFilled(c0, ImVec2(c0.x + chip, c0.y + chip),
                              ImGui::GetColorU32(touchui::accentFill()),
                              9.0f * s);
            ImGui::SetCursorPos(ImVec2(pad + chip + 10.0f * s,
                                       (topH - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::TextColored(touchui::textPrimary(), "Materializr");
            std::string pn = "New project";
            if (!m_currentProjectPath.empty()) {
                pn = m_currentProjectPath;
                auto slash = pn.find_last_of("/\\");
                if (slash != std::string::npos) pn = pn.substr(slash + 1);
            }
            ImGui::SameLine();
            ImGui::TextColored(touchui::textDim(), "/ %s", pn.c_str());
        }

        // Right-aligned controls: Undo, Redo, Focus, ⋯.
        const float sp = 8.0f * s;
        const float focusW = bh + ImGui::CalcTextSize("Focus").x + 27.0f * s;
        float x = ws.x - pad - (bh * 3 + focusW + sp * 3);
        ImGui::SetCursorPos(ImVec2(x, cy));

        const bool histLocked = anyInteractivePreviewActive();
        ImGui::BeginDisabled(histLocked || !m_history->canUndo());
        if (touchui::iconButton("undo", MZ_ICON_UNDO, bh)) undoWithCascade();
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, sp);
        ImGui::SetCursorPosY(cy);
        ImGui::BeginDisabled(histLocked || !m_history->canRedo());
        if (touchui::iconButton("redo", MZ_ICON_REDO, bh)) redoWithCascade();
        ImGui::EndDisabled();

        // Focus: viewport + rail only (right panel hidden).
        ImGui::SameLine(0.0f, sp);
        ImGui::SetCursorPosY(cy);
        if (touchui::pillButton("focus", MZ_ICON_FOCUS, "Focus",
                                m_rightPanelHidden)) {
            m_rightPanelHidden = !m_rightPanelHidden;
            saveAppSettings();
        }

        // ⋯ overflow: the desktop menus' essentials until Phase 2 flattens
        // them fully (shared item lists).
        ImGui::SameLine(0.0f, sp);
        ImGui::SetCursorPosY(cy);
        if (touchui::iconButton("overflow", MZ_ICON_MORE, bh))
            ImGui::OpenPopup("##TouchOverflow");
        if (ImGui::BeginPopup("##TouchOverflow")) {
            if (ImGui::MenuItem(MZ_ICON_OPEN "  Open Project..."))    loadProject();
            if (ImGui::MenuItem(MZ_ICON_SAVE "  Save Project"))       saveProjectQuick();
            if (ImGui::MenuItem(MZ_ICON_SAVE_AS "  Save Project As...")) saveProject();
            ImGui::Separator();
            if (ImGui::MenuItem(MZ_ICON_SETTINGS "  Settings..."))    m_showSettings = true;
            if (ImGui::MenuItem(MZ_ICON_ABOUT "  About Materializr"))
                m_aboutDialog->setVisible(true);
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    // ── Left tool rail — starter entries; the full selection-aware catalogue
    //    replaces this in Phase 3. ─────────────────────────────────────────
    if (railW > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(wp.x, wp.y + topH));
        ImGui::SetNextWindowSize(ImVec2(railW, ws.y - topH));
        if (ImGui::Begin("##TouchRail", nullptr, kShellWin)) {
            ImGui::SetCursorPosX(10.0f * s);
            touchui::sectionHeader("Tools");
            if (touchui::railButton("sketch", MZ_ICON_SKETCH, "Sketch",
                                    m_inSketchMode))
                handleToolAction(static_cast<int>(ToolAction::StartSketch));
            if (touchui::railButton("measure", MZ_ICON_MEASURE, "Measure", false))
                handleToolAction(static_cast<int>(ToolAction::Measure));
        }
        ImGui::End();
    }

    // ── Right side panel (Phase 2: Items | History content) ─────────────────
    if (rightW > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - rightW, wp.y + topH));
        ImGui::SetNextWindowSize(ImVec2(rightW, ws.y - topH));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, touchui::panelBg());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(14.0f * s, 12.0f * s));
        if (ImGui::Begin("##TouchRight", nullptr, kShellWin)) {
            static const char* kTabs[] = { "Items", "History" };
            m_touchRightTab = touchui::segmented("rightTabs", kTabs, 2,
                                                 m_touchRightTab);
            touchui::sectionHeader(m_touchRightTab == 0 ? "Bodies" : "Steps");
            ImGui::TextColored(touchui::textDim(), "(content lands in Phase 2)");
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // ── FULL pill — floats bottom-left so it stays reachable when the rail is
    //    hidden. Toggles chrome-less (viewport-only) mode and back. ──────────
    {
        const float m = 14.0f * s;
        ImGui::SetNextWindowPos(ImVec2(wp.x + railW + m, wp.y + ws.y - m),
                                ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        if (ImGui::Begin("##TouchFull", nullptr,
                         kShellWin | ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool full = m_leftPanelHidden && m_rightPanelHidden;
            if (touchui::pillButton("full", full ? MZ_ICON_FULL_EXIT : MZ_ICON_FULL,
                                    full ? "Exit" : "Full")) {
                const bool newHidden = !full;
                m_leftPanelHidden  = newHidden;
                m_rightPanelHidden = newHidden;
                saveAppSettings();
            }
        }
        ImGui::End();
    }

    // Center rect for renderViewport()'s pin.
    m_touchVpX = wp.x + railW;
    m_touchVpY = wp.y + topH;
    m_touchVpW = ws.x - railW - rightW;
    m_touchVpH = ws.y - topH;
}

} // namespace materializr
