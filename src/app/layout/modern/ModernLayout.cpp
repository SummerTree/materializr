// Modern layout (UiLayout::Modern) — the tablet shell (docs/im-touch-ui-plan.md
// Phase 0 skeleton with the Phase 1 theme/widgets/icons applied).
//
// Replaces the desktop menu bar / dockspace / status bar with fixed chrome:
// a top app bar (project, undo/redo, Focus, overflow menu), a left tool rail
// (the shared selection-context catalogue), and a right side panel hosting
// the same Items/History/Properties content renderers as classic's docks.
// The 3D viewport is pinned into the remaining center rect by
// renderViewport() via m_touchVp* (set here, read there, every frame).
//
// Everything fundamental (menus, tool catalogue, panel content) is shared
// code — see layout/LayoutCommon.h for the keep-in-lockstep contract.

#include <cstring>
#include "app/Application.h"
#include "app/layout/LayoutCommon.h"
#include "core/SelectionManager.h"
#include "modeling/SketchTool.h"   // SketchToolMode for the select-mode gate
#include "core/History.h"
#include "plugin/PluginContext.h"
#include "ui/HistoryPanel.h"
#include "ui/ItemsPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/Toolbar.h"       // ToolAction for the starter rail entries
#include "ui/TouchIcons.h"
#include "ui/TouchTheme.h"
#include "ui/TouchWidgets.h"
#include "touch_mode.h"
#include "ui_scale.h"

#include <imgui.h>
#include <string>

namespace materializr {

using layoutui::kShellWindowFlags;

void Application::renderModernLayout() {
    touchui::Scope style; // TouchTheme push/pop around the whole shell

    const float s = uiScale();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 wp = vp->WorkPos;
    const ImVec2 ws = vp->WorkSize;

    // Hover tooltip on the previous item — honours the same "toolbar
    // tooltips" setting the classic toolbar uses. (Hover exists on desktop
    // modern and on a tablet with a mouse; bare-finger use never sees it.)
    auto tip = [&](const char* text) {
        if (!m_showToolbarTooltips || !text) return;
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    const float topH   = 60.0f * s;
    const float railW  = m_leftPanelHidden  ? 0.0f : m_touchRailW * s; // user-resizable
    const float rightW = m_rightPanelHidden ? 0.0f : m_touchRightW * s; // user-resizable

    // ── Top app bar ─────────────────────────────────────────────────────────
    // The fixed bars are edge-flush strips — opt out of the theme's global
    // WindowRounding (their corners would notch against the viewport). The
    // pop right after Begin keeps rounding intact for anything opened inside
    // (modals, popups pick up style at their own Begin).
    auto beginFlushBar = [](const char* name, ImGuiWindowFlags flags) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        const bool open = ImGui::Begin(name, nullptr, flags);
        ImGui::PopStyleVar();
        return open;
    };

    ImGui::SetNextWindowPos(wp);
    ImGui::SetNextWindowSize(ImVec2(ws.x, topH));
    if (beginFlushBar("##TouchTopBar", kShellWindowFlags)) {
        const float pad = 14.0f * s;
        const float bh  = 44.0f * s;
        const float cy  = (topH - bh) * 0.5f; // vertical center for controls

        // ⋯ menu (top-left, nav-drawer style), then logo chip + name + /project.
        {
            const float bh0 = 44.0f * s;
            const float menuW = bh0 + 12.0f * s;    // ⋯ button + gap
            ImGui::SetCursorPos(ImVec2(pad, (topH - bh0) * 0.5f));
            if (touchui::iconButton("overflow", MZ_ICON_MORE, bh0))
                ImGui::OpenPopup("##TouchOverflow");
            tip("Menu: file, edit, view, help and settings");
            renderTouchOverflowPopup();

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 win = ImGui::GetWindowPos();
            const float chip = 30.0f * s;
            const float lx = pad + menuW;
            const ImVec2 c0(win.x + lx, win.y + (topH - chip) * 0.5f);
            dl->AddImageRounded(layoutui::logoTexture(), c0,
                                ImVec2(c0.x + chip, c0.y + chip),
                                ImVec2(0, 0), ImVec2(1, 1),
                                IM_COL32_WHITE, 7.0f * s);
            ImGui::SetCursorPos(ImVec2(lx + chip + 10.0f * s,
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

        // Right-aligned controls: [Finish, Discard,] Undo, Redo, [Keyboard,]
        // Focus, ⋯. Finish/Discard appear in sketch mode — the two actions
        // that must never be hunted for.
        const float sp = 8.0f * s;
        const bool showKb = materializr::touchMode();
        // Square icon buttons in the right cluster: undo, redo, [keyboard].
        // (The ⋯ overflow moved to the top-left.)
        const int nSquare = showKb ? 3 : 2;
        // Multi-Select toggle (the touch Ctrl stand-in): shown for 3D selection
        // and in sketch Select/move mode, hidden in the sketch draw tools where
        // adding to a selection is meaningless. Its old home was the bottom-left
        // viewport bar, where it overlapped the FULL pill.
        const bool showMulti = !m_inSketchMode ||
            (m_sketchTool && m_sketchTool->getMode() == SketchToolMode::Select);
        // Context-clear labels so nobody discards a whole sketch by reflex: while
        // a draw tool is running the buttons act on its SHAPE (Finish / Cancel);
        // with no tool running (e.g. Select/move) they act on the SKETCH, and say
        // so — "Finish Sketch" / "Discard Sketch".
        const bool toolRunning = m_inSketchMode && m_sketchTool &&
                                 m_sketchTool->isPlacing();
        const char* finishLbl = toolRunning ? "Finish" : "Finish Sketch";
        const char* exitLbl   = toolRunning ? "Cancel" : "Discard Sketch";
        // Inference-level toggle (sketch mode only): a compact two-row button
        // just left of Finish, click-cycles Max->Full->Reduced->Off like the
        // classic toolbar's guides button.
        const char* infLbl = "Full";
        if (m_inSketchMode && m_sketchTool) {
            switch (m_sketchTool->getInferenceLevel()) {
                case SketchTool::InferenceLevel::Full:    infLbl = "Full"; break;
                case SketchTool::InferenceLevel::Reduced: infLbl = "Reduced"; break;
                case SketchTool::InferenceLevel::Off:     infLbl = "Off"; break;
                case SketchTool::InferenceLevel::Max:     infLbl = "Max"; break;
            }
        }
        // Right-align the cluster with EXACT widths (touchui::pillButtonWidth
        // shares pillButton's sizing) — the previous estimate overshot per
        // pill, leaving an awkward gap against the right edge.
        // Focus is a 3-position cycle: full UI -> side panel hidden ->
        // viewport only (which retired the old bottom-left FULL pill). The
        // label/icon reflect the CURRENT state; width uses the current label.
        const int focusState = m_leftPanelHidden ? 2 : (m_rightPanelHidden ? 1 : 0);
        const char* focusIcon = focusState == 2 ? MZ_ICON_FULL_EXIT : MZ_ICON_FOCUS;
        const char* focusLbl  = focusState == 2 ? "Full" : "Focus";
        float total = bh * nSquare + sp * nSquare +
                      touchui::pillButtonWidth(focusIcon, focusLbl);
        if (m_inSketchMode)
            total += touchui::twoRowButtonWidth("Inference level", infLbl) + sp +
                     touchui::pillButtonWidth(MZ_ICON_FINISH, finishLbl) +
                     touchui::pillButtonWidth(MZ_ICON_DISCARD, exitLbl) + sp * 2;
        if (showMulti)
            total += touchui::pillButtonWidth(MZ_ICON_SELECT, "Multi") + sp;
        float x = ws.x - pad - total;
        ImGui::SetCursorPos(ImVec2(x, cy));

        if (showMulti) {
            if (touchui::pillButton("multi", MZ_ICON_SELECT, "Multi",
                                    m_multiSelectToggle))
                m_multiSelectToggle = !m_multiSelectToggle;
            tip("Multi-select: add taps to the current selection\n"
                "(the touch equivalent of holding Ctrl)");
            ImGui::SameLine(0.0f, sp);
            ImGui::SetCursorPosY(cy);
        }

        if (m_inSketchMode) {
            // Inference level — just left of Finish; click cycles the level.
            if (touchui::twoRowButton("inflvl", "Inference level", infLbl)) {
                handleToolAction(static_cast<int>(ToolAction::SketchCycleInference));
            }
            tip("Sketch inference level (snapping / guides)\n"
                "Click to cycle: Full \xE2\x86\x92 Reduced \xE2\x86\x92 Off \xE2\x86\x92 Max");
            ImGui::SameLine(0.0f, sp);
            ImGui::SetCursorPosY(cy);
            if (touchui::pillButton("finish", MZ_ICON_FINISH, finishLbl, true)) {
                if (toolRunning)
                    recordSketchMutation([&]{ m_sketchTool->onConfirm(); });
                else
                    handleToolAction(static_cast<int>(ToolAction::FinishSketch));
            }
            tip(toolRunning
                    ? "Finish the current shape, keeping the points placed"
                    : "Leave sketch mode, keeping the sketch");
            ImGui::SameLine(0.0f, sp);
            ImGui::SetCursorPosY(cy);
            if (touchui::pillButton("exit", MZ_ICON_DISCARD, exitLbl)) {
                if (toolRunning)
                    m_sketchTool->onCancel();       // discard the in-progress shape
                else
                    // Whole-sketch discard is destructive — confirm first so a
                    // misclick can't throw the sketch away.
                    ImGui::OpenPopup("Discard sketch?");
            }
            tip(toolRunning
                    ? "Cancel the in-progress shape"
                    : "Throw the sketch away and leave (asks to confirm)");
            if (ImGui::BeginPopupModal("Discard sketch?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted(
                    "Leave the sketch and throw away its changes?");
                ImGui::Spacing();
                const float bw = 150.0f * s;
                if (ImGui::Button("Discard Sketch", ImVec2(bw, 44.0f * s))) {
                    ImGui::CloseCurrentPopup();
                    handleToolAction(static_cast<int>(ToolAction::ExitSketchDiscard));
                }
                ImGui::SameLine();
                if (ImGui::Button("Keep Editing", ImVec2(bw, 44.0f * s)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.0f, sp);
            ImGui::SetCursorPosY(cy);
        }

        const bool histLocked = anyInteractivePreviewActive();
        ImGui::BeginDisabled(histLocked || !touchCanUndo());
        if (touchui::iconButton("undo", MZ_ICON_UNDO, bh)) touchUndo();
        ImGui::EndDisabled();
        tip("Undo (in a sketch: backs out the in-progress shape first)");
        ImGui::SameLine(0.0f, sp);
        ImGui::SetCursorPosY(cy);
        ImGui::BeginDisabled(histLocked || !m_history->canRedo());
        if (touchui::iconButton("redo", MZ_ICON_REDO, bh)) redoWithCascade();
        ImGui::EndDisabled();
        tip("Redo");

        // Soft-keyboard toggle (the desktop menu bar's right-aligned item;
        // there's no menu bar here). Touch mode only, same flag.
        if (showKb) {
            ImGui::SameLine(0.0f, sp);
            ImGui::SetCursorPosY(cy);
            if (touchui::iconButton("kb", MZ_ICON_KEYBOARD, bh))
                m_softKeyboardForced = !m_softKeyboardForced;
            tip("Toggle the on-screen keyboard");
        }

        // Focus cycle: 0 = everything, 1 = side panel hidden, 2 = viewport
        // only (rail hidden too). One button, three positions.
        ImGui::SameLine(0.0f, sp);
        ImGui::SetCursorPosY(cy);
        if (touchui::pillButton("focus", focusIcon, focusLbl,
                                focusState != 0)) {
            const int next = (focusState + 1) % 3;
            m_rightPanelHidden = next >= 1;
            m_leftPanelHidden  = next == 2;
            saveAppSettings();
        }
        tip(focusState == 0 ? "Focus: hide the side panel (tap again for viewport only)"
            : focusState == 1 ? "Focus: hide the tool rail too (viewport only)"
                              : "Bring the panels back");
        // (The ⋯ overflow menu now lives at the top-left of this bar.)
    }
    ImGui::End();

    // ── Left tool rail — the selection-context tool catalogue. ──────────────
    if (railW > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(wp.x, wp.y + topH));
        ImGui::SetNextWindowSize(ImVec2(railW, ws.y - topH));
        if (beginFlushBar("##TouchRail",
                          kShellWindowFlags & ~ImGuiWindowFlags_NoScrollbar)) {
            ImGui::SetCursorPosX(10.0f * s);
            touchui::sectionHeader("Tools");

            // TWO-COLUMN GRID when the rail is dragged wide: dragging wider
            // used to just stretch the same buttons into slabs; past the
            // threshold the catalogue reflows into two half-width cells per
            // row (left->right, top->bottom), halving the scroll length. Each
            // cell keeps the narrow rail's icon-over-label proportions.
            const float railInnerW = ImGui::GetContentRegionAvail().x;
            const int   railCols   = (m_touchRailW >= 118.0f) ? 2 : 1;
            const float railCellW  = railCols == 2
                ? (railInnerW - ImGui::GetStyle().ItemSpacing.x) * 0.5f
                : 0.0f;                        // 0 = railButton full width
            int railCellIdx = 0;
            // Evaluated as the width argument of each railButton: places the
            // cell (SameLine for the right column) and returns its width.
            auto cell = [&]() -> float {
                if (railCols == 2 && (railCellIdx & 1)) ImGui::SameLine();
                ++railCellIdx;
                return railCellW;
            };

            // Grouped popups for the create tools the contextual rail omits —
            // one tap away (not buried in the ⋯ menu). On a touch screen they
            // get roomier rows (bigger padding + row gap) for finger targets.
            const bool nothingSel = !m_inSketchMode &&
                (!m_selection || !m_selection->hasSelection());
            const bool touchPad = materializr::touchMode();
            auto pushPopupPad = [&] {
                if (!touchPad) return;
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                    ImVec2(16.0f * s, 13.0f * s));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                    ImVec2(12.0f * s, 14.0f * s));
            };
            auto popPopupPad = [&] { if (touchPad) ImGui::PopStyleVar(2); };
            auto constructGroup = [&] {
                if (touchui::railButton("constructGroup", MZ_ICON_FOCUS,
                                        "Construct", false, cell()))
                    ImGui::OpenPopup("##railConstruct");
                tip("Create a construction plane or axis derived from the selection");
                pushPopupPad();
                if (ImGui::BeginPopup("##railConstruct")) {
                    renderConstructionMenuItems();
                    ImGui::EndPopup();
                }
                popPopupPad();
            };

            // With nothing selected the create tools lead (Sketch on… at the
            // very top) and railTools' Measure lands at the bottom; with a
            // selection the contextual tools lead and Construct follows.
            if (nothingSel) {
                if (touchui::railButton("sketchOnGroup", MZ_ICON_SKETCH,
                                        "Sketch on...", false, cell()))
                    ImGui::OpenPopup("##railSketchOn");
                tip("Start a sketch on a world plane (XY / XZ / YZ)");
                pushPopupPad();
                if (ImGui::BeginPopup("##railSketchOn")) {
                    if (ImGui::MenuItem("XY plane"))
                        handleToolAction(static_cast<int>(ToolAction::StartSketchXY));
                    if (ImGui::MenuItem("XZ plane"))
                        handleToolAction(static_cast<int>(ToolAction::StartSketchXZ));
                    if (ImGui::MenuItem("YZ plane"))
                        handleToolAction(static_cast<int>(ToolAction::StartSketchYZ));
                    ImGui::EndPopup();
                }
                popPopupPad();
                if (touchui::railButton("primGroup", MZ_ICON_PRIMITIVE,
                                        "Primitive", false, cell()))
                    ImGui::OpenPopup("##railPrimitive");
                tip("Add a primitive solid: box, cylinder, sphere, cone or torus");
                pushPopupPad();
                if (ImGui::BeginPopup("##railPrimitive")) {
                    if (m_pluginContext) {
                        if (ImGui::MenuItem("Box"))
                            m_pluginContext->requestInteractiveOp("PrimitiveBox");
                        if (ImGui::MenuItem("Cylinder"))
                            m_pluginContext->requestInteractiveOp("PrimitiveCylinder");
                        if (ImGui::MenuItem("Sphere"))
                            m_pluginContext->requestInteractiveOp("PrimitiveSphere");
                        if (ImGui::MenuItem("Cone"))
                            m_pluginContext->requestInteractiveOp("PrimitiveCone");
                        if (ImGui::MenuItem("Torus"))
                            m_pluginContext->requestInteractiveOp("PrimitiveTorus");
                    }
                    ImGui::EndPopup();
                }
                popPopupPad();
                constructGroup();
            }

            if (m_toolbar) {
                const auto rail = m_toolbar->railTools();
                // Fewer rail buttons = less scrolling (the point of this on a
                // 1080 desktop / a tablet). Two collapses, MODERN ONLY (classic
                // and im-touch keep their own layouts):
                //   Transform = Copy + Mirror (+ Duplicate)   -> copy icon
                //   Pattern   = Linear + Circular             -> circular icon
                // And two entries move OUT of the rail entirely: Measure (now
                // the View menu) and the inference level (now the top bar).
                auto groupOf = [](const Toolbar::RailTool& t) -> int {
                    if (t.pluginIndex >= 0) {
                        if (t.label && std::strcmp(t.label, "Linear") == 0)   return 2;
                        if (t.label && std::strcmp(t.label, "Circular") == 0) return 2;
                        if (t.label && std::strcmp(t.label, "Duplicate") == 0) return 1;
                        return 0;
                    }
                    switch (t.action) {
                        case ToolAction::SketchCopy:
                        case ToolAction::SketchMirror:
                        case ToolAction::Mirror:               return 1;
                        case ToolAction::SketchLinearPattern:
                        case ToolAction::SketchRadialPattern:  return 2;
                        default: return 0;
                    }
                };
                auto skip = [](const Toolbar::RailTool& t) {
                    return t.action == ToolAction::Measure ||           // -> View menu
                           t.action == ToolAction::SketchCycleInference; // -> top bar
                };
                auto count = [&](int g) {
                    int n = 0;
                    for (const auto& t : rail)
                        if (!skip(t) && groupOf(t) == g) ++n;
                    return n;
                };
                auto fire = [&](const Toolbar::RailTool& t) {
                    if (t.pluginIndex >= 0) m_toolbar->fireRailPlugin(t.pluginIndex);
                    else handleToolAction(static_cast<int>(t.action));
                };

                bool done[3] = { false, false, false };
                int railIdx = 0;
                for (const auto& tool : rail) {
                    if (skip(tool)) continue;
                    const int g = groupOf(tool);
                    // Only collapse when the group actually has 2+ members in
                    // this context; a lone Mirror stays a plain button.
                    if (g != 0 && count(g) >= 2) {
                        if (done[g]) continue;
                        done[g] = true;
                        const char* gIcon  = g == 1 ? MZ_ICON_COPY : MZ_ICON_PATTERN_CIRCULAR;
                        const char* gLabel = g == 1 ? "Transform" : "Pattern";
                        const char* gPopup = g == 1 ? "##railTransform" : "##railPattern";
                        ImGui::PushID(2000 + g);
                        if (touchui::railButton(gLabel, gIcon, gLabel, false, cell()))
                            ImGui::OpenPopup(gPopup);
                        tip(g == 1 ? "Copy or mirror the selection"
                                   : "Linear or circular pattern of the selection");
                        pushPopupPad();
                        if (ImGui::BeginPopup(gPopup)) {
                            for (const auto& m : rail) {
                                if (skip(m) || groupOf(m) != g) continue;
                                if (ImGui::MenuItem(m.label)) fire(m);
                            }
                            ImGui::EndPopup();
                        }
                        popPopupPad();
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::PushID(railIdx++); // labels can repeat across groups
                    const bool clicked = touchui::railButton(
                        tool.label, tool.icon, tool.label, tool.active,
                        cell());
                    tip(tool.tip);
                    if (tool.pluginIndex >= 0) {
                        if (clicked) m_toolbar->fireRailPlugin(tool.pluginIndex);
                    } else if (tool.action == ToolAction::Polygon)
                        renderRailPolygonSidesPopup(clicked);
                    else if (clicked)
                        handleToolAction(static_cast<int>(tool.action));
                    ImGui::PopID();
                }
            }

            // Construction stays reachable with a selection too (its options
            // derive from the selection) — after the contextual tools.
            if (!m_inSketchMode && !nothingSel)
                constructGroup();
        }
        ImGui::End();
    }

    // ── Right side panel (Items | History & Properties) ─────────────────────
    if (rightW > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - rightW, wp.y + topH));
        ImGui::SetNextWindowSize(ImVec2(rightW, ws.y - topH));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, touchui::panelBg());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(14.0f * s, 12.0f * s));
        if (beginFlushBar("##TouchRight", kShellWindowFlags)) {
            // Left-edge drag splitter: the panel is resizable. Screen-space
            // strip along the window's left edge; drag left = wider (the
            // panel is right-anchored). Width persists via m_touchRightW.
            {
                const ImVec2 winPos = ImGui::GetWindowPos();
                const ImVec2 keep = ImGui::GetCursorPos();
                ImGui::SetCursorScreenPos(winPos);
                ImGui::InvisibleButton("##rightResize",
                                       ImVec2(10.0f * s, ws.y - topH));
                const bool active = ImGui::IsItemActive();
                if (ImGui::IsItemHovered() || active)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                if (active) {
                    m_touchRightW -= ImGui::GetIO().MouseDelta.x / s;
                    if (m_touchRightW < 200.0f) m_touchRightW = 200.0f;
                    if (m_touchRightW > 520.0f) m_touchRightW = 520.0f;
                }
                if (ImGui::IsItemDeactivated()) saveAppSettings();
                // Grip hint: hairline normally, accent while grabbed/hovered.
                ImGui::GetWindowDrawList()->AddRectFilled(
                    winPos, ImVec2(winPos.x + 3.0f * s, winPos.y + ws.y - topH),
                    ImGui::GetColorU32(active ? touchui::accentDeep()
                                              : touchui::hairline()));
                ImGui::SetCursorPos(keep);
            }

            // Properties lives inside the History tab (below the steps) but
            // the tab just says "History" — that's where people expect it,
            // and the short label keeps the switcher clean.
            static const char* kTabs[] = { "Items", "History" };
            if (m_touchRightTab > 1) m_touchRightTab = 1; // migrate old 3-tab value
            const int tab = touchui::segmented("rightTabs", kTabs, 2,
                                               m_touchRightTab);
            if (tab != m_touchRightTab) {
                m_touchRightTab = tab;
                saveAppSettings();
            }
            // Scrolling body below the pinned switcher. The panels' content
            // renderers are the same code the desktop docks host — identical
            // behavior, different container.
            if (ImGui::BeginChild("##touchRightBody", ImVec2(0, 0), false)) {
                if (m_touchRightTab == 0) {
                    if (m_itemsPanel && m_itemsPanel->renderContent()) {
                        m_hoveredBodyId = -1;
                        m_meshesDirty = true;
                    }
                } else {
                    // History on top, Properties beneath — one tab hosts
                    // both. When a history STEP is selected the History panel
                    // shows the step's editor INLINE, so the bottom Properties
                    // section would just duplicate an empty panel beneath it
                    // ("two properties windows, the bottom one blank") — give
                    // History the full height instead and skip the section.
                    const bool stepEditing =
                        m_historyPanel && m_historyPanel->getEditingStep() >= 0;
                    // History gets the SMALLER share so IT scrolls (long step
                    // lists) while Properties keeps enough room to show a
                    // selection's fields without its own scrollbar (Steve).
                    const float histH = stepEditing
                        ? 0.0f
                        : ImGui::GetContentRegionAvail().y * 0.52f;
                    if (ImGui::BeginChild("##histHalf", ImVec2(0, histH), false)) {
                        if (m_historyPanel) {
                            // Undo/redo live in the shell's top bar; the panel
                            // shows its step counter beside the label instead.
                            m_historyPanel->setShowUndoRedo(false);
                            if (m_historyPanel->renderContent())
                                m_meshesDirty = true;
                        }
                    }
                    ImGui::EndChild();
                    if (!stepEditing) {
                        ImGui::Separator();
                        touchui::sectionHeader("Properties");
                        if (ImGui::BeginChild("##propsHalf", ImVec2(0, 0), false)) {
                            if (m_propertiesPanel && m_propertiesPanel->renderContent())
                                m_meshesDirty = true;
                        }
                        ImGui::EndChild();
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // (The old bottom-left FULL pill folded into the Focus cycle above.)
    // The panel/viewport edge tabs (pop-in/out chevrons) render in
    // renderModernEdgeTabs(), called AFTER renderViewport so their windows are
    // submitted after the (NoBringToFrontOnFocus) Viewport window and sit ON
    // TOP of it. Submitting them here — before the viewport — let the viewport
    // cover them until a focus reorder (e.g. toggling Settings) surfaced them,
    // which read as "no chevrons on first launch" on the tablet.

    // Center rect for renderViewport()'s pin.
    m_touchVpX = wp.x + railW;
    m_touchVpY = wp.y + topH;
    m_touchVpW = ws.x - railW - rightW;
    m_touchVpH = ws.y - topH;
}

// The panel/viewport pop-in/out edge tabs. Called AFTER renderViewport (see
// the dispatch in run()) so these windows are submitted last and sit above the
// Viewport window — otherwise the viewport covered them until a focus reorder.
// Geometry comes from m_touchVp* (the panel/viewport boundaries), set by
// renderModernLayout earlier this frame.
void Application::renderModernEdgeTabs() {
    const float s = uiScale();
    const float tabW = 16.0f * s, tabH = 72.0f * s;
    const float midY = m_touchVpY + m_touchVpH * 0.5f;
    // side: -1 = tab bulges rightward (left rail edge), +1 = leftward.
    auto edgeTab = [&](const char* id, float edgeX, int side,
                       bool* hiddenVar, float* widthVar, float minW,
                       float maxW, bool* dragged) {
        // Anchor by the window's LEFT edge (pivot 0) so the flat side lands
        // exactly on the panel boundary. Push WindowMinSize 0 (tabW is below
        // the default 32) and derive geometry from our own winLeft, never
        // GetWindowPos — so cx = winLeft(+tabW) is exactly edgeX.
        const float winLeft = (side < 0) ? edgeX : edgeX - tabW;
        ImGui::SetNextWindowPos(ImVec2(winLeft, midY), ImGuiCond_Always,
                                ImVec2(0.0f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(tabW, tabH));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        // NOT kShellWindowFlags: those carry NoBringToFrontOnFocus, which (with
        // the Viewport window, also NoBringToFrontOnFocus, sharing this rect)
        // left the tab BURIED under the viewport in ImGui's persistent z-order
        // until an unrelated focus event surfaced it — "no chevrons on cold
        // start". Without the flag the tiny input window rises to the front on
        // creation and stays there (the viewport can't climb above it). The
        // VISUAL is drawn into the foreground draw list below, so it's on top
        // regardless of any window ordering.
        const ImGuiWindowFlags tabFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin(id, nullptr, tabFlags)) {
            const ImVec2 p(winLeft, midY - tabH * 0.5f); // our exact rect
            ImGui::SetCursorScreenPos(p);
            ImGui::InvisibleButton("##grip", ImVec2(tabW, tabH));
            const bool hov = ImGui::IsItemHovered();
            const bool act = ImGui::IsItemActive();
            if (hov || act)
                ImGui::SetMouseCursor(*hiddenVar ? ImGuiMouseCursor_Hand
                                                 : ImGuiMouseCursor_ResizeEW);
            // Drag = resize (visible panel only); a few px of slop separates a
            // tap from a drag.
            if (act && !*hiddenVar && ImGui::IsMouseDragging(0, 4.0f)) {
                *dragged = true;
                *widthVar += (side < 0 ? 1.0f : -1.0f) *
                             ImGui::GetIO().MouseDelta.x / s;
                if (*widthVar < minW) *widthVar = minW;
                if (*widthVar > maxW) *widthVar = maxW;
            }
            if (ImGui::IsItemDeactivated()) {
                if (*dragged) {
                    saveAppSettings();          // resize ended
                } else {
                    *hiddenVar = !*hiddenVar;   // tap: pop out / back in
                    saveAppSettings();
                }
                *dragged = false;
            }

            // Foreground draw list: always renders on top of ALL windows
            // (incl. the viewport), so the chevron can never be hidden behind
            // a window even if the z-order is off.
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const float cx = side < 0 ? p.x : p.x + tabW;
            const ImVec2 c(cx, p.y + tabH * 0.5f);
            const float pi = 3.1415926f;
            dl->PathArcTo(c, tabW - 1.0f * s,
                          side < 0 ? -pi * 0.5f : pi * 0.5f,
                          side < 0 ? pi * 0.5f : pi * 1.5f, 24);
            dl->PathFillConvex(ImGui::GetColorU32(
                (hov || act) ? touchui::rowBg() : touchui::panelBg()));
            // Chevron points where the panel edge moves on tap.
            const float bulge = side < 0 ? 1.0f : -1.0f;
            const float dir = *hiddenVar ? bulge : -bulge;
            const float chw = 4.0f * s, chh = 5.0f * s;
            const ImVec2 tipPt(c.x + bulge * 6.0f * s + dir * chw * 0.5f, c.y);
            dl->AddLine(ImVec2(tipPt.x - dir * chw, c.y - chh), tipPt,
                        ImGui::GetColorU32(touchui::textDim()), 2.0f * s);
            dl->AddLine(ImVec2(tipPt.x - dir * chw, c.y + chh), tipPt,
                        ImGui::GetColorU32(touchui::textDim()), 2.0f * s);
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    };
    edgeTab("##railTab", m_touchVpX, -1, &m_leftPanelHidden,
            &m_touchRailW, 64.0f, 208.0f, &m_railTabDragged);
    edgeTab("##rightTab", m_touchVpX + m_touchVpW, +1, &m_rightPanelHidden,
            &m_touchRightW, 200.0f, 520.0f, &m_rightTabDragged);
}

} // namespace materializr
