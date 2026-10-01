#pragma once
// How every mesher call in Materializr asks OCCT to triangulate a shape.
//
// The one setting that matters is the algorithm. OCCT's default (Watson)
// slows down sharply on a planar face with many curved inner wires, which is
// exactly what hole patterns and SVG outlines produce: one face of a 400-hole
// plate took 1028 ms, the whole body 1095 ms, on every edit and every load.
// Delabella (in OCCT since 7.6; every platform here builds 7.9.3) meshes that
// face in 47 ms and the body in 84 ms, was never slower on spheres, tori,
// fillets or slotted plates, and gives the same triangle counts with equal or
// smaller achieved deflection.
//
// BRepMesh_IncrementalMesh's shape constructors already call Perform(); a
// second Perform() is a full extra pass over the model (25 ms on that plate),
// so callers must not add one.
#include <IMeshTools_Parameters.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <Adaptor3d_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Poly_Triangle.hxx>
#include <cmath>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Curve.hxx>
#include <TopoDS_Edge.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <Standard_Type.hxx>
#include <algorithm>
#include <chrono>

namespace materializr {

inline IMeshTools_Parameters meshParams(double deflection, double angularDeflection,
                                        bool inParallel)
{
    IMeshTools_Parameters p;
    p.Deflection = deflection;
    p.Angle = angularDeflection;
    p.Relative = false;
    p.InParallel = inParallel;
    p.MeshAlgo = IMeshTools_MeshAlgoType_Delabella;
    return p;
}


// Aborts a mesher run once its time budget is spent. OCCT polls UserBreak()
// all through BRepMesh, so this lands within milliseconds.
class MeshDeadline : public Message_ProgressIndicator {
public:
    DEFINE_STANDARD_RTTI_INLINE(MeshDeadline, Message_ProgressIndicator)
    explicit MeshDeadline(double seconds)
        : m_end(std::chrono::steady_clock::now() +
                std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0))) {}
    Standard_Boolean UserBreak() override {
        return std::chrono::steady_clock::now() > m_end;
    }
protected:
    void Show(const Message_ProgressScope&, const Standard_Boolean) override {}
private:
    std::chrono::steady_clock::time_point m_end;
};

// Triangulate a plain swept wall directly as a strip along its directrix.
//
// A prism wall (SurfaceOfLinearExtrusion) is ruled: S(u,v) = C(u) + v*dir, so a
// strip of quads between C(u_k)+v0*dir and C(u_k)+v1*dir is the exact surface,
// and the only thing that sets its quality is how finely the directrix C is
// sampled. Delabella declines these faces when the directrix is a traced
// outline, and Watson then spends 25 s+ on one at Ultra quality (0.01 mm /
// 0.10 rad); this is O(n) and instant. Only a FULL wall qualifies - all four
// edges must be iso-lines in the face's UV domain (two along u, two along v) -
// so a wall trimmed by a boolean falls through to the real meshers.
inline bool meshExtrusionWallStrip(const TopoDS_Face& f, double deflection,
                                   double angularDeflection) {
    try {
        BRepAdaptor_Surface s(f, Standard_False);   // raw surface: nodes stay in face-local coordinates
        if (s.GetType() != GeomAbs_SurfaceOfExtrusion) return false;
        int alongU = 0, alongV = 0;
        for (TopExp_Explorer ex(f, TopAbs_EDGE); ex.More(); ex.Next()) {
            double a = 0.0, b = 0.0;
            Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(TopoDS::Edge(ex.Current()), f, a, b);
            if (pc.IsNull()) return false;
            if (Handle(Geom2d_TrimmedCurve) t = Handle(Geom2d_TrimmedCurve)::DownCast(pc))
                pc = t->BasisCurve();
            Handle(Geom2d_Line) ln = Handle(Geom2d_Line)::DownCast(pc);
            if (ln.IsNull()) return false;
            const gp_Dir2d d = ln->Direction();
            if (std::abs(d.Y()) < 1e-9) ++alongU;
            else if (std::abs(d.X()) < 1e-9) ++alongV;
            else return false;
        }
        if (alongU != 2 || alongV != 2) return false;

        double u0, u1, v0, v1;
        BRepTools::UVBounds(f, u0, u1, v0, v1);
        if (!(v1 > v0) || !(u1 > u0)) return false;
        const gp_Vec dir(s.Direction());
        Handle(Adaptor3d_Curve) base = s.BasisCurve();
        if (base.IsNull()) return false;

        GCPnts_TangentialDeflection pts(*base, u0, u1, angularDeflection, deflection, 2);
        const int n = pts.NbPoints();
        if (n < 2) return false;

        Handle(Poly_Triangulation) tri = new Poly_Triangulation(2 * n, 2 * (n - 1), Standard_False);
        for (int k = 0; k < n; ++k) {
            const gp_Pnt c = pts.Value(k + 1);
            tri->SetNode(k + 1, c.Translated(dir * v0));
            tri->SetNode(n + k + 1, c.Translated(dir * v1));
        }
        for (int k = 1; k < n; ++k) {       // natural surface orientation (du x dv)
            tri->SetTriangle(2 * k - 1, Poly_Triangle(k, k + 1, n + k + 1));
            tri->SetTriangle(2 * k, Poly_Triangle(k, n + k + 1, n + k));
        }
        tri->Deflection(deflection);
        BRep_Builder().UpdateFace(f, tri);
        return true;
    } catch (...) {
        return false;
    }
}

// Watson-mesh one bare face, giving up after `seconds`.
//
// Surface-deflection control is OFF here. On the swept wall of a traced
// 450-point outline (an extrusion surface over a 600-pole directrix) it made
// Watson insert 92,806 nodes and take 6.5 s; without it the same wall meshes
// in 0.8 s with 14,820 nodes (0.2 s at angle 1.0). A wall like that is ruled in
// the sweep direction, so the check buys nothing, and this path only runs for
// faces Delabella already failed on. (~100 s on the UI thread before the time
// box existed.) A timed-out attempt is discarded so the caller can escalate.
inline bool meshFaceWatsonBounded(const TopoDS_Face& f, double deflection,
                                  double angularDeflection, double seconds) {
    if (meshExtrusionWallStrip(f, deflection, angularDeflection)) return true;
    TopLoc_Location loc;
    try {
        IMeshTools_Parameters wp = meshParams(deflection, angularDeflection, false);
        wp.MeshAlgo = IMeshTools_MeshAlgoType_Watson;
        wp.ControlSurfaceDeflection = false;
        opencascade::handle<MeshDeadline> pi = new MeshDeadline(seconds);
        BRepMesh_IncrementalMesh retry(f, wp, pi->Start());
        if (pi->UserBreak()) {                 // budget spent: drop any partial mesh
            BRepTools::Clean(f);
            return false;
        }
    } catch (...) {
        return false;
    }
    return !BRep_Tool::Triangulation(f, loc).IsNull();
}

constexpr double kBareFaceBudgetSec = 3.0;

// Cheap guess at how slow a shape will be to mesh, for a body that has no
// timing history yet (a fresh extrude). Total B-spline poles over its edges:
// a traced 450-point outline carries thousands, an ordinary part a few dozen.
// Walks the edges only - no geometry is evaluated.
inline int meshComplexityHint(const TopoDS_Shape& shape) {
    int poles = 0;
    try {
        for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
            double f = 0.0, l = 0.0;
            Handle(Geom_Curve) c = BRep_Tool::Curve(TopoDS::Edge(ex.Current()), f, l);
            if (c.IsNull()) continue;
            if (Handle(Geom_BSplineCurve) b = Handle(Geom_BSplineCurve)::DownCast(c))
                poles += b->NbPoles();
        }
    } catch (...) {}
    return poles;
}
// Above this a body is meshed off-thread even with no old mesh to keep on
// screen: it shows up when the worker lands rather than freezing the UI.
constexpr int kHeavyPoleHint = 400;

// A face where even the Watson retry above fails - real and reproducible on
// an entirely ordinary planar face (10-edge boundary, no degenerate edges),
// not just a theoretical edge case: confirmed mesh-failing under BOTH
// Delabella and Watson at a project's "High"/"Ultra" quality settings
// (0.03/0.15 and 0.01/0.10) while succeeding fine at "Medium" (0.10/0.30)
// and coarser. Whatever OCCT's samplers dislike about that fine a spacing on
// this face, backing off clears it - recovered at 2x deflection for the
// High-quality case above, 4x for Ultra. A locally coarser single face is a
// far smaller defect than a hole clean through the model, so escalate
// through a few factors (capping angular deflection around 34 degrees,
// where facets stop looking meaningfully different) before giving up.
inline bool meshBareFaceEscalating(const TopoDS_Face& f, double deflection,
                                   double angularDeflection) {
    TopLoc_Location loc;
    for (double factor : {2.0, 4.0, 8.0, 16.0, 32.0}) {
        meshFaceWatsonBounded(f, deflection * factor,
                              std::min(angularDeflection * factor, 0.6),
                              kBareFaceBudgetSec);
        if (!BRep_Tool::Triangulation(f, loc).IsNull()) return true;
    }
    return false;
}

// Delabella (meshParams()'s algorithm, chosen for speed on many-holed faces)
// can leave a face with zero triangles on otherwise fully BRepCheck-valid,
// closed geometry - confirmed on real boolean-result bodies (issue #117).
// Mesh with it, then retry any bare face with Watson (OCCT's default, slower
// but far more robust) so a real hole never reaches the screen. Every caller
// that meshes a shape for DISPLAY (as opposed to STL/OBJ/glTF export, which
// tolerate a slower, non-Delabella pass across the board) should go through
// this instead of constructing BRepMesh_IncrementalMesh directly - a plain
// Delabella call has already had to be patched into this fallback more than
// once (the main render path, and again for the async worker a live
// interactive op's result gets pre-meshed on) precisely because it is easy to
// add a new meshing call site without remembering the fallback.
inline void meshWithFallback(const TopoDS_Shape& shape, double deflection,
                             double angularDeflection, bool inParallel)
{
    BRepMesh_IncrementalMesh(shape, meshParams(deflection, angularDeflection, inParallel));
    for (TopExp_Explorer fx(shape, TopAbs_FACE); fx.More(); fx.Next()) {
        const TopoDS_Face& f = TopoDS::Face(fx.Current());
        TopLoc_Location loc;
        if (!BRep_Tool::Triangulation(f, loc).IsNull()) continue;
        try {
            IMeshTools_Parameters wp = meshParams(deflection, angularDeflection, false);
            wp.MeshAlgo = IMeshTools_MeshAlgoType_Watson;
            BRepMesh_IncrementalMesh(f, wp);
        } catch (...) {
            // Fall through to the escalating retry below.
        }
        if (BRep_Tool::Triangulation(f, loc).IsNull())
            meshBareFaceEscalating(f, deflection, angularDeflection);
    }
}

} // namespace materializr
