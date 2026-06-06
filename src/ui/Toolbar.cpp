#include "Toolbar.h"
#include "../core/SelectionManager.h"
#include "../core/History.h"
#include "../core/Operation.h"
#include "../plugin/PluginRegistry.h"
#include "../plugin/PluginContext.h"
#include "../plugin/Contributions.h"
#include <imgui.h>
#include <cmath>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopoDS.hxx>

namespace materializr {

Toolbar::Toolbar() = default;

// Tooltip helper. Wraps long descriptions across multiple lines instead of
// the single-line behaviour ImGui::SetItemTooltip gives by default — tooltip
// strings can run to a couple of sentences and used to truncate awkwardly.
// BeginItemTooltip handles the hover-delay; PushTextWrapPos gives us the
// width cap (in pixels, roughly 28em at the current font size).
void Toolbar::tip(const char* text) const {
    if (!m_showTooltips) return;
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Toolbar::setSelectionManager(const SelectionManager* sel) {
    m_selection = sel;
}

ToolAction Toolbar::render() {
    ToolAction action = ToolAction::None;

    ImGui::Begin("Tools");

    if (m_sketchMode) {
        action = renderSketchTools();
    } else if (!m_selection || !m_selection->hasSelection()) {
        action = renderNoSelectionTools();
    } else if (m_selection->hasSelectedSketchRegions()) {
        action = renderSketchRegionTools();
    } else if (m_selection->primaryType() == SelectionType::Plane) {
        action = renderPlaneSelectedTools();
    } else if (m_selection->primaryType() == SelectionType::Axis) {
        action = renderAxisSelectedTools();
    } else if (m_selection->hasSelectedSketches()) {
        action = renderSketchSelectedTools();
    } else if (m_selection->hasSelectedFaces()) {
        action = renderFaceTools();
        if (action == ToolAction::None) {
            // Body tools (gizmos + Mirror) stay available when a face is
            // selected so the user can move/rotate/scale the whole body, but
            // the whole-body plugin contributions (Split / Duplicate / Pattern)
            // are skipped — they don't apply in face-selection context.
            action = renderBodyTools(/*includePluginButtons=*/false);
        }
    } else if (m_selection->hasSelectedBodies()) {
        action = renderBodyTools();
    } else if (m_selection->hasSelectedEdges()) {
        action = renderEdgeTools();
    } else {
        action = renderNoSelectionTools();
    }

    ImGui::End();
    return action;
}

void Toolbar::setSketchMode(bool active) {
    m_sketchMode = active;
}

bool Toolbar::isSketchMode() const {
    return m_sketchMode;
}

// Render plugin-contributed buttons matching any of the given contexts.
// contextMask is a bitmask: bit N = SelectionContext(N).
void Toolbar::renderPluginButtons(int contextMask) {
    if (!m_pluginCtx) return;
    auto& contribs = PluginRegistry::instance().toolbarContributions();
    std::string lastSection;
    for (size_t i = 0; i < contribs.size(); ++i) {
        auto& c = contribs[i];
        if (!((1 << static_cast<int>(c.context)) & contextMask)) continue;
        if (c.section != lastSection) {
            if (!lastSection.empty()) ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", c.section.c_str());
            ImGui::Separator();
            lastSection = c.section;
        }
        ImGui::PushID(static_cast<int>(i + 10000));
        if (ImGui::Button(c.name.c_str(), ImVec2(-1, 30))) {
            if (c.toolFactory) {
                PluginRegistry::instance().activateTool(c.toolFactory(), *m_pluginCtx);
            } else if (c.action) {
                c.action(*m_pluginCtx);
            }
        }
        if (!c.tooltip.empty()) tip(c.tooltip.c_str());
        ImGui::PopID();
    }
}

void Toolbar::renderAddPlaneMenu() {
    if (!m_selection || !m_pluginCtx) return;

    // Detect which construction-plane modes the current selection supports.
    int  planarFaces = 0, planeCount = 0;
    bool haveCyl = false, straightEdge = false, haveAxis = false;
    for (const auto& e : m_selection->getSelection()) {
        if (e.type == SelectionType::Plane) { ++planeCount; continue; }
        if (e.type == SelectionType::Axis)  { haveAxis = true; continue; }
        if (e.shape.IsNull()) continue;
        try {
            if (e.type == SelectionType::Face) {
                Handle(Geom_Surface) s = BRep_Tool::Surface(TopoDS::Face(e.shape));
                if (!s.IsNull()) {
                    if (s->IsKind(STANDARD_TYPE(Geom_Plane))) ++planarFaces;
                    else if (!Handle(Geom_CylindricalSurface)::DownCast(s).IsNull())
                        haveCyl = true;
                }
            } else if (e.type == SelectionType::Edge) {
                BRepAdaptor_Curve ad(TopoDS::Edge(e.shape));
                if (ad.GetType() == GeomAbs_Line) straightEdge = true;
            }
        } catch (...) {}
    }

    const bool midplane = (planarFaces >= 2) || (planeCount >= 2);
    if (!(midplane || haveCyl || haveAxis || straightEdge)) return;

    ImGui::Separator();
    if (ImGui::Button("Add Plane...", ImVec2(-1, 30)))
        ImGui::OpenPopup("AddPlaneMenu");
    tip("Create a construction plane derived from the current selection.");
    if (ImGui::BeginPopup("AddPlaneMenu")) {
        if (midplane && ImGui::MenuItem("Midplane (between the 2 selected)"))
            m_pluginCtx->requestInteractiveOp("Midplane");
        if (haveCyl) {
            if (ImGui::MenuItem("Tangent to cylinder"))
                m_pluginCtx->requestInteractiveOp("TangentPlane");
            if (ImGui::MenuItem("Perpendicular to cylinder axis"))
                m_pluginCtx->requestInteractiveOp("PlaneNormalToAxis");
            if (ImGui::MenuItem("Through cylinder axis (longitudinal)"))
                m_pluginCtx->requestInteractiveOp("PlaneThroughAxis");
        } else if (haveAxis || straightEdge) {
            if (ImGui::MenuItem(straightEdge ? "Normal to edge" : "Normal to axis"))
                m_pluginCtx->requestInteractiveOp("PlaneNormalToAxis");
        }
        ImGui::EndPopup();
    }
}

void Toolbar::renderAddAxisMenu() {
    if (!m_selection || !m_pluginCtx) return;

    int  planarFaces = 0, planeCount = 0, vertexCount = 0;
    bool haveCyl = false, straightEdge = false;
    for (const auto& e : m_selection->getSelection()) {
        if (e.type == SelectionType::Plane)  { ++planeCount;  continue; }
        if (e.type == SelectionType::Vertex) { ++vertexCount; continue; }
        if (e.shape.IsNull()) continue;
        try {
            if (e.type == SelectionType::Face) {
                Handle(Geom_Surface) s = BRep_Tool::Surface(TopoDS::Face(e.shape));
                if (!s.IsNull()) {
                    if (s->IsKind(STANDARD_TYPE(Geom_Plane))) ++planarFaces;
                    else if (!Handle(Geom_CylindricalSurface)::DownCast(s).IsNull())
                        haveCyl = true;
                }
            } else if (e.type == SelectionType::Edge) {
                BRepAdaptor_Curve ad(TopoDS::Edge(e.shape));
                if (ad.GetType() == GeomAbs_Line) straightEdge = true;
            }
        } catch (...) {}
    }

    const bool twoPlanes  = (planeCount >= 2) || (planarFaces >= 2);
    const bool twoVerts   = (vertexCount >= 2);
    const bool faceNormal = (planarFaces >= 1);
    if (!(haveCyl || straightEdge || twoVerts || faceNormal || twoPlanes)) return;

    ImGui::Separator();
    if (ImGui::Button("Add Axis...", ImVec2(-1, 30)))
        ImGui::OpenPopup("AddAxisMenu");
    tip("Create a construction axis derived from the current selection.");
    if (ImGui::BeginPopup("AddAxisMenu")) {
        if (haveCyl && ImGui::MenuItem("From cylinder axis"))
            m_pluginCtx->requestInteractiveOp("AxisFromCylinder");
        if (straightEdge && ImGui::MenuItem("Along edge"))
            m_pluginCtx->requestInteractiveOp("AxisAlongEdge");
        if (twoVerts && ImGui::MenuItem("Through two vertices"))
            m_pluginCtx->requestInteractiveOp("AxisTwoPoints");
        if (faceNormal && ImGui::MenuItem("Normal to face"))
            m_pluginCtx->requestInteractiveOp("AxisNormalToFace");
        if (twoPlanes && ImGui::MenuItem("Intersection of two planes"))
            m_pluginCtx->requestInteractiveOp("AxisTwoPlanes");
        ImGui::EndPopup();
    }
}

ToolAction Toolbar::renderSketchTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Sketch Tools");
    // Constraint status badge — only appears once the sketch has constraints.
    // Green = Fully constrained, blue = Under (free DOF), red = Over
    // (contradictory). Hover shows the precise degree-of-freedom count.
    if (m_sketchSolverState >= 0) {
        ImVec4 col;
        const char* label = "";
        switch (m_sketchSolverState) {
            case 0: col = ImVec4(0.20f, 0.85f, 0.35f, 1.0f); label = "Fully constrained"; break;
            case 1: col = ImVec4(0.30f, 0.65f, 1.00f, 1.0f); label = "Under-constrained"; break;
            case 2: col = ImVec4(0.95f, 0.30f, 0.30f, 1.0f); label = "Over-constrained";   break;
            default: col = ImVec4(0.7f,0.7f,0.7f,1.0f);      label = "";                    break;
        }
        ImGui::TextColored(col, "● %s", label);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Degrees of freedom: %d\n"
                              "Negative = contradictory constraints, "
                              "zero = uniquely determined, "
                              "positive = free to drag.",
                              m_sketchSolverDof);
        }
    }
    ImGui::Separator();

    // Snap on/off + step both live in the corner widget next to the ViewCube
    // now — single source of truth. The duplicate grid-step row used to sit
    // here but was removed once the corner widget proved sufficient.

    // Render a sketch-tool button with a thick light-grey border when it's
    // the currently active mode. Caller checks the return value to set
    // `action`. Mode id matches SketchToolMode enum (see Toolbar.h).
    auto skBtn = [&](const char* label, int modeId) -> bool {
        bool active = (m_activeSketchMode == modeId);
        if (active) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }
        bool clicked = ImGui::Button(label, ImVec2(-1, 30));
        if (active) {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        return clicked;
    };

    if (skBtn("Select / Move", 1)) action = ToolAction::SelectSketch;
    tip("Pick sketch elements (points, lines, regions). Drag selection to move.");
    if (skBtn("Line",      2))     action = ToolAction::Line;
    tip("Draw straight line segments. Click to add vertices, Esc to finish.");
    if (skBtn("Circle",    3))     action = ToolAction::Circle;
    tip("Draw a circle: click centre, drag to radius.");
    if (skBtn("Rectangle", 4))     action = ToolAction::Rectangle;
    tip("Draw an axis-aligned rectangle: click one corner, drag to the opposite.");
    if (skBtn("Arc",       5))     action = ToolAction::Arc;
    tip("Three-point arc: click start, end, then a point on the curve.");
    if (skBtn("Spline",    6))     action = ToolAction::Spline;
    tip("Multi-point spline. Click control points, Enter to finish.");
    if (skBtn("Polygon",   7))     action = ToolAction::Polygon;
    tip("Regular polygon: click centre, drag to size. Side count in properties.");
    if (skBtn("Trim",      8))     action = ToolAction::Trim;
    tip("Trim a sketch segment at the nearest intersections.");

    // Transforms operate on the current sketch-element selection (Select tool +
    // click/Ctrl+click on points and lines). No-op if nothing is selected.
    // Rotate is the gizmo's ring handle (drag = 15° snap, popup for exact value),
    // so it doesn't get its own button.
    ImGui::Separator();
    if (ImGui::Button("Copy",   ImVec2(-1, 28))) action = ToolAction::SketchCopy;
    tip("Duplicate the selected sketch elements at an offset.");
    if (ImGui::Button("Mirror", ImVec2(-1, 28))) action = ToolAction::SketchMirror;
    tip("Mirror selected elements across a sketch line you'll pick next.");
    if (ImGui::Button("Linear Pattern", ImVec2(-1, 28))) action = ToolAction::SketchLinearPattern;
    tip("Copy the selected sketch elements N times along the sketch X axis.");
    if (ImGui::Button("Radial Pattern", ImVec2(-1, 28))) action = ToolAction::SketchRadialPattern;
    tip("Copy the selected sketch elements around an origin you specify.");

    // Formal-constraint buttons only appear in "Constraint buttons" helper
    // mode (Settings → Interface → Sketch helper). In default "Inferences"
    // mode the constraints live exclusively in the sketch-viewport right-
    // click menu so the panel stays uncluttered. Buttons are filtered by
    // selection arity so the user only ever sees options that can apply.
    if (m_sketchHelperMode == 1 &&
        (m_selPoints > 0 || m_selLines > 0 ||
         m_selCircles > 0 || m_selArcs > 0)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Constraints");
        ImGui::Separator();
        if (m_selLines >= 1) {
            if (ImGui::Button("Horizontal", ImVec2(-1, 24))) action = ToolAction::SketchConstrainHorizontal;
            tip("Lock the selected line(s) horizontal.");
            if (ImGui::Button("Vertical", ImVec2(-1, 24))) action = ToolAction::SketchConstrainVertical;
            tip("Lock the selected line(s) vertical.");
        }
        if (m_selPoints >= 2) {
            if (ImGui::Button("Coincident", ImVec2(-1, 24))) action = ToolAction::SketchConstrainCoincident;
            tip("Make the selected points share the same position.");
            if (ImGui::Button("Distance", ImVec2(-1, 24))) action = ToolAction::SketchDimDistance;
            tip("Lock the distance between the points at its current value.");
        }
        if (m_selLines >= 2) {
            if (ImGui::Button("Parallel", ImVec2(-1, 24))) action = ToolAction::SketchConstrainParallel;
            tip("Lock the selected lines to stay parallel to the first one.");
            if (ImGui::Button("Perpendicular", ImVec2(-1, 24))) action = ToolAction::SketchConstrainPerpendicular;
            tip("Lock the selected lines perpendicular to the first one.");
            if (ImGui::Button("Equal", ImVec2(-1, 24))) action = ToolAction::SketchConstrainEqual;
            tip("Force the selected lines to share a common length.");
            if (ImGui::Button("Angle", ImVec2(-1, 24))) action = ToolAction::SketchDimAngle;
            tip("Lock the angle between the selected lines at its current value.");
        }
        if (m_selPoints >= 1) {
            if (ImGui::Button("Fix Position", ImVec2(-1, 24))) action = ToolAction::SketchConstrainFixed;
            tip("Pin the selected point(s) at their current position.");
        }
        int curves = m_selCircles + m_selArcs;
        if (curves >= 1) {
            if (ImGui::Button("Radius", ImVec2(-1, 24))) action = ToolAction::SketchDimRadius;
            tip("Lock the radius of the selected circle(s)/arc(s) at its current value.");
        }
        if (curves >= 1 && m_selLines >= 1) {
            if (ImGui::Button("Tangent", ImVec2(-1, 24))) action = ToolAction::SketchConstrainTangent;
            tip("Make each selected curve tangent to each selected line.");
        }
        if (curves >= 2) {
            if (ImGui::Button("Concentric", ImVec2(-1, 24))) action = ToolAction::SketchConstrainConcentric;
            tip("Force the selected curves to share a centre with the first.");
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Measure", ImVec2(-1, 28))) action = ToolAction::Measure;
    tip("Measure distance / length between picked sketch elements.");

    if (!m_cameraOrtho) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.85f, 1.0f));
        if (ImGui::Button("Look at Sketch", ImVec2(-1, 30)))
            action = ToolAction::LookAtSketch;
        tip("Snap the camera to look straight down the sketch plane (orthographic).");
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button("Finish Sketch", ImVec2(-1, 30)))
        action = ToolAction::FinishSketch;
    tip("Leave sketch mode and return to the 3D viewport. Keeps the sketch.");
    if (ImGui::Button("Exit Sketch (discard)", ImVec2(-1, 30)))
        action = ToolAction::ExitSketchDiscard;
    tip("Discard the current sketch entirely and leave sketch mode. Rewinds "
        "history to before the sketch was entered; the body returns to its "
        "pre-sketch state. Useful when you've started a sketch you don't "
        "want to keep — Esc-while-placing only cancels the in-progress shape, "
        "this clears everything.");

    // Plugin buttons for InSketchMode context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::InSketchMode));

    return action;
}

ToolAction Toolbar::renderNoSelectionTools() {
    ToolAction action = ToolAction::None;

    // Start a sketch on a base plane — lets you model from scratch with no body.
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Create");
    ImGui::Separator();
    if (ImGui::Button("Sketch on XY", ImVec2(-1, 30))) action = ToolAction::StartSketchXY;
    tip("Start a new sketch on the world XY (floor) plane.");
    if (ImGui::Button("Sketch on XZ", ImVec2(-1, 30))) action = ToolAction::StartSketchXZ;
    tip("Start a new sketch on the world XZ (front) plane.");
    if (ImGui::Button("Sketch on YZ", ImVec2(-1, 30))) action = ToolAction::StartSketchYZ;
    tip("Start a new sketch on the world YZ (side) plane.");

    // Axis from a vertex selection (two vertices → through-points axis). This
    // is the fallback context vertices land in; renders nothing otherwise.
    renderAddAxisMenu();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Inspect");
    ImGui::Separator();
    if (ImGui::Button("Measure", ImVec2(-1, 30))) action = ToolAction::Measure;
    tip("Measure distance, length, or angle between picked features.");

    // Plugin buttons: NoSelection + Always
    int mask = (1 << static_cast<int>(SelectionContext::NoSelection))
             | (1 << static_cast<int>(SelectionContext::Always));
    renderPluginButtons(mask);

    return action;
}

ToolAction Toolbar::renderBodyTools(bool includePluginButtons) {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Transform");
    ImGui::Separator();

    // Gizmo modes side by side, then Mirror.
    float third = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Move", ImVec2(third, 30)))   action = ToolAction::Move;
    tip("Show the translate gizmo. Drag axes / planes to move. (W)");
    ImGui::SameLine();
    if (ImGui::Button("Rotate", ImVec2(third, 30))) action = ToolAction::Rotate;
    tip("Show the rotate gizmo. Drag rings to rotate around each axis. (E)");
    ImGui::SameLine();
    if (ImGui::Button("Scale", ImVec2(third, 30)))  action = ToolAction::Scale;
    tip("Show the scale gizmo. Drag handles to resize. (R)");
    // Mirror + Revolve share the row so they read as the "uses an
    // already-created primitive" pair (mirror plane / construction axis).
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Mirror", ImVec2(half, 30)))    action = ToolAction::Mirror;
    tip("Mirror the selected bodies across a plane you pick next.");
    ImGui::SameLine();
    if (ImGui::Button("Revolve", ImVec2(half, 30)))   action = ToolAction::Revolve;
    tip("Revolve the selected body / bodies around a Construction Axis. "
        "Pick an axis next; the popup handles angle + mode. Multi-body "
        "selection rotates as a group.");

    // Plugin buttons: always include HasBodies (1+ bodies), and only include
    // MultipleBodies (2+ bodies, e.g. Union / Subtract / Intersect) when at
    // least two bodies are actually selected. Previously OR-ing them both
    // unconditionally meant boolean ops appeared with a single body picked,
    // which can't do anything.
    if (includePluginButtons) {
        int mask = (1 << static_cast<int>(SelectionContext::HasBodies));
        if (m_selection && m_selection->selectedBodyCount() >= 2) {
            mask |= (1 << static_cast<int>(SelectionContext::MultipleBodies));
        }
        renderPluginButtons(mask);
    }

    return action;
}

ToolAction Toolbar::renderFaceTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Face Operations");
    ImGui::Separator();

    if (ImGui::Button("Sketch on Face", ImVec2(-1, 30)))
        action = ToolAction::SketchOnFace;
    tip("Start a new sketch lying on the picked face.");
    if (ImGui::Button("Push / Pull", ImVec2(-1, 30)))
        action = ToolAction::PushPull;
    tip("Drag the face along its normal to extrude (+) or cut (−) into the body.");
    // Extrude From a face → make a new body that's the face's silhouette
    // swept along its normal. Push/Pull modifies the source body; Extrude
    // always creates a separate body. Same ToolAction the sketch toolbar
    // uses; the handler dispatches by selection type.
    if (ImGui::Button("Extrude From", ImVec2(-1, 30)))
        action = ToolAction::ExtrudeSketch;
    tip("Make a NEW body by extruding this face's silhouette (source body unchanged).");
    if (ImGui::Button("Shell", ImVec2(-1, 30)))
        action = ToolAction::Shell;
    tip("Hollow the body, removing the picked face. Wall thickness in the popup.");
    if (ImGui::Button("Taper", ImVec2(-1, 30)))
        action = ToolAction::Taper;
    tip("Tilt the picked face(s) by an angle about the body's base - "
        "a cylinder wall becomes a cone, box sides become a pyramid. "
        "Angle and pull axis in the popup.");
    if (ImGui::Button("Scale Face", ImVec2(-1, 30)))
        action = ToolAction::ScaleFace;
    tip("Pinch or flare the body toward a scaled copy of this END face - "
        "shrink a wing tip profile into a winglet taper. Scale, blend "
        "length and extend/pinch mode in the popup.");
    if (m_canEditDiameter &&
        ImGui::Button("Edit Diameter", ImVec2(-1, 30)))
        action = ToolAction::EditDiameter;
    tip("Resize a cylindrical hole / pin to an exact diameter.");
    if (m_canEditDiameter &&
        ImGui::Button("Thread", ImVec2(-1, 30)))
        action = ToolAction::Thread;
    tip("Cut a helical screw thread into the picked cylindrical face — "
        "external on a boss, internal in a hole. Pitch / depth / handedness "
        "in the popup.");

    // "Edit Fillet / Chamfer" appears only when the picked face was actually
    // produced by a fillet or chamfer op. We ask each Operation via
    // ownsFace() — the same hook the History panel uses elsewhere.
    if (m_selection && m_history) {
        TopoDS_Shape pickedFace;
        for (const auto& e : m_selection->getSelection()) {
            if (e.type == SelectionType::Face && !e.shape.IsNull()) {
                pickedFace = e.shape; break;
            }
        }
        if (!pickedFace.IsNull()) {
            const auto& ops = m_history->operations();
            for (const auto& op : ops) {
                if (op && op->isEnabled() && op->ownsFace(pickedFace)) {
                    const char* label = (op->typeId() == "fillet")
                                            ? "Edit Fillet"
                                            : (op->typeId() == "chamfer")
                                                  ? "Edit Chamfer"
                                                  : nullptr;
                    if (label && ImGui::Button(label, ImVec2(-1, 30)))
                        action = ToolAction::EditFilletChamfer;
                    tip(op->typeId() == "fillet"
                            ? "Change this fillet's radius without re-picking edges."
                            : "Change this chamfer's distance without re-picking edges.");
                    break;
                }
            }
        }
    }

    // Construction-plane / -axis creation from the selected face(s).
    renderAddPlaneMenu();
    renderAddAxisMenu();

    // Plugin buttons for HasFaces context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasFaces));

    return action;
}

ToolAction Toolbar::renderSketchSelectedTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Sketch");
    ImGui::Separator();
    ImGui::TextWrapped("Tip: hover a sketch region to highlight it, click to select, Ctrl+click to add to selection.");
    ImGui::Separator();

    if (ImGui::Button("Edit Sketch", ImVec2(-1, 30)))
        action = ToolAction::EditSketch;
    tip("Re-enter sketch mode to revise this sketch's geometry.");
    if (ImGui::Button("Extrude From", ImVec2(-1, 30)))
        action = ToolAction::ExtrudeSketch;
    tip("Make a new body by extruding the sketch's closed regions.");
    if (ImGui::Button("Subtract Sketch", ImVec2(-1, 30)))
        action = ToolAction::SubtractSketch;
    tip("Cut the extruded regions out of the body the sketch was drawn on.");
    ImGui::TextWrapped("Subtract cuts the extruded profile into the body the "
                       "sketch was drawn on (preview shown in red).");

    // Move / Rotate gizmo modes — appear here so a selected sketch behaves
    // like a movable construction plane. Bodies have these in renderBodyTools;
    // sketches need their own entry point. The Transform header matches the
    // "Sketch" / "Loft" section-label convention so the toolbar reads as a
    // sequence of clearly-titled groups.
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Transform");
    ImGui::Separator();
    if (ImGui::Button("Move", ImVec2(-1, 30)))
        action = ToolAction::Move;
    tip("Show the Move gizmo on the selected sketch. Drag an axis to reposition "
        "the sketch in 3D - its geometry rides along, so this effectively turns "
        "the sketch into a movable construction plane. Only available outside "
        "ortho view and sketch-edit mode.");
    if (ImGui::Button("Rotate", ImVec2(-1, 30)))
        action = ToolAction::Rotate;
    tip("Show the Rotate gizmo on the selected sketch. Drag a ring to spin the "
        "sketch around its centroid.");

    // Plugin buttons for HasSketches context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasSketches));

    return action;
}

ToolAction Toolbar::renderPlaneSelectedTools() {
    ToolAction action = ToolAction::None;
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Construction Plane");
    ImGui::Separator();
    if (ImGui::Button("Sketch on this Plane", ImVec2(-1, 30)))
        action = ToolAction::SketchOnFace; // dispatched on Plane in handler
    tip("Start a new sketch lying on this construction plane — same workflow as "
        "Sketch on Face, just with the plane as the host.");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Transform");
    ImGui::Separator();
    if (ImGui::Button("Move", ImVec2(-1, 30)))   action = ToolAction::Move;
    tip("Show the Move gizmo on this plane. Drag an axis arrow to nudge it; "
        "the live readout pinned to the cursor shows the offset along the "
        "plane's own normal.");
    if (ImGui::Button("Rotate", ImVec2(-1, 30))) action = ToolAction::Rotate;
    tip("Show the Rotate gizmo. Drag a ring to spin the plane around its "
        "origin; snap is 5° increments when snap-to-grid is on.");

    // Midplane between two selected construction planes; axis from their
    // intersection.
    renderAddPlaneMenu();
    renderAddAxisMenu();
    return action;
}

ToolAction Toolbar::renderAxisSelectedTools() {
    ToolAction action = ToolAction::None;
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Construction Axis");
    ImGui::Separator();
    ImGui::TextWrapped("Axes are 1-D primitives — they'll feed Revolve and "
                       "future Pattern-Around-Axis ops. For now you can "
                       "move them; rotate isn't meaningful on a line.");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Transform");
    ImGui::Separator();
    if (ImGui::Button("Move", ImVec2(-1, 30))) action = ToolAction::Move;
    tip("Show the Move gizmo on this axis. Drag an arrow to translate "
        "the axis origin; the direction is preserved.");

    renderAddPlaneMenu();
    return action;
}

ToolAction Toolbar::renderSketchRegionTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Region");
    ImGui::Separator();
    int n = m_selection ? m_selection->selectedSketchRegionCount() : 0;
    ImGui::Text("%d region%s selected", n, n == 1 ? "" : "s");
    ImGui::Spacing();

    // Push/Pull routes through the app's interactive arrow gizmo (default 0,
    // drag to extrude/cut) — same as a body face.
    if (ImGui::Button("Push / Pull", ImVec2(-1, 30)))
        action = ToolAction::PushPull;
    tip("Drag the arrow to extrude this region into a body, or cut it into the parent.");

    // Subtract: cut this region out of the body the sketch sits on, with a red
    // preview of the removed volume. Disabled when the sketch has no source body.
    if (ImGui::Button("Subtract", ImVec2(-1, 30)))
        action = ToolAction::SubtractSketch;
    tip("Cut this region into the body the sketch was drawn on (preview in red).");

    // Any remaining HasSketchRegions plugin buttons.
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasSketchRegions));

    // Edit the sketch this region belongs to — re-enter sketch mode to revise it.
    if (ImGui::Button("Edit Sketch", ImVec2(-1, 30)))
        action = ToolAction::EditSketch;
    tip("Re-enter sketch mode to revise this region's parent sketch.");

    // Move / Rotate the region's PARENT sketch in 3D — same gizmo path as
    // the whole-sketch case. A region selection is just a finger pointing at
    // its sketch for these ops. Hidden in ortho view (gizmo's own rule) but
    // the buttons stay visible so the user understands the action exists.
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Transform");
    ImGui::Separator();
    if (ImGui::Button("Move", ImVec2(-1, 30)))
        action = ToolAction::Move;
    tip("Show the Move gizmo on the parent sketch. Drag an axis to reposition "
        "the sketch in 3D - geometry follows, so the sketch becomes a movable "
        "construction plane. Outside ortho view only.");
    if (ImGui::Button("Rotate", ImVec2(-1, 30)))
        action = ToolAction::Rotate;
    tip("Show the Rotate gizmo on the parent sketch. Drag a ring to spin the "
        "sketch around its centroid.");

    ImGui::Spacing();
    ImGui::TextWrapped("Drag positive distance to extrude, negative to cut into the body the sketch sits on.");

    return action;
}

ToolAction Toolbar::renderEdgeTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Edge Ops");
    ImGui::Separator();
    if (ImGui::Button("Fillet", ImVec2(-1, 30)))  action = ToolAction::Fillet;
    tip("Round the picked edge(s). Set radius in the popup.");
    if (ImGui::Button("Chamfer", ImVec2(-1, 30))) action = ToolAction::Chamfer;
    tip("Bevel the picked edge(s). Set distance in the popup.");
    if (m_canEditDiameter &&
        ImGui::Button("Edit Diameter", ImVec2(-1, 30)))
        action = ToolAction::EditDiameter;
    tip("Resize the cylindrical face this edge belongs to.");

    // Construction plane / axis from this edge.
    renderAddPlaneMenu();
    renderAddAxisMenu();

    // Plugin buttons for HasEdges context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasEdges));

    return action;
}

} // namespace materializr
