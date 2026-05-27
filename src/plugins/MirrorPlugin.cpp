#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/MirrorOp.h"
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <memory>

namespace {

// Mirror the first selected body across one of its own minimum bounding-box
// faces (min-X face for axis 0, min-Y for 1, min-Z for 2). Anchoring the mirror
// plane to the body — rather than a world-origin plane — guarantees the copy
// lands flush beside the original and is actually visible; a world plane through
// the origin maps a body centred there straight onto itself.
void doMirrorAxis(materializr::PluginContext& ctx, int axis) {
    const auto& sel = ctx.selection().getSelection();
    if (sel.empty() || sel[0].bodyId < 0) return;

    auto op = std::make_unique<MirrorOp>();
    op->setBody(sel[0].bodyId);

    bool ok = false;
    try {
        const TopoDS_Shape& shape = ctx.document().getBody(sel[0].bodyId);
        Bnd_Box bb;
        BRepBndLib::Add(shape, bb);
        if (!bb.IsVoid()) {
            double x1, y1, z1, x2, y2, z2;
            bb.Get(x1, y1, z1, x2, y2, z2);
            double cx = (x1 + x2) * 0.5, cy = (y1 + y2) * 0.5, cz = (z1 + z2) * 0.5;
            gp_Pnt pt;
            gp_Dir dir;
            if (axis == 0)      { pt = gp_Pnt(x1, cy, cz); dir = gp_Dir(1, 0, 0); }
            else if (axis == 1) { pt = gp_Pnt(cx, y1, cz); dir = gp_Dir(0, 1, 0); }
            else                { pt = gp_Pnt(cx, cy, z1); dir = gp_Dir(0, 0, 1); }
            op->setPlane(MirrorPlane::Custom);
            op->setCustomPlane(gp_Ax2(pt, dir));
            ok = true;
        }
    } catch (...) {}

    if (!ok) op->setPlane(MirrorPlane::YZ); // degenerate fallback
    op->setKeepOriginal(true);
    if (ctx.history().pushOperation(std::move(op), ctx.document())) {
        ctx.markMeshesDirty();
    }
}

// Mirror the owning body of the first selected planar face across that face's
// plane. Lets the user reflect a body about an arbitrary flat face it already has.
void doMirrorAcrossFace(materializr::PluginContext& ctx) {
    const auto& sel = ctx.selection().getSelection();
    for (const auto& e : sel) {
        if (e.type != SelectionType::Face || e.shape.IsNull() || e.bodyId < 0) continue;
        TopoDS_Face face = TopoDS::Face(e.shape);
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        if (surf.IsNull() || !surf->IsKind(STANDARD_TYPE(Geom_Plane))) continue;
        gp_Pln pln = Handle(Geom_Plane)::DownCast(surf)->Pln();
        const gp_Ax3& ax = pln.Position();

        auto op = std::make_unique<MirrorOp>();
        op->setBody(e.bodyId);
        op->setPlane(MirrorPlane::Custom);
        op->setCustomPlane(gp_Ax2(ax.Location(), ax.Direction()));
        op->setKeepOriginal(true);
        if (ctx.history().pushOperation(std::move(op), ctx.document())) {
            ctx.markMeshesDirty();
        }
        return; // first planar face only
    }
}

} // namespace

REGISTER_PLUGIN(Mirror, [](materializr::PluginContext& ctx) {
    ctx.registerToolbarButton({"Mirror X", "Transform",
        materializr::SelectionContext::HasBodies, 210,
        [](materializr::PluginContext& ctx) { doMirrorAxis(ctx, 0); }, nullptr});
    ctx.registerToolbarButton({"Mirror Y", "Transform",
        materializr::SelectionContext::HasBodies, 211,
        [](materializr::PluginContext& ctx) { doMirrorAxis(ctx, 1); }, nullptr});
    ctx.registerToolbarButton({"Mirror Z", "Transform",
        materializr::SelectionContext::HasBodies, 212,
        [](materializr::PluginContext& ctx) { doMirrorAxis(ctx, 2); }, nullptr});

    // Shown when a face is selected: mirror that face's body across the face plane.
    ctx.registerToolbarButton({"Mirror across Face", "Transform",
        materializr::SelectionContext::HasFaces, 213,
        [](materializr::PluginContext& ctx) { doMirrorAcrossFace(ctx); }, nullptr});
})
