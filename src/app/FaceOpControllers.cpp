#include "ui/StepperRow.h"
#include "FaceOpControllers.h"
#include "UserAxes.h"
#include "../core/Document.h"
#include "../core/SelectionManager.h"
#include "../core/NumParse.h"
#include "../ui/TouchWidgets.h" // im-touch number-pad amount fields
#include "../modeling/ShellOp.h"
#include "../modeling/TaperOp.h"
#include "../modeling/ScaleFaceOp.h"
#include "../modeling/ProjectSketchOp.h"
#include "../modeling/DefeatureOp.h"
#include "../modeling/ResizeCylindricalOp.h"
#include "../modeling/MoveFaceOp.h"
#include "../core/History.h"
#include "../modeling/Sketch.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <imgui.h>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

namespace materializr {

namespace {
// True if `face` shares an edge with a rounded (cylinder/torus = fillet) face of
// `body`. That's the exact condition OCCT's offset can't open, so it's what the
// Shell warning should key on — NOT merely "the body has fillets somewhere"
// (which mis-blamed fillets on a plain side face that failed for another reason).
bool faceBordersRounded(const TopoDS_Shape& body, const TopoDS_Face& face) {
    if (body.IsNull() || face.IsNull()) return false;
    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
    for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
        int idx = edgeToFaces.FindIndex(ex.Current());
        if (idx == 0) continue;
        for (const TopoDS_Shape& nb : edgeToFaces.FindFromIndex(idx)) {
            if (nb.IsSame(face)) continue;
            BRepAdaptor_Surface sa(TopoDS::Face(nb));
            if (sa.GetType() == GeomAbs_Cylinder || sa.GetType() == GeomAbs_Torus)
                return true;
        }
    }
    return false;
}
} // namespace

// ─── Shell ───────────────────────────────────────────────────────────────────

int ShellController::onBegin(const IopContext& ctx) {
    for (const auto& e : ctx.selection.getSelection()) {
        if (e.type == SelectionType::Face && e.bodyId >= 0 &&
            !e.shape.IsNull()) {
            m_face = TopoDS::Face(e.shape);
            m_thickness = 1.0f;
            std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.2f",
                          m_thickness);
            m_inputFocus = true;
            return e.bodyId;
        }
    }
    return -1;
}

std::unique_ptr<Operation> ShellController::buildOp(const IopContext&) {
    if (m_thickness <= 0.0f) return nullptr;
    auto op = std::make_unique<ShellOp>();
    op->setBody(bodyId());
    op->setThickness(static_cast<double>(m_thickness));
    op->addFaceToRemove(m_face);
    return op;
}

void ShellController::panelBody(const IopContext& ctx, bool& changed) {
    ImGui::TextDisabled("Hollows the body, opening a face.");

    if (ctx.cornerCommitUi) {
        // im-touch: number-pad amount field — no InputText, no native
        // keyboard (which froze the app on iOS).
        if (touchui::amountField("shellAmt", nullptr, &m_thickness, "mm", 2,
                                 /*allowSign=*/false, 0.1f, 20.0f)) {
            std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.2f", m_thickness);
            changed = true;
        }
    } else {
    if (m_inputFocus) {
        ImGui::SetKeyboardFocusHere();
        m_inputFocus = false;
    }
    ImGui::SetNextItemWidth(140);
    // parseFinite: non-finite input keeps the previous thickness rather
    // than feeding inf into MakeThickSolid.
    if (ImGui::InputText("##shellThickness", m_inputBuf, sizeof(m_inputBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue |
                         ImGuiInputTextFlags_CharsDecimal)) {
        (void)materializr::parseFinite(m_inputBuf, m_thickness);
        requestCommit();
    } else {
        float parsed = m_thickness;
        if (materializr::parseFinite(m_inputBuf, parsed) &&
            std::abs(parsed - m_thickness) > 0.001f) {
            m_thickness = parsed;
            changed = true;
        }
    }
    ImGui::SameLine();
    ImGui::Text("mm");
    }

    if (materializr::stepperRow("shellStep", &m_thickness,
                                /*allowNegative=*/false, 0.1f, 20.0f)) {
        // Snap to 0.1 mm — wall thicknesses are almost always in tenths, and a
        // free-floating 3.47 mm slider value is just noise.
        m_thickness = std::round(m_thickness * 10.0f) / 10.0f;
        std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.2f", m_thickness);
        changed = true;
    }

    if (!previewOk()) {
        const ImVec4 warn(1.0f, 0.6f, 0.3f, 1.0f);
        // Only blame fillets when THIS face actually borders one — OCCT can't
        // open a fillet-bordered face (it seals the cavity), and no thickness
        // fixes it; the answer is order-of-operations: shell first, fillet after.
        // A plain side face that failed for another reason gets the generic hint.
        const TopoDS_Shape& body = ctx.doc.getBody(bodyId());
        if (faceBordersRounded(body, m_face)) {
            ImGui::TextColored(warn,
                "Shell failed: OCCT can't open a fillet-bordered face.\n"
                "Shell the body FIRST, then add the fillets.");
        } else {
            ImGui::TextColored(warn,
                "Shell failed - try a thinner wall, or\n"
                "this body's faces can't be shelled.");
        }
    }
}

void ShellController::onCleanup() {
    m_face.Nullify();
}

// ─── Taper ───────────────────────────────────────────────────────────────────

int TaperController::onBegin(const IopContext& ctx) {
    // Collect every selected face on ONE body — multi-select all four
    // sides of a box to pyramid it in one go.
    m_faces.clear();
    int body = -1;
    for (const auto& e : ctx.selection.getSelection()) {
        if (e.type != SelectionType::Face || e.bodyId < 0 ||
            e.shape.IsNull())
            continue;
        if (body < 0) body = e.bodyId;
        if (e.bodyId != body) continue; // one body per op
        m_faces.push_back(TopoDS::Face(e.shape));
    }
    if (m_faces.empty()) return -1;
    m_angle = 10.0f;
    m_axisIdx = 0;
    m_flipBase = false;
    return body;
}

bool TaperController::resolveFrame(const IopContext& ctx, glm::vec3& dirOut,
                                   glm::vec3& neutralOut) const {
    if (bodyId() < 0 || m_faces.empty()) return false;

    // Pull direction. Auto: a cylindrical/conical face drafts along its own
    // axis; a planar face drafts along the world axis most PERPENDICULAR to
    // its normal (preferring up). Manual: the user-convention X/Y/Z radios.
    glm::vec3 dir(0.0f, 1.0f, 0.0f);
    if (m_axisIdx == 0) {
        try {
            const TopoDS_Face& f = m_faces.front();
            Handle(Geom_Surface) s = BRep_Tool::Surface(f);
            Handle(Geom_CylindricalSurface) cyl =
                Handle(Geom_CylindricalSurface)::DownCast(s);
            Handle(Geom_ConicalSurface) cone =
                Handle(Geom_ConicalSurface)::DownCast(s);
            if (!cyl.IsNull() || !cone.IsNull()) {
                gp_Dir a = !cyl.IsNull()
                               ? cyl->Cylinder().Position().Direction()
                               : cone->Cone().Position().Direction();
                dir = glm::vec3(static_cast<float>(a.X()),
                                static_cast<float>(a.Y()),
                                static_cast<float>(a.Z()));
            } else {
                BRepGProp_Face prop(f);
                double u1, u2, v1, v2;
                prop.Bounds(u1, u2, v1, v2);
                gp_Pnt c;
                gp_Vec nv;
                prop.Normal(0.5 * (u1 + u2), 0.5 * (v1 + v2), c, nv);
                glm::vec3 n(static_cast<float>(nv.X()),
                            static_cast<float>(nv.Y()),
                            static_cast<float>(nv.Z()));
                if (glm::length(n) > 1e-6f) n = glm::normalize(n);
                const glm::vec3 axes[3] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}};
                float best = 2.0f;
                for (const auto& a : axes) {
                    float d = std::abs(glm::dot(n, a));
                    if (d < best - 1e-4f) { best = d; dir = a; }
                }
            }
        } catch (...) {}
    } else {
        dir = userAxisToWorldVec(m_axisIdx - 1);
    }
    if (glm::length(dir) < 1e-6f) return false;
    dir = glm::normalize(dir);

    // Neutral plane: perpendicular to the pull direction, through the
    // body's extreme along it — the BASE stays fixed and the far end
    // tilts. Flip moves the fixed plane to the other extreme.
    try {
        Bnd_Box bb;
        BRepBndLib::Add(ctx.doc.getBody(bodyId()), bb);
        if (bb.IsVoid()) return false;
        double x0, y0, z0, x1, y1, z1;
        bb.Get(x0, y0, z0, x1, y1, z1);
        glm::vec3 corners[8] = {
            {(float)x0, (float)y0, (float)z0}, {(float)x1, (float)y0, (float)z0},
            {(float)x0, (float)y1, (float)z0}, {(float)x1, (float)y1, (float)z0},
            {(float)x0, (float)y0, (float)z1}, {(float)x1, (float)y0, (float)z1},
            {(float)x0, (float)y1, (float)z1}, {(float)x1, (float)y1, (float)z1}};
        float lo = 1e30f, hi = -1e30f;
        for (const auto& c : corners) {
            float p = glm::dot(c, dir);
            lo = std::min(lo, p);
            hi = std::max(hi, p);
        }
        glm::vec3 center(0.5f * (float)(x0 + x1), 0.5f * (float)(y0 + y1),
                         0.5f * (float)(z0 + z1));
        float proj = m_flipBase ? hi : lo;
        neutralOut = center + dir * (proj - glm::dot(center, dir));
        dirOut = dir;
        return true;
    } catch (...) { return false; }
}

std::unique_ptr<Operation> TaperController::buildOp(const IopContext& ctx) {
    if (std::abs(m_angle) < 0.1f) return nullptr;
    glm::vec3 dir, np;
    if (!resolveFrame(ctx, dir, np)) return nullptr;
    auto op = std::make_unique<TaperOp>();
    op->setBody(bodyId());
    for (const auto& f : m_faces) op->addFace(f);
    op->setDirection(dir.x, dir.y, dir.z);
    op->setNeutralPoint(np.x, np.y, np.z);
    op->setAngleDeg(static_cast<double>(m_angle));
    return op;
}

void TaperController::panelBody(const IopContext& ctx, bool& changed) {
    ImGui::TextDisabled("%zu face(s) tilt about the body's base.",
                        m_faces.size());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
    ImGui::TextDisabled("Tip: pick SIDE walls — a cylinder wall becomes a "
                        "cone, box sides become a pyramid.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    if (previewOk()) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
                           "Previewing %.1f deg", m_angle);
    } else if (std::abs(m_angle) < 0.1f) {
        // buildOp() short-circuits at ~0° so no preview is computed —
        // but the face is fine. Don't flash the "can't taper" warning
        // when the user is just sitting on the slider's zero stop.
        ImGui::TextDisabled("Move the angle slider to preview.");
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                           "No preview: this face can't taper along the "
                           "current Pull axis. Try another axis, Flip base, "
                           "or pick a side face.");
        ImGui::TextDisabled("Note: only flat / cylindrical / conical walls "
                            "can be drafted (a kernel limit) - for freeform "
                            "shapes like wing skins, use Scale Face on the "
                            "END face instead.");
        ImGui::PopTextWrapPos();
    }

    ImGui::TextDisabled("Angle: %.1f deg", m_angle);
    if (materializr::stepperRow("taperStep", &m_angle,
                                /*allowNegative=*/true, -45.0f, 45.0f))
        changed = true;
    if (ctx.cornerCommitUi &&
        touchui::amountField("taperAmt", nullptr, &m_angle, "deg", 1,
                             /*allowSign=*/true, -45.0f, 45.0f))
        changed = true;

    ImGui::Text("Pull axis");
    ImGui::SameLine();
    const char* axisNames[4] = {"Auto", "X", "Y", "Z"};
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::RadioButton(axisNames[i], m_axisIdx == i)) {
            m_axisIdx = i;
            changed = true;
        }
    }
    if (ImGui::Checkbox("Flip base (fixed end)", &m_flipBase))
        changed = true;
}

void TaperController::onCleanup() { m_faces.clear(); }

// ─── Remove Face (defeature) ─────────────────────────────────────────────────

int DefeatureController::onBegin(const IopContext& ctx) {
    // Gather every selected face on ONE body — multi-select a few faces to
    // remove them together.
    m_faces.clear();
    int body = -1;
    for (const auto& e : ctx.selection.getSelection()) {
        if (e.type != SelectionType::Face || e.bodyId < 0 || e.shape.IsNull())
            continue;
        if (body < 0) body = e.bodyId;
        if (e.bodyId != body) continue; // one body per op
        m_faces.push_back(TopoDS::Face(e.shape));
    }
    if (m_faces.empty()) return -1;
    return body;
}

std::unique_ptr<Operation> DefeatureController::buildOp(const IopContext&) {
    if (m_faces.empty()) return nullptr;
    auto op = std::make_unique<DefeatureOp>();
    op->setBody(bodyId());
    for (const auto& f : m_faces) op->addFace(f);
    return op;
}

void DefeatureController::panelBody(const IopContext&, bool&) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
    ImGui::TextDisabled("Removes the selected face(s) and heals the surrounding "
                        "faces back together — e.g. take a baked fillet back to "
                        "a sharp edge so you can re-fillet it.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    ImGui::Text("%zu face(s) selected", m_faces.size());

    if (previewOk()) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Previewing removal");
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                           "Can't remove: the neighbouring faces can't be "
                           "extended to close the gap. Try a different face — a "
                           "single fillet / round usually works.");
        ImGui::PopTextWrapPos();
    }
}

void DefeatureController::onCleanup() { m_faces.clear(); }

// ─── Project Sketch ──────────────────────────────────────────────────────────

int ProjectSketchController::onBegin(const IopContext& ctx) {
    m_face.Nullify();
    m_sketchIds.clear();
    m_regionFilter.clear();
    m_depth = 1.0f;
    m_mode = 0;

    int body = -1;
    int pickedSketch = -1;
    for (const auto& e : ctx.selection.getSelection()) {
        if (e.type == SelectionType::Face && e.bodyId >= 0 &&
            !e.shape.IsNull() && body < 0) {
            m_face = TopoDS::Face(e.shape);
            body = e.bodyId;
        }
        // Regions Ctrl+clicked beforehand narrow the projection to just
        // those; all of them must come from one sketch.
        if (e.type == SelectionType::SketchRegion && e.sketchId >= 0) {
            if (pickedSketch < 0) pickedSketch = e.sketchId;
            if (e.sketchId == pickedSketch)
                m_regionFilter.push_back(e.subShapeIndex);
        }
    }
    if (body < 0) return -1;

    m_sketchIds = ctx.doc.getAllSketchIds();
    if (m_sketchIds.empty()) {
        std::fprintf(stderr, "[ProjectSketch] no sketches in document\n");
        return -1;
    }
    // Default to the selection's sketch, else the newest one.
    m_sketchPick = static_cast<int>(m_sketchIds.size()) - 1;
    if (pickedSketch >= 0) {
        for (size_t i = 0; i < m_sketchIds.size(); ++i)
            if (m_sketchIds[i] == pickedSketch)
                m_sketchPick = static_cast<int>(i);
    }
    return body;
}

std::unique_ptr<Operation> ProjectSketchController::buildOp(
    const IopContext&) {
    if (m_face.IsNull() || m_sketchIds.empty() || m_depth < 0.01f)
        return nullptr;
    auto op = std::make_unique<ProjectSketchOp>();
    op->setBody(bodyId());
    op->setTargetFace(m_face);
    op->setSketchId(m_sketchIds[m_sketchPick]);
    op->setRegionFilter(m_regionFilter);
    op->setDepth(static_cast<double>(m_depth));
    op->setMode(m_mode == 1 ? ProjectSketchOp::Mode::Emboss
                            : ProjectSketchOp::Mode::Engrave);
    return op;
}

void ProjectSketchController::panelBody(const IopContext& ctx,
                                        bool& changed) {
    ImGui::TextDisabled("Projects the sketch onto this face along the\n"
                        "sketch's normal, then cuts in or raises out.");
    ImGui::TextWrapped("Click the sketch elements you want projected — click "
                       "each to add or remove. Use Select all / Clear below.");

    // Live region scoping: clicking sketch regions in the viewport while this
    // panel is open narrows the projection to just those (each click toggles —
    // no modifier needed while this step is active); clicking empty space goes
    // back to the whole sketch. A clicked region also drives the sketch choice,
    // so picking "the relevant sketch" is literally clicking it.
    {
        int selSketch = -1;
        std::vector<int> live;
        for (const auto& e : ctx.selection.getSelection()) {
            if (e.type != SelectionType::SketchRegion || e.sketchId < 0)
                continue;
            if (selSketch < 0) selSketch = e.sketchId;
            if (e.sketchId == selSketch)
                live.push_back(e.subShapeIndex);
        }
        if (selSketch >= 0 &&
            selSketch != m_sketchIds[m_sketchPick]) {
            for (size_t i = 0; i < m_sketchIds.size(); ++i) {
                if (m_sketchIds[i] == selSketch) {
                    m_sketchPick = static_cast<int>(i);
                    changed = true;
                }
            }
        }
        std::sort(live.begin(), live.end());
        std::vector<int> cur = m_regionFilter;
        std::sort(cur.begin(), cur.end());
        if (live != cur) {
            m_regionFilter = live;
            changed = true;
        }
    }

    std::string current =
        ctx.doc.getSketchName(m_sketchIds[m_sketchPick]);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##projSketch", current.c_str())) {
        for (size_t i = 0; i < m_sketchIds.size(); ++i) {
            ImGui::PushID(static_cast<int>(i)); // names may repeat
            bool sel = static_cast<int>(i) == m_sketchPick;
            std::string label = ctx.doc.getSketchName(m_sketchIds[i]);
            if (ImGui::Selectable(label.c_str(), sel)) {
                if (static_cast<int>(i) != m_sketchPick) {
                    m_sketchPick = static_cast<int>(i);
                    m_regionFilter.clear(); // filter was for the old sketch
                    changed = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    // Select all → then click the few you DON'T want to drop them (easier than
    // hand-picking every letter of a long inscription). Clear → back to none.
    if (ImGui::SmallButton("Select all")) {
        if (auto sk = ctx.doc.getSketch(m_sketchIds[m_sketchPick])) {
            const int sid = m_sketchIds[m_sketchPick];
            const int n = static_cast<int>(sk->buildRegions().size());
            for (int i = 0; i < n; ++i) {
                bool already = false;
                for (const auto& s : ctx.selection.getSelection())
                    if (s.type == SelectionType::SketchRegion &&
                        s.sketchId == sid && s.subShapeIndex == i) {
                        already = true;
                        break;
                    }
                if (already) continue;
                SelectionEntry e;
                e.type = SelectionType::SketchRegion;
                e.sketchId = sid;
                e.subShapeIndex = i;
                ctx.selection.toggleSelection(e); // adds (absent after the check)
            }
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        ctx.selection.clear();
        changed = true;
    }
    // Smart guess: auto-nesting of a dense logo is imperfect, so cycle the
    // selection through loops-only / islands-only / all. One press usually
    // lands close to what you want (loops-only = letters, counters hollow);
    // fix the stragglers by clicking. An "island" is a region whose interior
    // sits inside another region's solid (a counter that should be a hole).
    if (ImGui::SmallButton("Cycle loops/islands")) {
        if (auto sk = ctx.doc.getSketch(m_sketchIds[m_sketchPick])) {
            auto regions = sk->buildRegions();
            const int sid = m_sketchIds[m_sketchPick];
            std::vector<bool> island(regions.size(), false);
            for (size_t i = 0; i < regions.size(); ++i)
                for (size_t j = 0; j < regions.size(); ++j)
                    if (i != j && sk->isPointInRegion(
                                      regions[j], regions[i].representativePoint)) {
                        island[i] = true;
                        break;
                    }
            m_cycleMode = (m_cycleMode + 1) % 3; // press 1=loops, 2=islands, 3=all
            ctx.selection.clear();
            for (size_t i = 0; i < regions.size(); ++i) {
                const bool want = m_cycleMode == 0 ||
                                  (m_cycleMode == 1 && !island[i]) ||
                                  (m_cycleMode == 2 && island[i]);
                if (!want) continue;
                SelectionEntry e;
                e.type = SelectionType::SketchRegion;
                e.sketchId = sid;
                e.subShapeIndex = static_cast<int>(i);
                ctx.selection.toggleSelection(e);
            }
            changed = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", m_cycleMode == 0 ? "all"
                              : m_cycleMode == 1 ? "loops" : "islands");
    if (!m_regionFilter.empty()) {
        ImGui::TextDisabled("%d region(s) selected - click any to add or\n"
                            "remove. Use Clear to reset.",
                            static_cast<int>(m_regionFilter.size()));
    } else {
        ImGui::TextDisabled("All regions. Click elements to project only\n"
                            "those (click each to add or remove).");
    }

    if (!wantsLivePreview(ctx)) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                           "%d regions - live preview is off here.\n"
                           "Confirm to apply (may take a moment).",
                           effectiveRegionCount(ctx));
    }

    if (ImGui::RadioButton("Engrave", &m_mode, 0)) changed = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Emboss", &m_mode, 1)) changed = true;

    ImGui::TextDisabled("Depth: %.2f mm", m_depth);
    if (materializr::stepperRow("projDepthStep", &m_depth,
                                /*allowNegative=*/false, 0.1f, 10.0f)) {
        changed = true;
    }
    if (ctx.cornerCommitUi &&
        touchui::amountField("projAmt", nullptr, &m_depth, "mm", 2,
                             /*allowSign=*/false, 0.1f, 10.0f))
        changed = true;

    if (!previewOk()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                           "Projection failed - the selected region(s) couldn't\n"
                           "be applied (off the face, or too thin/degenerate).");
    }
}

void ProjectSketchController::onCleanup() {
    m_face.Nullify();
    m_sketchIds.clear();
    m_regionFilter.clear();
}

int ProjectSketchController::effectiveRegionCount(const IopContext& ctx) const {
    if (m_sketchIds.empty()) return 0;
    if (!m_regionFilter.empty()) return static_cast<int>(m_regionFilter.size());
    if (auto sk = ctx.doc.getSketch(m_sketchIds[m_sketchPick]))
        return static_cast<int>(sk->buildRegions().size());
    return 0;
}

bool ProjectSketchController::wantsLivePreview(const IopContext& ctx) const {
    return effectiveRegionCount(ctx) <= kPreviewRegionCap;
}

// ─── Scale Face ──────────────────────────────────────────────────────────────

int ScaleFaceController::onBegin(const IopContext& ctx) {
    int body = -1;
    m_face.Nullify();
    for (const auto& e : ctx.selection.getSelection()) {
        if (e.type == SelectionType::Face && e.bodyId >= 0 &&
            !e.shape.IsNull()) {
            m_face = TopoDS::Face(e.shape);
            body = e.bodyId;
            break;
        }
    }
    if (body < 0 || m_face.IsNull()) return -1;

    m_pctU = m_pctV = 30.0f;
    m_uniform = true;
    m_dragAxis = -1;
    m_len = 10.0f;
    m_lenMax = 100.0f;
    try {
        TopoDS_Shape bodyShape = ctx.doc.getBody(body);

        BRepGProp_Face gpf(m_face);
        double u1, u2, v1, v2;
        gpf.Bounds(u1, u2, v1, v2);
        gp_Pnt onFace;
        gp_Vec nv;
        gpf.Normal(0.5 * (u1 + u2), 0.5 * (v1 + v2), onFace, nv);
        Bnd_Box bb;
        BRepBndLib::Add(bodyShape, bb);
        if (nv.Magnitude() > 1e-9 && !bb.IsVoid()) {
            gp_Dir n(nv);
            double x0, y0, z0, x1, y1, z1;
            bb.Get(x0, y0, z0, x1, y1, z1);
            gp_Pnt corners[8] = {
                gp_Pnt(x0, y0, z0), gp_Pnt(x1, y0, z0),
                gp_Pnt(x0, y1, z0), gp_Pnt(x1, y1, z0),
                gp_Pnt(x0, y0, z1), gp_Pnt(x1, y0, z1),
                gp_Pnt(x0, y1, z1), gp_Pnt(x1, y1, z1)};
            double depth = 0.0;
            for (const auto& c : corners) {
                double d = gp_Vec(c, onFace).Dot(gp_Vec(n));
                depth = std::max(depth, d);
            }
            if (depth > 1e-3) {
                // Default = the FULL depth of the body behind the face, so
                // scaling a box top re-slopes the sides from the BASE.
                m_lenMax = static_cast<float>(depth);
                m_len = m_lenMax;
            }
        }
        // Gizmo frame: the face plane's own axes + the face's half-extents
        // along them. COPY the plane — Pln() returns a temporary, and a
        // reference into it dangles (the red-line-to-infinity bug).
        Handle(Geom_Plane) gpl =
            Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(m_face));
        if (!gpl.IsNull()) {
            const gp_Pln fpln = gpl->Pln();
            const gp_Ax3& fax = fpln.Position();
            gp_Dir ud = fax.XDirection(), vd2 = fax.YDirection();
            m_axisU = glm::vec3((float)ud.X(), (float)ud.Y(), (float)ud.Z());
            m_axisV = glm::vec3((float)vd2.X(), (float)vd2.Y(),
                                (float)vd2.Z());
            GProp_GProps fpr;
            BRepGProp::SurfaceProperties(m_face, fpr);
            gp_Pnt fc = fpr.CentreOfMass();
            m_center = glm::vec3((float)fc.X(), (float)fc.Y(), (float)fc.Z());
            Bnd_Box fbb;
            BRepBndLib::Add(m_face, fbb);
            if (!fbb.IsVoid()) {
                double fx0, fy0, fz0, fx1, fy1, fz1;
                fbb.Get(fx0, fy0, fz0, fx1, fy1, fz1);
                gp_Pnt fcs[8] = {
                    gp_Pnt(fx0, fy0, fz0), gp_Pnt(fx1, fy0, fz0),
                    gp_Pnt(fx0, fy1, fz0), gp_Pnt(fx1, fy1, fz0),
                    gp_Pnt(fx0, fy0, fz1), gp_Pnt(fx1, fy0, fz1),
                    gp_Pnt(fx0, fy1, fz1), gp_Pnt(fx1, fy1, fz1)};
                float hu = 1.0f, hv = 1.0f;
                for (const auto& cpt : fcs) {
                    glm::vec3 d((float)cpt.X() - m_center.x,
                                (float)cpt.Y() - m_center.y,
                                (float)cpt.Z() - m_center.z);
                    hu = std::max(hu, std::abs(glm::dot(d, m_axisU)));
                    hv = std::max(hv, std::abs(glm::dot(d, m_axisV)));
                }
                m_halfU = hu;
                m_halfV = hv;
            }
        }
    } catch (...) {}
    return body;
}

std::unique_ptr<Operation> ScaleFaceController::buildOp(const IopContext&) {
    auto op = std::make_unique<ScaleFaceOp>();
    op->setBody(bodyId());
    op->setFace(m_face);
    op->setScaleUV(static_cast<double>(m_pctU), static_cast<double>(m_pctV));
    op->setLength(static_cast<double>(m_len));
    // Always Pinch — it re-slopes the EXISTING walls and, since the >100%
    // union landed, does it in both directions. The old Extend/Pinch radio
    // asked the user to pick a boolean before they knew what either did, and
    // Extend answered a different question anyway (bolt a new tapered section
    // on top). The op keeps both modes so old projects replay unchanged.
    op->setMode(ScaleFaceOp::Mode::Pinch);
    return op;
}

void ScaleFaceController::applyHandleDrag(int axis, float dPct,
                                          const IopContext& ctx) {
    float& pct = (axis == 0) ? m_pctU : m_pctV;
    pct = std::min(maxPct(), std::max(5.0f, pct + dPct));
    if (m_uniform) {
        m_pctU = pct;
        m_pctV = pct;
    }
    update(ctx);
}

// Hit-test + drag, moved here from Application_Viewport verbatim in behaviour.
// Click anywhere along an arrow SHAFT (centre → tip), not just a disc at the
// tip: the old 16-px tip target made the visible arrow look clickable when it
// wasn't (Steve: "the gizmo is not clickable").
void ScaleFaceController::onViewportInput(const IopViewport& vp,
                                          const IopContext& ctx) {
    if (vp.clicked && m_dragAxis < 0) {
        const glm::vec3 tipU = m_center + m_axisU * (m_halfU * m_pctU / 100.0f);
        const glm::vec3 tipV = m_center + m_axisV * (m_halfV * m_pctV / 100.0f);
        glm::vec2 cs, tu, tv;
        const bool gotC = vp.toScreen(m_center, cs);
        const bool gotU = vp.toScreen(tipU, tu);
        const bool gotV = vp.toScreen(tipV, tv);
        auto distToSeg = [&](glm::vec2 a, glm::vec2 b) {
            const glm::vec2 d = b - a;
            const float len2 = glm::dot(d, d);
            glm::vec2 q;
            if (len2 < 1e-6f) {
                q = vp.mouse - a;
            } else {
                float t = glm::dot(vp.mouse - a, d) / len2;
                t = std::max(0.0f, std::min(1.0f, t));
                q = vp.mouse - (a + t * d);
            }
            return std::sqrt(glm::dot(q, q));
        };
        const float du = (gotC && gotU) ? distToSeg(cs, tu) : 1e9f;
        const float dv = (gotC && gotV) ? distToSeg(cs, tv) : 1e9f;
        const float pick = 12.0f; // generous, matches trackpad feel
        if      (du < pick && du <= dv) m_dragAxis = 0;
        else if (dv < pick)             m_dragAxis = 1;
        setDraggingHandle(m_dragAxis >= 0);
    }

    if (m_dragAxis >= 0 && vp.dragging) {
        const glm::vec3 axis = (m_dragAxis == 0) ? m_axisU : m_axisV;
        const float half     = (m_dragAxis == 0) ? m_halfU : m_halfV;
        const float dW = vp.dragAlongAxis(m_center, axis, vp.mouseDelta);
        applyHandleDrag(m_dragAxis, dW / std::max(half, 1e-3f) * 100.0f, ctx);
    }

    if (vp.released) {
        m_dragAxis = -1;
        setDraggingHandle(false);
    }
}

void ScaleFaceController::drawOverlay(const IopOverlay& ov) const {
    auto handle = [&](const glm::vec3& axis, float halfExt, float pct,
                      unsigned col) {
        const glm::vec3 tipW = m_center + axis * (halfExt * pct / 100.0f);
        glm::vec2 a, b;
        if (!ov.toScreen(m_center, a) || !ov.toScreen(tipW, b)) return;
        ov.line(a, b, col, 3.0f);
        ov.disc(b, 7.0f, col);
        char hl[16];
        std::snprintf(hl, sizeof(hl), "%.0f%%", pct);
        ov.label(b, hl, col);
    };
    handle(m_axisU, m_halfU, m_pctU, 0xFF5A5AEBu); // red   (0xAABBGGRR)
    handle(m_axisV, m_halfV, m_pctV, 0xFFEB965Au); // blue
}

void ScaleFaceController::panelBody(const IopContext& ctx, bool& changed) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
    ImGui::TextDisabled("Scale this face; the side walls re-slope to follow. "
                        "Under 100%% shrinks it, over 100%% grows it. Full "
                        "length = walls follow from the base; shorter = blend "
                        "only near the face.");
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    if (previewOk()) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
                           "Previewing %.0f%% x %.0f%% over %.1f mm",
                           m_pctU, m_pctV, m_len);
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                           "No preview: needs a FLAT end face (and 100%% is "
                           "a no-op). Try another face or tweak values.");
        ImGui::PopTextWrapPos();
    }

    if (ImGui::Checkbox("Uniform", &m_uniform) && m_uniform) {
        m_pctV = m_pctU;
        changed = true;
    }
    if (m_uniform) {
        ImGui::TextDisabled("Scale: %.0f %%", m_pctU);
        if (materializr::stepperRow("scaleStep", &m_pctU,
                                    /*allowNegative=*/true, 5.0f, maxPct(),
                                    /*zeroValue=*/100.0f)) {
            m_pctV = m_pctU;
            changed = true;
        }
        if (ctx.cornerCommitUi &&
            touchui::amountField("scaleAmt", nullptr, &m_pctU, "%", 0,
                                 /*allowSign=*/false, 5.0f, maxPct())) {
            m_pctV = m_pctU;
            changed = true;
        }
    } else {
        // Each slider's text label is shown ABOVE the bar in the colour of
        // the matching face-gizmo arrow, with a hidden "##" slider label.
        // (Steve: "U" and "V" mean nothing here, and the narrow panel was
        //  truncating "Scale U" / "Scale V" to just "S" at the right edge.)
        const ImVec4 redCol (0.92f, 0.35f, 0.35f, 1.0f); // matches the red arrow
        const ImVec4 blueCol(0.35f, 0.59f, 0.92f, 1.0f); // matches the blue arrow
        ImGui::TextColored(redCol, "Red line");
        ImGui::SameLine(); ImGui::TextDisabled("%.0f %%", m_pctU);
        if (materializr::stepperRow("scaleUStep", &m_pctU,
                                    /*allowNegative=*/true, 5.0f, maxPct(),
                                    /*zeroValue=*/100.0f))
            changed = true;
        if (ctx.cornerCommitUi &&
            touchui::amountField("scaleUAmt", nullptr, &m_pctU, "%", 0,
                                 /*allowSign=*/false, 5.0f, maxPct()))
            changed = true;
        ImGui::TextColored(blueCol, "Blue line");
        ImGui::SameLine(); ImGui::TextDisabled("%.0f %%", m_pctV);
        if (materializr::stepperRow("scaleVStep", &m_pctV,
                                    /*allowNegative=*/true, 5.0f, maxPct(),
                                    /*zeroValue=*/100.0f))
            changed = true;
        if (ctx.cornerCommitUi &&
            touchui::amountField("scaleVAmt", nullptr, &m_pctV, "%", 0,
                                 /*allowSign=*/false, 5.0f, maxPct()))
            changed = true;
    }
    ImGui::TextDisabled("Or drag the two arrows on the face.");
    ImGui::TextDisabled("Length: %.1f mm", m_len);
    if (materializr::stepperRow("lenStep", &m_len,
                                /*allowNegative=*/false, 0.5f,
                                std::max(m_lenMax, 1.0f)))
        changed = true;
    if (ctx.cornerCommitUi &&
        touchui::amountField("lenAmt", nullptr, &m_len, "mm", 1,
                             /*allowSign=*/false, 0.5f, std::max(m_lenMax, 1.0f)))
        changed = true;
}

void ScaleFaceController::onCleanup() {
    m_face.Nullify();
    m_dragAxis = -1;
}

// ─── Resize Cylindrical (Edit Diameter) ──────────────────────────────────────
// Was ~17 members on Application plus begin/update/commit/cancel and a
// hand-rolled panel in Application_Dialogs. The base already models all of it:
// the snapshot, the live preview, Confirm/Cancel/Enter/Esc, and — via
// wantsLivePreview — the threaded-body case that has to skip the preview.

int ResizeCylindricalController::onBegin(const IopContext& ctx) {
    // Resolve our own target rather than being handed one. detectCylindricalPick
    // needs only the document and the selection, both of which are right here.
    m_pick = detectCylindricalPick(ctx.doc, ctx.selection);
    if (!m_pick.ok || m_pick.bodyId < 0) return -1;

    m_deferred = ctx.history.isBodyThreaded(m_pick.bodyId);
    m_newBottomDiameter = m_pick.bottomR * 2.0;
    m_newTopDiameter    = m_pick.topR    * 2.0;
    std::snprintf(m_botBuf, sizeof(m_botBuf), "%.2f", m_newBottomDiameter);
    std::snprintf(m_topBuf, sizeof(m_topBuf), "%.2f", m_newTopDiameter);
    m_inputFocus = true;
    return m_pick.bodyId;
}

bool ResizeCylindricalController::wantsLivePreview(const IopContext&) const {
    return !m_deferred;
}

std::unique_ptr<Operation> ResizeCylindricalController::buildOp(
    const IopContext&) {
    const double newBot = m_pick.editBottom ? m_newBottomDiameter * 0.5
                                            : m_pick.bottomR;
    const double newTop = m_pick.editTop    ? m_newTopDiameter    * 0.5
                                            : m_pick.topR;
    // Degenerate or unchanged: no op. The base treats a null op as "nothing to
    // push" and cleans up, which is what the old commit's cancel() branch did.
    const bool unchanged = std::abs(newBot - m_pick.bottomR) < 1e-5 &&
                           std::abs(newTop - m_pick.topR)    < 1e-5;
    if (newBot < 1e-4 || newTop < 1e-4 || unchanged) return nullptr;

    auto op = std::make_unique<ResizeCylindricalOp>();
    op->setBody(bodyId());
    op->setAxis(m_pick.axis);
    op->setHeight(m_pick.height);
    op->setOldRadii(m_pick.bottomR, m_pick.topR);
    op->setNewRadii(newBot, newTop);
    op->setIsHole(m_pick.isHole);
    return op;
}

void ResizeCylindricalController::panelBody(const IopContext& ctx,
                                            bool& changed) {
    // The base already titles the panel "Edit Diameter"; this line carries the
    // part that varies — which end, and whether it's a hole or an outer face.
    const bool bothEnds = both();
    const char* what = bothEnds       ? "Both ends"
                     : m_pick.editBottom ? "Bottom end"
                                         : "Top end";
    ImGui::TextDisabled("%s \xE2\x80\x94 %s", what,
                        m_pick.isHole ? "hole" : "outer face");

    if (bothEnds) {
        ImGui::Text("Original: %.2f mm", m_pick.topR * 2.0);
    } else if (m_pick.editBottom) {
        ImGui::Text("Original: %.2f mm", m_pick.bottomR * 2.0);
        ImGui::TextDisabled("Top stays at %.2f mm — drag this end to make a cone.",
                            m_pick.topR * 2.0);
    } else {
        ImGui::Text("Original: %.2f mm", m_pick.topR * 2.0);
        ImGui::TextDisabled("Bottom stays at %.2f mm — drag this end to make a cone.",
                            m_pick.bottomR * 2.0);
    }

    if (m_inputFocus) {
        ImGui::SetKeyboardFocusHere();
        m_inputFocus = false;
    }

    // Drive one buffer; mirror into the other when face-editing both ends.
    char*   buf = m_pick.editBottom ? m_botBuf : m_topBuf;
    double* val = m_pick.editBottom ? &m_newBottomDiameter : &m_newTopDiameter;

    double parsed = *val;
    bool edited = false;
    if (ctx.cornerCommitUi) {
        // im-touch: number-pad amount field — no InputText, no native keyboard
        // (which froze the app on iOS).
        double v = *val;
        if (touchui::amountField("rcylAmt", nullptr, &v, "mm", 2,
                                 /*allowSign=*/false)) {
            parsed = v;
            edited = std::abs(parsed - *val) > 0.001;
            std::snprintf(buf, 32, "%.2f", v);
        }
    } else {
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputText("##rcyldia", buf, 32,
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_CharsDecimal))
            requestCommit();   // Enter in the field = Confirm
        // parseFinite: garbage/inf keeps the previous value.
        edited = materializr::parseFinite(buf, parsed) &&
                 std::abs(parsed - *val) > 0.001;
        ImGui::SameLine();
        ImGui::Text("mm");
    }
    if (edited) {
        *val = parsed;
        if (bothEnds) {
            m_newBottomDiameter = parsed;
            m_newTopDiameter    = parsed;
            std::snprintf(m_pick.editBottom ? m_topBuf : m_botBuf, 32, "%.2f",
                          parsed);
        }
        changed = true;
    }

    // Only complain once the user has actually asked for a different size.
    // buildOp returns nullptr for "unchanged", which the base reports as a
    // failed preview — so at the untouched original this warned about an
    // invalid diameter before anything had been typed.
    if (!previewOk() && !m_deferred && changedFromOriginal()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.35f, 1.0f),
                           "Invalid diameter for this feature —\n"
                           "a hole can't exceed the surrounding wall.");
    }
    if (m_deferred) {
        ImGui::TextDisabled("Threaded body — applies on OK,\n"
                            "then the thread re-cuts in background.");
    }
}

void ResizeCylindricalController::onCleanup() {
    m_pick = CylindricalPick{};
    m_deferred = false;
    m_inputFocus = true;
}

// ─── Move Face ───────────────────────────────────────────────────────────────
// Slice 2: the gesture maths moves first — these four read nothing but the
// state, so they port with a rename and no behaviour change. The lifecycle
// (begin/update/commit) still runs on Application and calls back through
// the accessors until slice 3.

bool MoveFaceController::faceXformNontrivial() const {
    switch (m_st.faceXformKind) {
        case FaceXform::Translate: return glm::length(m_st.moveFaceVec) > 1e-4f;
        case FaceXform::Rotate:
            return m_st.moveFaceIsTwist
                ? std::abs(m_st.moveFaceTwist) > 1e-4f
                : (std::abs(m_st.moveFaceAngle) > 1e-4f || m_st.moveFaceRotHasAccum);
        case FaceXform::Scale:
            return m_st.moveFaceScaleUniform
                ? std::abs(m_st.moveFaceScale - 1.0f) > 1e-4f
                : (std::abs(m_st.moveFaceScaleA - 1.0f) > 1e-4f ||
                   std::abs(m_st.moveFaceScaleB - 1.0f) > 1e-4f);
    }
    return false;
}

glm::mat3 MoveFaceController::faceRotTotal() const {
    return rodrigues(m_st.moveFaceRotAxis, m_st.moveFaceAngle) * m_st.moveFaceRotAccum;
}

void MoveFaceController::bakeFaceRotationDrag() {
    // Twist isn't a tilt-matrix accumulation — nothing to bake for it.
    if (m_st.moveFaceIsTwist) return;
    if (m_st.faceXformKind != FaceXform::Rotate || std::abs(m_st.moveFaceAngle) < 1e-5f)
        return;
    m_st.moveFaceRotAccum = rodrigues(m_st.moveFaceRotAxis, m_st.moveFaceAngle) * m_st.moveFaceRotAccum;
    m_st.moveFaceRotHasAccum = true;
    m_st.moveFaceAngle = 0.0f;
    m_st.moveFaceAngleBase = 0.0f;
}

void MoveFaceController::configureFaceOp(MoveFaceOp& op) const {
    switch (m_st.faceXformKind) {
        case FaceXform::Translate:
            op.setKind(MoveFaceOp::Kind::Translate);
            op.setMoveVector(gp_Vec(m_st.moveFaceVec.x, m_st.moveFaceVec.y, m_st.moveFaceVec.z));
            break;
        case FaceXform::Rotate: {
            if (m_st.moveFaceIsTwist) { // third ring = twist about the normal
                op.setKind(MoveFaceOp::Kind::Twist);
                op.setTwist(m_st.moveFaceTwist);
                break;
            }
            op.setKind(MoveFaceOp::Kind::Rotate);
            // Composed rotation (live drag ∘ accumulated tilts) as a gp_Trsf
            // about the pivot, so stacked tilts about both axes apply at once.
            glm::mat3 R = faceRotTotal();
            glm::vec3 Tt = m_st.moveFacePivot - R * m_st.moveFacePivot;
            gp_Trsf trsf;
            trsf.SetValues(R[0][0], R[1][0], R[2][0], Tt.x,
                           R[0][1], R[1][1], R[2][1], Tt.y,
                           R[0][2], R[1][2], R[2][2], Tt.z);
            op.setRotationExplicit(trsf);
            break;
        }
        case FaceXform::Scale:
            op.setKind(MoveFaceOp::Kind::Scale);
            if (m_st.moveFaceScaleUniform) {
                op.setScaleFactor(m_st.moveFaceScale);
            } else {
                op.setScaleNonUniform(
                    gp_Dir(m_st.moveFaceAxisA.x, m_st.moveFaceAxisA.y, m_st.moveFaceAxisA.z),
                    gp_Dir(m_st.moveFaceAxisB.x, m_st.moveFaceAxisB.y, m_st.moveFaceAxisB.z),
                    m_st.moveFaceScaleA, m_st.moveFaceScaleB);
            }
            break;
    }
    op.setLoopMotion(m_st.moveFaceMoveOuter, m_st.moveFaceHoleSlant, m_st.moveFaceHoleVertical);
}

} // namespace materializr
