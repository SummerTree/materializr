#include "ExtrudeController.h"
#include "../core/Document.h"
#include "../core/NumParse.h"
#include "../ui/UiTheme.h"       // viewportBanner
#include "../ui/NumField.h"      // btnConfirm / btnCancel
#include "../ui/StepperRow.h"
#include "../ui/TouchWidgets.h"  // im-touch number-pad amount field
#include "../ui/OpDialogGrip.h"
#include "../touch_mode.h"
#include <imgui.h>
#include <BRep_Tool.hxx>
#include <BRepGProp_Face.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <cstdio>

namespace materializr {

double ExtrudeController::opDistance() const {
    return (m_mode == ExtrudeMode::Subtract)
        ? -static_cast<double>(m_distance)
        : static_cast<double>(m_distance);
}

bool ExtrudeController::beginExtrude(const IopContext& ctx,
                                     const TopoDS_Shape& profile,
                                     ExtrudeMode mode, int targetBody,
                                     int sourceSketchId) {
    // Extrude sweeps a profile along its normal — only meaningful for a FLAT
    // profile. A single curved body face (cylinder / sphere / fillet) has no
    // single normal, so extruding it produced garbage geometry; refuse with
    // guidance instead (mirrors Sketch-on-Face). Checked BEFORE anything is
    // disturbed so a bad attempt leaves an in-progress op alone. Sketch
    // profiles are planar by construction; wire / compound profiles aren't a
    // single face and skip this check.
    if (profile.ShapeType() == TopAbs_FACE) {
        Handle(Geom_Surface) s = BRep_Tool::Surface(TopoDS::Face(profile));
        if (s.IsNull() || !s->IsKind(STANDARD_TYPE(Geom_Plane))) {
            if (ctx.toast)
                ctx.toast("Can't extrude a curved face \xE2\x80\x94 extrude "
                          "works on flat faces only.");
            return false;
        }
    }
    m_profile = profile;
    m_mode = mode;
    m_targetBody = targetBody;
    m_sketchId = sourceSketchId;
    return begin(ctx);
}

int ExtrudeController::onBegin(const IopContext& ctx) {
    (void)ctx;
    m_distance = 5.0f;
    std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_distance);
    m_inputFocus = true;

    // Face normal and centre. A compound profile (multi-region extrude —
    // several letters at once) uses its first face: all regions of one sketch
    // are coplanar, so any face gives the right normal.
    TopoDS_Shape normShape = m_profile;
    if (m_profile.ShapeType() != TopAbs_FACE) {
        TopExp_Explorer fx(m_profile, TopAbs_FACE);
        if (fx.More()) normShape = fx.Current();
    }
    if (normShape.ShapeType() == TopAbs_FACE) {
        BRepGProp_Face prop(TopoDS::Face(normShape));
        gp_Pnt center;
        gp_Vec norm;
        double u1, u2, v1, v2;
        prop.Bounds(u1, u2, v1, v2);
        prop.Normal((u1 + u2) * 0.5, (v1 + v2) * 0.5, center, norm);
        if (norm.Magnitude() > 1e-10)
            m_normal = glm::normalize(glm::vec3(norm.X(), norm.Y(), norm.Z()));
        m_origin = glm::vec3(center.X(), center.Y(), center.Z());
    }
    // Point the on-screen arrow INTO the body for a Subtract, so dragging
    // toward the material deepens the cut.
    if (m_mode == ExtrudeMode::Subtract) m_normal = -m_normal;

    // Threaded target bodies are fine: the preview is always a NewBody tool
    // volume (never a per-frame boolean against the target), and the real
    // Subtract runs once at commit through History::pushOperation, which
    // reflows the cut beneath the Thread step and re-cuts in background.
    return kNoTargetBody;   // the preview body doesn't exist yet
}

std::unique_ptr<Operation> ExtrudeController::buildOp(const IopContext& ctx) {
    (void)ctx;
    // The live instance. Always NewBody — the user watches the tool volume
    // being swept; Subtract's real boolean is buildCommitOp's job.
    auto op = std::make_unique<ExtrudeOp>();
    op->setProfile(m_profile);
    op->setDistance(opDistance());
    op->setMode(ExtrudeMode::NewBody);
    op->setSketchSource(m_sketchId);
    return op;
}

bool ExtrudeController::syncLiveOp(Operation& op) {
    static_cast<ExtrudeOp&>(op).setDistance(opDistance());
    return true;
}

std::unique_ptr<Operation> ExtrudeController::buildCommitOp(const IopContext& ctx) {
    (void)ctx;
    // NewBody: the previewed instance IS the result — let the base record it
    // as-is. Subtract: the preview was only a tool volume, so hand back the
    // real boolean cut against the body the sketch was drawn on.
    if (m_mode != ExtrudeMode::Subtract || m_targetBody < 0) {
        std::fprintf(stdout, "Extruded %.1f mm\n", m_distance);
        return nullptr;
    }
    auto op = std::make_unique<ExtrudeOp>();
    op->setProfile(m_profile);
    op->setDistance(opDistance());
    op->setMode(ExtrudeMode::Subtract);
    op->setTargetBody(m_targetBody);
    op->setSketchSource(m_sketchId);
    std::fprintf(stdout, "Subtracted %.1f mm from body %d\n",
                 std::abs(m_distance), m_targetBody);
    return op;
}

void ExtrudeController::updateExtrude(const IopContext& ctx, bool applySnap) {
    if (!active()) return;
    if (!std::isfinite(m_distance)) { m_distance = 0.0f; return; }
    // Snap the live distance to the corner-widget grid step before applying
    // (issue #24). Drag/commit paths snap; live typing and the steppers pass
    // applySnap=false so a typed value stays exact.
    if (applySnap && ctx.snapToGrid && ctx.gridStep > 0.0f) {
        m_distance = std::round(m_distance / ctx.gridStep) * ctx.gridStep;
        std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_distance);
    }
    update(ctx);
}

int ExtrudeController::previewBodyId() const {
    const auto* op = static_cast<const ExtrudeOp*>(liveOp());
    return (op && livePreviewApplied()) ? op->createdBodyId() : -1;
}

// Left-drag anywhere in the viewport moves the distance along the arrow's
// normal. No handle to latch — the whole viewport is the drag surface — so
// draggingHandle() stays false and the camera keeps its own claim (the
// dispatch loop skips this while the camera is dragging).
void ExtrudeController::onViewportInput(const IopViewport& vp,
                                        const IopContext& ctx) {
    if (!active() || !vp.dragging) return;
    m_distance += vp.dragAlongAxis(m_origin, m_normal, vp.mouseDelta);
    std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_distance);
    updateExtrude(ctx);
}

void ExtrudeController::renderExtrudePanel(const IopContext& ctx) {
    if (!active()) return;
    const float s = ctx.panel.uiScale;
    const bool imTouch = ctx.panel.imTouch;

    materializr::viewportBanner(
        ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
        materializr::touchMode()
            ? "EXTRUDE - Drag in viewport or type distance, then Confirm / Cancel."
            : "EXTRUDE - Drag in viewport or type distance. Enter to confirm, Escape to cancel.");

    // Floating distance input panel. im-touch: anchored just off the extrude
    // arrow's tip, like the sketch bubbles; other layouts (or a tip behind
    // the camera) keep the fixed top-right spot.
    bool extAnchored = false;
    if (imTouch && ctx.panel.anchorValid) {
        const ImVec2 vwp = ImGui::GetWindowPos();
        const float vww = ImGui::GetWindowWidth();
        float ax = std::min(std::max(ctx.panel.anchorX + 24.0f * s, vwp.x + 8.0f),
                            vwp.x + vww - 250.0f * s);
        float ay = std::max(ctx.panel.anchorY + 12.0f * s, vwp.y + 8.0f);
        ImGui::SetNextWindowPos(ImVec2(ax, ay), ImGuiCond_Appearing);
        extAnchored = true;
    }
    if (!extAnchored)
        ImGui::SetNextWindowPos(ImVec2(
            std::max(ImGui::GetWindowPos().x + 6.0f,
                     ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 250.0f * s),
            ImGui::GetWindowPos().y + 50), ImGuiCond_Appearing);
    // Pin the width (min == max) so moving the panel can't feed back into
    // the value field's content-avail width and ratchet the window wider.
    ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f * s, 0.0f),
                                        ImVec2(240.0f * s, 100000.0f));
    ImGui::Begin("##ExtrudeInput", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_AlwaysAutoResize);
    opDialogDragGrip(s);

    if (!imTouch) {   // im-touch: just the value well below
        ImGui::Text("Extrude Distance (mm)");
        ImGui::Separator();
    }

    if (m_inputFocus) {
        if (!materializr::touchMode())
            ImGui::SetKeyboardFocusHere();  // touch: drag to set distance, or tap the field to type
        m_inputFocus = false;
    }

    bool doCommit = false, doCancel = false;
    if (imTouch) {
        // im-touch: the WHOLE panel is this one tappable value well — no
        // header, hint or steppers (Steve: the full "distance dialog" kept
        // showing up; drag for coarse, pad for exact).
        if (touchui::amountField("extAmt", "Distance", &m_distance,
                                 "mm", 1, /*allowSign=*/true)) {
            std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_distance);
            updateExtrude(ctx, /*applySnap=*/false);  // typed = exact
        }
        // touch: raise the soft keyboard only when the field is TAPPED (not
        // on open, which would cover the drag handle). ImGui's own
        // click-activation doesn't focus the field in this transient overlay
        // popup, so re-assert focus on the tap (issue #22).
        if (materializr::touchMode() && ImGui::IsItemClicked())
            ImGui::SetKeyboardFocusHere(-1);
    } else {
        if (ImGui::InputText("##dist", m_inputBuf, sizeof(m_inputBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            // Enter pressed — commit (parseFinite: keep last on garbage)
            (void)materializr::parseFinite(m_inputBuf, m_distance);
            updateExtrude(ctx);
            doCommit = true;
        } else {
            // Update distance from text as user types
            float parsed = m_distance;
            if (materializr::parseFinite(m_inputBuf, parsed) &&
                std::abs(parsed - m_distance) > 0.01f && std::abs(parsed) > 0.01f) {
                m_distance = parsed;
                updateExtrude(ctx, /*applySnap=*/false);  // live typing = exact
            }
        }
        ImGui::SameLine();
        ImGui::Text("mm");
    }

    // Quick-nudge stepper (replaces the slider): ±10/1/0.1, and 0 to clear
    // the extrusion mid-preview. Desktop only — im-touch stays a single well.
    if (!imTouch &&
        materializr::stepperRow("extrudeStep", &m_distance,
                                /*allowNegative=*/true, -50.0f, 50.0f)) {
        std::snprintf(m_inputBuf, sizeof(m_inputBuf), "%.1f", m_distance);
        updateExtrude(ctx, /*applySnap=*/false);  // steppers override the grid
    }

    if (!ctx.cornerCommitUi) {   // im-touch: corner ✓/✗ FABs instead
        ImGui::Spacing();
        if (ImGui::Button(materializr::btnConfirm(), ImVec2(110, 0)))
            doCommit = true;
        ImGui::SameLine();
        if (ImGui::Button(materializr::btnCancel(), ImVec2(110, 0)))
            doCancel = true;
    }
    ImGui::End();
    // Commit/cancel AFTER End() — they tear the controller's state down, and
    // the window has to be closed first.
    if (doCommit) commit(ctx);
    else if (doCancel) cancel(ctx);
}

void ExtrudeController::confirmFromKey(const IopContext& ctx) {
    if (!active()) return;
    (void)materializr::parseFinite(m_inputBuf, m_distance);
    updateExtrude(ctx);
    commit(ctx);
}

void ExtrudeController::panelBody(const IopContext&, bool&) {
    // Unused: renderExtrudePanel draws the whole thing (renderPanel is
    // overridden silent) because the value well anchors to the viewport.
}

void ExtrudeController::onCleanup() {
    m_profile.Nullify();
    m_mode = ExtrudeMode::NewBody;
    m_targetBody = -1;
    m_sketchId = -1;
}

} // namespace materializr
