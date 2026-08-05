#include "ExtrudeController.h"
#include "../core/Document.h"
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

void ExtrudeController::panelBody(const IopContext&, bool&) {
    // Unused: the panel lives in Application_Viewport (renderPanel is
    // overridden silent) because its distance well anchors to the viewport.
}

void ExtrudeController::onCleanup() {
    m_profile.Nullify();
    m_mode = ExtrudeMode::NewBody;
    m_targetBody = -1;
    m_sketchId = -1;
}

} // namespace materializr
