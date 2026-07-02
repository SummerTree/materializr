// Diagnostic probe: drive the REAL ShellOp against a project's (filleted) body
// at a sweep of thicknesses, timing each call. Confirms (a) an over-thick wall
// now fails CLEANLY and fast (no infinite "Cote PT2PT3 nul" hang), and (b) the
// opened face rebinds onto a regenerated body so the shell survives an edit.
//
// Usage: probe_shell <project.materializr>

#include "core/Document.h"
#include "io/ProjectIO.h"
#include "modeling/ShellOp.h"

#include <BRepGProp_Face.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <OSD.hxx>
#include <chrono>
#include <cstdio>

using materializr::ProjectIO;
using materializr::ProjectHistory;

static TopoDS_Face bottomFace(const TopoDS_Shape& body) {
    TopoDS_Face best; double bestDot = 1e9;
    for (TopExp_Explorer ex(body, TopAbs_FACE); ex.More(); ex.Next()) {
        TopoDS_Face f = TopoDS::Face(ex.Current());
        try {
            BRepGProp_Face gf(f);
            Standard_Real u0, u1, v0, v1; gf.Bounds(u0, u1, v0, v1);
            gp_Pnt p; gp_Vec n; gf.Normal((u0+u1)/2, (v0+v1)/2, p, n);
            if (n.Magnitude() < 1e-9) continue;
            n.Normalize();
            if (n.Y() < bestDot) { bestDot = n.Y(); best = f; }
        } catch (...) {}
    }
    return best;
}

static double vol(const TopoDS_Shape& s) {
    GProp_GProps g; BRepGProp::VolumeProperties(s, g); return g.Mass();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <project>\n", argv[0]); return 2; }
    OSD::SetSignal(Standard_False);

    Document doc;
    ProjectHistory hist;
    auto res = ProjectIO::load(argv[1], doc, &hist);
    if (!res.success) { std::fprintf(stderr, "load failed: %s\n", res.errorMessage.c_str()); return 1; }

    int bodyId = doc.getAllBodyIds().empty() ? -1 : doc.getAllBodyIds().front();
    if (bodyId < 0) { std::fprintf(stderr, "no body\n"); return 1; }
    const TopoDS_Shape body0 = doc.getBody(bodyId);
    TopoDS_Face bf = bottomFace(body0);
    std::printf("body %d, bottom-face found=%d, base vol=%.1f\n",
                bodyId, !bf.IsNull(), vol(body0));

    // (a) Thickness sweep through the REAL ShellOp — must never hang.
    std::printf("\n--- ShellOp thickness sweep (each must return quickly) ---\n");
    for (double t : { 1.0, 2.0, 2.5, 3.0, 4.0, 6.0 }) {
        doc.updateBody(bodyId, body0); // reset
        ShellOp op;
        op.setBody(bodyId);
        op.setThickness(t);
        op.addFaceToRemove(bf);
        auto t0 = std::chrono::steady_clock::now();
        bool ok = op.execute(doc);
        double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0).count();
        std::printf("  t=%.2f : %-9s %8.1f ms  vol=%.1f\n",
                    t, ok ? "OK" : "fail(clean)", ms,
                    ok ? vol(doc.getBody(bodyId)) : 0.0);
    }

    // (b) Face rebind: shell a valid thickness, then translate the body (as a
    // regeneration would move geometry) and re-run the SAME op — the opened
    // face must re-bind to the moved body instead of being lost.
    std::printf("\n--- face rebind across a body move ---\n");
    doc.updateBody(bodyId, body0);
    ShellOp op;
    op.setBody(bodyId);
    op.setThickness(1.5);
    op.addFaceToRemove(bf);
    bool first = op.execute(doc);
    std::printf("  initial shell: %s vol=%.1f\n", first ? "OK" : "FAIL",
                first ? vol(doc.getBody(bodyId)) : 0.0);

    // Simulate an upstream rebuild: move the ORIGINAL body and set it as the
    // current body, so op.execute must re-find the (now different-handle) face.
    gp_Trsf tr; tr.SetTranslation(gp_Vec(0, 0, 25));
    TopoDS_Shape moved = BRepBuilderAPI_Transform(body0, tr, true).Shape();
    doc.updateBody(bodyId, moved);
    bool second = op.execute(doc);
    std::printf("  after move+re-exec: %s vol=%.1f (expect ~same as initial)\n",
                second ? "OK — rebound" : "FAIL — face lost",
                second ? vol(doc.getBody(bodyId)) : 0.0);
    return 0;
}
