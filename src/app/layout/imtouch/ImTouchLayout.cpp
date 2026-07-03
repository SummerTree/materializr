// im-touch layout (UiLayout::ImTouch — the name is an homage to ImGui):
// near-zero chrome. The viewport fills the whole work rect; everything else
// floats over it — project/selection chip (top-left), undo + keyboard + menu
// (top-right), the contextual tool catalogue on the left edge, the
// Fusion-style history timeline (bottom-center), a "+" create FAB
// (bottom-right), and an fps readout.
//
// Everything fundamental (menus, tool catalogue, history editing) is shared
// code — see layout/LayoutCommon.h for the keep-in-lockstep contract.

#include "app/Application.h"
#include "app/layout/LayoutCommon.h"
#include "core/Document.h"
#include "core/History.h"
#include "core/Operation.h"
#include "core/SelectionManager.h"
#include "modeling/SketchEditOp.h"      // timeline: Apply cascade targets
#include "modeling/SketchTransformOp.h"
#include "modeling/SketchTool.h"   // SketchToolMode for the select-mode gate
#include "plugin/PluginContext.h"
#include "ui/HistoryPanel.h"
#include "ui/Toolbar.h"       // ToolAction for the tool-bar entries
#include "ui/TouchIcons.h"
#include "ui/TouchTheme.h"
#include "ui/TouchWidgets.h"
#include "touch_mode.h"
#include "ui_scale.h"

#include <cfloat> // FLT_MAX (tool bar height constraint)
#include <imgui.h>
#include <set>
#include <string>

namespace materializr {

namespace {

// Icon for a history step's box in the timeline, by op typeId. Reloaded
// steps (ReplayOp) report the ORIGINAL op's typeId, so they map the same.
const char* stepIcon(const std::string& t) {
    if (t == "extrude")                    return MZ_ICON_EXTRUDE;
    if (t == "pushpull" || t == "moveface" || t == "move_hole")
                                           return MZ_ICON_PUSHPULL;
    if (t == "revolve")                    return MZ_ICON_LATHE;
    if (t == "loft" || t == "sweep")       return MZ_ICON_SPLINE;
    if (t == "fillet")                     return MZ_ICON_FILLET;
    if (t == "chamfer")                    return MZ_ICON_CHAMFER;
    if (t == "shell")                      return MZ_ICON_SHELL;
    if (t == "boolean")                    return MZ_ICON_SUBTRACT;
    if (t == "split_body")                 return MZ_ICON_TRIM;
    if (t == "delete")                     return MZ_ICON_DELETE;
    if (t == "defeature")                  return MZ_ICON_REPAIR;
    if (t == "mirror")                     return MZ_ICON_MIRROR;
    if (t == "pattern")                    return MZ_ICON_PATTERN;
    if (t == "copy" || t == "duplicate_sketch")
                                           return MZ_ICON_COPY;
    if (t == "scale_face" || t == "resize_cylindrical" || t == "taper")
                                           return MZ_ICON_SCALE;
    if (t == "primitive")                  return MZ_ICON_PRIMITIVE;
    if (t == "thread")                     return MZ_ICON_ROTATE;
    if (t == "construction_plane" || t == "construction_axis")
                                           return MZ_ICON_AXES;
    if (t == "sketchedit" || t == "combine_sketches" || t == "project_sketch")
                                           return MZ_ICON_SKETCH;
    if (t == "transform" || t == "axis_transform" || t == "plane_transform" ||
        t == "sketchtransform" || t == "align")
                                           return MZ_ICON_MOVE;
    return MZ_ICON_EDIT;
}

} // namespace

void Application::renderImTouchLayout() {
    touchui::Scope style;

    const float s = uiScale();
    auto tip = [&](const char* text) {   // same policy as the modern layout
        if (!m_showToolbarTooltips || !text) return;
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 wp = vp->WorkPos;
    const ImVec2 ws = vp->WorkSize;
    const float m = 12.0f * s; // float margin from the work-rect edges

    // Viewport underneath everything.
    m_touchVpX = wp.x;
    m_touchVpY = wp.y;
    m_touchVpW = ws.x;
    m_touchVpH = ws.y;

    // These overlays float ON TOP of the full-screen viewport window, which is
    // NoBringToFrontOnFocus (pinned to the back). They must NOT share that flag:
    // if they do, z-order falls to ImGui's persistent creation order — which is
    // fine when the app LAUNCHES straight into im-touch (overlays created
    // early), but when the user TOGGLES to it at runtime the viewport was
    // created first and stays in front, burying every overlay (the "invisible
    // shell" bug). Dropping the flag makes them ordinary foreground windows
    // that come to front on appearance, so they render above the back-pinned
    // viewport every time.
    const ImGuiWindowFlags kFloat =
        (layoutui::kShellWindowFlags & ~ImGuiWindowFlags_NoBringToFrontOnFocus) |
        ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f * s);
    // No window borders on any of the floating overlays — the 1px frame reads
    // as a faint "ghost" rectangle around transparent windows (the +, the
    // chip, the buttons). Their rounded fill is the only chrome we want.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    // ── Project / selection chip (top-left) ─────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(wp.x + m, wp.y + m));
    ImGui::SetNextWindowBgAlpha(0.55f);
    if (ImGui::Begin("##LiteChip", nullptr, kFloat)) {
        // ⋯ menu at the far left (moved off the top-right cluster).
        if (touchui::iconButton("menu", MZ_ICON_MENU_BARS, 30.0f * s))
            ImGui::OpenPopup("##TouchOverflow");
        tip("Menu: file, edit, view, help and settings");
        renderTouchOverflowPopup();
        ImGui::SameLine(0.0f, 8.0f * s);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float chip = 18.0f * s;
        const ImVec2 c0 = ImGui::GetCursorScreenPos();
        dl->AddImageRounded(layoutui::logoTexture(), c0,
                            ImVec2(c0.x + chip, c0.y + chip),
                            ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, 4.0f * s);
        ImGui::Dummy(ImVec2(chip, chip));
        ImGui::SameLine();

        std::string pn = "New project";
        if (!m_currentProjectPath.empty()) {
            pn = m_currentProjectPath;
            auto slash = pn.find_last_of("/\\");
            if (slash != std::string::npos) pn = pn.substr(slash + 1);
        }
        // Selection summary: "· Face (2)" of the primary type, mirroring the
        // mockup's "mug.mzr · Face (1)".
        std::string sel;
        if (m_selection && m_selection->hasSelection()) {
            const SelectionType t = m_selection->primaryType();
            int n = 0;
            for (const auto& e : m_selection->getSelection())
                if (e.type == t) ++n;
            static const char* kNames[] = { "None", "Body", "Face", "Edge",
                                            "Vertex", "Sketch", "Region",
                                            "Plane", "Axis" };
            const int ti = static_cast<int>(t);
            if (ti > 0 && ti < 9) {
                sel = std::string("  ·  ") + kNames[ti] +
                      " (" + std::to_string(n) + ")";
            }
        }
        ImGui::TextColored(touchui::textPrimary(), "%s", pn.c_str());
        if (!sel.empty()) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(touchui::textDim(), "%s", sel.c_str());
        }
    }
    ImGui::End();

    // ── Undo / keyboard / menu (top-right) ──────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - m, wp.y + m),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    if (ImGui::Begin("##LiteTopRight", nullptr, kFloat)) {
        const float bh = 44.0f * s;
        // Multi-Select (moved off the bottom-left viewport bar): 3D selection and
        // sketch Select/move mode only.
        const bool showMulti = !m_inSketchMode ||
            (m_sketchTool && m_sketchTool->getMode() == SketchToolMode::Select);
        if (showMulti) {
            if (touchui::pillButton("multi", MZ_ICON_SELECT, "Multi",
                                    m_multiSelectToggle))
                m_multiSelectToggle = !m_multiSelectToggle;
            tip("Multi-select: add taps to the current selection\n"
                "(the touch equivalent of holding Ctrl)");
            ImGui::SameLine(0.0f, 8.0f * s);
        }
        const bool histLocked = anyInteractivePreviewActive();
        ImGui::BeginDisabled(histLocked || !touchCanUndo());
        if (touchui::iconButton("undo", MZ_ICON_UNDO, bh)) touchUndo();
        ImGui::EndDisabled();
        tip("Undo (in a sketch: backs out the in-progress shape first)");
        if (materializr::touchMode()) {
            ImGui::SameLine(0.0f, 8.0f * s);
            if (touchui::iconButton("kb", MZ_ICON_KEYBOARD, bh))
                m_softKeyboardForced = !m_softKeyboardForced;
            tip("Toggle the on-screen keyboard");
        }
        // (Items toggle moved to the persistent right-edge button below;
        // History has its own bottom toggle; the ⋯ menu lives on the top-left
        // chip.)
    }
    ImGui::End();

    // ── Persistent right-edge Items button: tap to open the model tree (which
    //    appears just left of it), tap again — or the tree's header chevron —
    //    to collapse it back. Accent-filled while the tree is open. Vertically
    //    centred so it clears the top-right cluster, the ViewCube and the FAB.
    //    (History is NOT here — it lives at the bottom next to the timeline; see
    //    below — so its reopen button sits where its minimize chevron does.)
    const float railBtnW = 60.0f * s;
    {
        ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - m, wp.y + ws.y * 0.5f),
                                ImGuiCond_Always, ImVec2(1.0f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.0f);   // the button draws its own solid fill
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("##LiteItemsButton", nullptr, kFloat)) {
            if (touchui::railButton("railItems", MZ_ICON_ITEMS, "Items",
                                    m_imTouchTree, railBtnW, /*solid=*/true)) {
                m_imTouchTree = !m_imTouchTree;
                saveAppSettings();
            }
            tip(m_imTouchTree ? "Hide the model tree"
                              : "Show the model tree (bodies, sketches, construction)");
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ── Transparent model tree (right edge) — the structure the modern
    //    layout's Items panel shows, display-focused: visibility checkbox +
    //    name + tap-to-select. Deep actions (rename, folders, export) live in
    //    the other layouts; this stays an im-touch-only overlay.
    if (m_imTouchTree && m_document) {
        // Anchored just LEFT of the right-edge rail so its Items button stays
        // visible and tappable while the tree is open.
        ImGui::SetNextWindowPos(
            ImVec2(wp.x + ws.x - m - railBtnW - 8.0f * s, wp.y + ws.y * 0.5f),
            ImGuiCond_Always, ImVec2(1.0f, 0.5f));
        const float treeW = 250.0f * s;
        ImGui::SetNextWindowSizeConstraints(ImVec2(treeW, 0),
                                            ImVec2(treeW, ws.y - 2.0f * m));
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("##LiteTree", nullptr,
                         kFloat & ~ImGuiWindowFlags_NoScrollbar)) {
            // "Items" title. No in-panel minimize control — the right-edge
            // Items button (which opened it) toggles it closed again.
            ImGui::TextColored(touchui::textPrimary(), "Items");
            ImGui::Separator();
            // Selected ids per kind, collected once.
            std::set<int> selB, selS, selP, selA;
            if (m_selection)
                for (const auto& e : m_selection->getSelection()) {
                    if (e.type == SelectionType::Body   && e.bodyId   >= 0) selB.insert(e.bodyId);
                    if (e.type == SelectionType::Sketch && e.sketchId >= 0) selS.insert(e.sketchId);
                    if (e.type == SelectionType::Plane  && e.planeId  >= 0) selP.insert(e.planeId);
                    if (e.type == SelectionType::Axis   && e.axisId   >= 0) selA.insert(e.axisId);
                }
            // Plain tap = single-select; with the Multi toggle armed, bodies
            // toggle in/out of the selection (same semantics as the Items
            // panel's Ctrl+click). Body picks are navigation-only, exactly
            // like the Items panel's rows; other kinds select plainly.
            auto pick = [&](SelectionEntry entry, bool multiOk) {
                if (!m_selection) return;
                if (multiOk && m_multiSelectToggle) m_selection->toggleSelection(entry);
                else                                m_selection->select(entry);
                if (entry.type == SelectionType::Body)
                    m_selection->setNavigationOnly(true);
            };

            bool any = false;
            const auto bodyIds = m_document->getAllBodyIds();
            if (!bodyIds.empty()) {
                any = true;
                touchui::sectionHeader("Bodies");
                for (int id : bodyIds) {
                    ImGui::PushID(id);
                    bool visible = m_document->isBodyVisible(id);
                    auto act = touchui::listRow("body", &visible,
                                                m_document->getBodyName(id).c_str(),
                                                selB.count(id) > 0,
                                                /*withOverflow=*/false);
                    if (act.toggled) m_document->setBodyVisible(id, visible);
                    if (act.clicked) {
                        SelectionEntry e;
                        e.type = SelectionType::Body;
                        e.bodyId = id;
                        // Parity with ItemsPanel::makeEntry — downstream code
                        // (highlight outline, ops) expects body entries to
                        // carry the shape.
                        try { e.shape = m_document->getBody(id); } catch (...) {}
                        pick(e, /*multiOk=*/true);
                    }
                    ImGui::PopID();
                }
            }
            const auto sketchIds = m_document->getAllSketchIds();
            if (!sketchIds.empty()) {
                any = true;
                touchui::sectionHeader("Sketches");
                for (int id : sketchIds) {
                    ImGui::PushID(id);
                    bool visible = m_document->isSketchVisible(id);
                    auto act = touchui::listRow("sketch", &visible,
                                                m_document->getSketchName(id).c_str(),
                                                selS.count(id) > 0,
                                                /*withOverflow=*/false);
                    if (act.toggled) m_document->setSketchVisible(id, visible);
                    if (act.clicked) {
                        SelectionEntry e;
                        e.type = SelectionType::Sketch;
                        e.sketchId = id;
                        pick(e, /*multiOk=*/false);
                    }
                    ImGui::PopID();
                }
            }
            const auto planeIds = m_document->getAllPlaneIds();
            const auto axisIds  = m_document->getAllAxisIds();
            if (!planeIds.empty() || !axisIds.empty()) {
                any = true;
                touchui::sectionHeader("Construction");
                for (int id : planeIds) {
                    ImGui::PushID(id + 100000); // avoid plane/axis id collisions
                    const auto* p = m_document->getPlane(id);
                    std::string label = p ? p->name
                                          : std::string("Plane ") + std::to_string(id);
                    bool visible = m_document->isPlaneVisible(id);
                    auto act = touchui::listRow("plane", &visible, label.c_str(),
                                                selP.count(id) > 0,
                                                /*withOverflow=*/false);
                    if (act.toggled) m_document->setPlaneVisible(id, visible);
                    if (act.clicked) {
                        SelectionEntry e;
                        e.type = SelectionType::Plane;
                        e.planeId = id;
                        pick(e, /*multiOk=*/false);
                    }
                    ImGui::PopID();
                }
                for (int id : axisIds) {
                    ImGui::PushID(id + 200000);
                    const auto* a = m_document->getAxis(id);
                    std::string label = a ? a->name
                                          : std::string("Axis ") + std::to_string(id);
                    bool visible = m_document->isAxisVisible(id);
                    auto act = touchui::listRow("axis", &visible, label.c_str(),
                                                selA.count(id) > 0,
                                                /*withOverflow=*/false);
                    if (act.toggled) m_document->setAxisVisible(id, visible);
                    if (act.clicked) {
                        SelectionEntry e;
                        e.type = SelectionType::Axis;
                        e.axisId = id;
                        pick(e, /*multiOk=*/false);
                    }
                    ImGui::PopID();
                }
            }
            if (!any)
                ImGui::TextColored(touchui::textDim(), "Nothing here yet");
        }
        ImGui::End();
    }

    // ── Contextual tool bar — the same catalogue the modern layout's rail
    //    uses, floating on the LEFT edge, vertically centered. Sketch mode
    //    appends Finish/Exit pills below the tools. Tall catalogues (sketch
    //    mode on a landscape tablet) can exceed the work rect, so cap the
    //    height and let the bar scroll rather than run off-screen.
    ImGui::SetNextWindowPos(ImVec2(wp.x + m, wp.y + ws.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.0f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
                                        ImVec2(FLT_MAX, ws.y - 2.0f * m));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, touchui::panelBg());
    if (ImGui::Begin("##LiteToolBar", nullptr,
                     kFloat & ~ImGuiWindowFlags_NoScrollbar)) {
        if (m_toolbar) {
            for (const auto& tool : m_toolbar->railTools()) {
                const bool clicked = touchui::railButton(
                    tool.label, tool.icon, tool.label, tool.active, 64.0f * s);
                tip(tool.tip);
                if (tool.action == ToolAction::Polygon)
                    renderRailPolygonSidesPopup(clicked);
                else if (clicked)
                    handleToolAction(static_cast<int>(tool.action));
            }
            if (m_inSketchMode) {
                const bool toolRunning = m_sketchTool && m_sketchTool->isPlacing();
                const char* finishLbl = toolRunning ? "Finish" : "Finish Sketch";
                const char* exitLbl   = toolRunning ? "Cancel" : "Discard Sketch";
                ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
                if (touchui::pillButton("finish", MZ_ICON_FINISH, finishLbl, true)) {
                    if (toolRunning)
                        recordSketchMutation([&]{ m_sketchTool->onConfirm(); });
                    else
                        handleToolAction(static_cast<int>(ToolAction::FinishSketch));
                }
                tip(toolRunning
                        ? "Finish the current shape, keeping the points placed"
                        : "Leave sketch mode, keeping the sketch");
                if (touchui::pillButton("exit", MZ_ICON_DISCARD, exitLbl)) {
                    if (toolRunning)
                        m_sketchTool->onCancel();
                    else
                        ImGui::OpenPopup("Discard sketch?"); // confirm — destructive
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
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();

    // ── "+" create FAB (bottom-right) ───────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - m, wp.y + ws.y - m),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    if (ImGui::Begin("##LiteFab", nullptr, kFloat)) {
        if (touchui::fab("create", MZ_ICON_ADD))
            ImGui::OpenPopup("##LiteCreate");
        tip("Create: a sketch or a primitive solid");
        if (ImGui::BeginPopup("##LiteCreate")) {
            // Mirror the modern rail's create logic instead of dumping every
            // option flat: sketch is contextual (on a picked face/plane if there
            // is one, else a "New Sketch" submenu of world planes), the five
            // primitives live under ONE "Primitive" submenu, and construction
            // geometry derives from the selection — so only the relevant, grouped
            // create tools show, matching the classic + modern layouts.
            const bool faceOrPlaneSel = m_selection &&
                (m_selection->hasSelectedFaces() ||
                 m_selection->primaryType() == SelectionType::Plane);
            if (faceOrPlaneSel) {
                if (ImGui::MenuItem(MZ_ICON_SKETCH "  Sketch on selection"))
                    handleToolAction(static_cast<int>(ToolAction::SketchOnFace));
            } else if (ImGui::BeginMenu(MZ_ICON_SKETCH "  New Sketch")) {
                if (ImGui::MenuItem("XY plane"))
                    handleToolAction(static_cast<int>(ToolAction::StartSketchXY));
                if (ImGui::MenuItem("XZ plane"))
                    handleToolAction(static_cast<int>(ToolAction::StartSketchXZ));
                if (ImGui::MenuItem("YZ plane"))
                    handleToolAction(static_cast<int>(ToolAction::StartSketchYZ));
                ImGui::EndMenu();
            }
            if (m_pluginContext &&
                ImGui::BeginMenu(MZ_ICON_PRIMITIVE "  Primitive")) {
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
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(MZ_ICON_FOCUS "  Construction")) {
                renderConstructionMenuItems();
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    // ── History: a bottom toggle whose REOPEN button sits exactly where its
    //    minimize chevron is (not up on the Items rail). The toggle is a fixed
    //    left-anchored button — a chevron to hide while the strip is open, the
    //    History clock to reopen while collapsed — and the Fusion-360-style
    //    step strip (tap a box for its properties popup: edit params, roll
    //    to it, toggle/delete) is a separate scrolling window to its right.
    //    Hidden in sketch mode: rolling the host body back under a live sketch
    //    is forbidden (see History::setUndoFloor).
    const int steps = m_history ? m_history->stepCount() : 0;
    const bool histAvail = !m_inSketchMode && m_document && m_history;
    const float histX   = wp.x + m;   // bottom-left corner (fps moved to top)
    const float histGap = 8.0f * s;
    // Last frame's measured strip height, so the toggle can be centred on the
    // strip's vertical middle — the strip window carries padding the
    // borderless button doesn't, so plain bottom-alignment left it sitting low.
    // The height persists once the strip has rendered, so the button holds that
    // same centred position when collapsed instead of snapping back down.
    static float s_histStripH = 0.0f;
    if (histAvail) {
        if (s_histStripH > 0.0f)
            ImGui::SetNextWindowPos(
                ImVec2(histX, (wp.y + ws.y - m) - s_histStripH * 0.5f),
                ImGuiCond_Always, ImVec2(0.0f, 0.5f));
        else
            ImGui::SetNextWindowPos(ImVec2(histX, wp.y + ws.y - m),
                                    ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);   // the button draws its own solid fill
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("##LiteHistoryToggle", nullptr, kFloat)) {
            // Clock icon + "History" label — mirrors the Items button, and
            // accent-fills while the timeline is open. The button is the same
            // open or closed, so the reopen state sits exactly where the
            // collapse state was.
            if (touchui::railButton("histToggle", MZ_ICON_HISTORY, "History",
                                    m_imTouchTimeline, railBtnW, /*solid=*/true)) {
                m_imTouchTimeline = !m_imTouchTimeline;
                saveAppSettings();
            }
            tip(m_imTouchTimeline ? "Hide the history timeline"
                                  : "Show the history timeline");
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    if (m_imTouchTimeline && histAvail) {
        const float stripX = histX + railBtnW + histGap;
        ImGui::SetNextWindowPos(ImVec2(stripX, wp.y + ws.y - m),
                                ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        // End before the create FAB (bottom-right).
        const float fabClear = m + 76.0f * s;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0, 0), ImVec2((wp.x + ws.x - fabClear) - stripX, FLT_MAX));
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, touchui::panelBg());
        if (ImGui::Begin("##LiteTimeline", nullptr,
                         (kFloat & ~ImGuiWindowFlags_NoScrollbar) |
                             ImGuiWindowFlags_HorizontalScrollbar)) {
            s_histStripH = ImGui::GetWindowSize().y;  // for centring the toggle
            // Empty history still shows a hint so toggling History in a fresh
            // project doesn't look like it did nothing.
            if (steps == 0)
                ImGui::TextColored(touchui::textDim(),
                                   "History: no steps yet");
            const int curr = m_history->currentStep();
            const int failedAt = m_history->lastReplayFailure();
            const bool histLocked = anyInteractivePreviewActive();
            const ImU32 amber = ImGui::GetColorU32(ImVec4(0.95f, 0.75f, 0.3f, 1.0f));
            const ImU32 red   = ImGui::GetColorU32(ImVec4(1.0f, 0.45f, 0.35f, 1.0f));

            // Auto-scroll the current step into view whenever history mutates
            // (new op, undo/redo, edit) — not on user scrolls.
            static unsigned s_seenRev = ~0u;
            const bool historyMoved = (s_seenRev != m_history->revision());

            bool wantOpen = false;
            for (int i = 0; i < steps; ++i) {
                const Operation* op = m_history->getStep(i);
                if (!op) continue;
                if (i > 0) ImGui::SameLine(0.0f, 6.0f * s);
                ImGui::PushID(i);
                ImU32 tint = 0;
                if (i == failedAt)          tint = red;
                else if (op->isReloaded())  tint = amber;
                const bool dim = (i > curr) || !op->isEnabled();
                std::string stepName = op->name();
                if (stepName.empty()) stepName = op->typeId();
                if (touchui::timelineBox("step", stepIcon(op->typeId()),
                                         i == curr, i == m_imTouchHistoryEdit,
                                         dim, tint, 0.0f, stepName.c_str())) {
                    m_imTouchHistoryEdit = (m_imTouchHistoryEdit == i) ? -1 : i;
                    wantOpen = (m_imTouchHistoryEdit == i);
                    // Drive the viewport's orange edited-element highlight.
                    if (m_historyPanel)
                        m_historyPanel->setEditingStep(m_imTouchHistoryEdit);
                }
                if (i == curr && historyMoved) ImGui::SetScrollHereX(0.5f);
                ImGui::PopID();
            }
            s_seenRev = m_history->revision();

            if (wantOpen) ImGui::OpenPopup("##LiteStepProps");
            ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f * s, 0),
                                                ImVec2(360.0f * s, 460.0f * s));
            if (ImGui::BeginPopup("##LiteStepProps")) {
                const Operation* op =
                    (m_imTouchHistoryEdit >= 0 && m_imTouchHistoryEdit < steps)
                        ? m_history->getStep(m_imTouchHistoryEdit)
                        : nullptr;
                if (!op) {
                    ImGui::CloseCurrentPopup();
                } else {
                    const int i = m_imTouchHistoryEdit;
                    std::string detail = op->description();
                    if (detail.empty()) detail = op->name();
                    ImGui::TextColored(touchui::textPrimary(), "%d. %s",
                                       i + 1, detail.c_str());
                    if (!op->isEnabled())
                        ImGui::TextColored(touchui::textDim(), "Disabled");
                    if (i > curr)
                        ImGui::TextColored(touchui::textDim(),
                                           "Undone \xE2\x80\x94 Go Here replays it.");
                    if (i == failedAt) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                            "Couldn't recompute after an upstream change. Edit "
                            "its parameters, fix the step before it, or delete it.");
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::Separator();

                    if (op->isReloaded()) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f),
                            "Restored from an older save \xE2\x80\x94 no editable "
                            "parameters. Undo/redo still work.");
                        ImGui::PopTextWrapPos();
                    } else {
                        // The op's own parameter editor — identical widgets to
                        // the desktop History panel's Properties section.
                        ImGui::BeginChild("##props", ImVec2(0.0f, 200.0f * s),
                                          true);
                        const_cast<Operation*>(op)->renderProperties();
                        ImGui::EndChild();
                        ImGui::BeginDisabled(histLocked);
                        if (ImGui::Button("Apply Changes",
                                          ImVec2(-1.0f, 44.0f * s))) {
                            // Same sequence as HistoryPanel: carry inline
                            // sketch-dimension edits into later snapshots
                            // FIRST, then a transactional replay, then cascade
                            // so bodies built from the sketch follow.
                            m_history->propagateSketchValueEdits(i, *m_document);
                            const bool applied = m_history->editStep(
                                i, *m_document, /*transactional=*/true);
                            m_meshesDirty = true;
                            if (applied) {
                                if (auto* se =
                                        dynamic_cast<const SketchEditOp*>(op)) {
                                    auto tgt = se->getTarget();
                                    int sid = tgt ? m_document->findSketchId(
                                                        tgt.get())
                                                  : -1;
                                    if (sid >= 0) cascadeFromSketchEdit(sid);
                                } else if (auto* st = dynamic_cast<
                                               const SketchTransformOp*>(op)) {
                                    if (st->getSketchId() >= 0)
                                        cascadeFromSketchEdit(st->getSketchId());
                                }
                            }
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::Separator();

                    const float bw = 104.0f * s;
                    ImGui::BeginDisabled(histLocked || i == curr);
                    if (ImGui::Button("Go Here", ImVec2(bw, 44.0f * s))) {
                        // Roll the model to this step (Fusion's marker drag).
                        // Progress guard: a failed replay mid-walk must not
                        // spin forever.
                        while (m_history->currentStep() > i) {
                            const int before = m_history->currentStep();
                            undoWithCascade();
                            if (m_history->currentStep() == before) break;
                        }
                        while (m_history->currentStep() < i) {
                            const int before = m_history->currentStep();
                            redoWithCascade();
                            if (m_history->currentStep() == before) break;
                        }
                        m_meshesDirty = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(histLocked);
                    if (ImGui::Button(op->isEnabled() ? "Disable" : "Enable",
                                      ImVec2(bw, 44.0f * s))) {
                        // In-place toggle — preserves base bodies the op
                        // modifies (replayAll's doc.clear() would drop them).
                        m_history->setStepEnabled(i, !op->isEnabled(),
                                                  *m_document);
                        m_meshesDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete", ImVec2(bw, 44.0f * s))) {
                        if (m_history->removeStep(i, *m_document)) {
                            m_imTouchHistoryEdit = -1;
                            if (m_historyPanel)
                                m_historyPanel->setEditingStep(-1);
                            ImGui::CloseCurrentPopup();
                        } else {
                            showToast(
                                "Can't delete: a later operation depends on it.");
                        }
                        m_meshesDirty = true;
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndPopup();
            } else if (m_imTouchHistoryEdit >= 0) {
                // Popup dismissed by tapping elsewhere — drop the edit state
                // (and the viewport highlight) with it.
                m_imTouchHistoryEdit = -1;
                if (m_historyPanel) m_historyPanel->setEditingStep(-1);
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // ── fps readout — a small solid chip at the top-centre. Hidden entirely
    //    via Settings → Appearance → "Show FPS counter".
    if (m_showFps) {
        ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x * 0.5f, wp.y + m),
                                ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, touchui::panelBg());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f * s, 4.0f * s));
        if (ImGui::Begin("##LiteFps", nullptr, kFloat)) {
            ImGui::SetWindowFontScale(0.82f);   // smaller than the shell text
            ImGui::TextColored(touchui::textDim(), "%.0f fps",
                               ImGui::GetIO().Framerate);
            ImGui::SetWindowFontScale(1.0f);
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    ImGui::PopStyleVar(2); // WindowRounding + WindowBorderSize
}

} // namespace materializr
