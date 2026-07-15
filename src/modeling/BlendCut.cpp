#include "BlendCut.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace materializr {
namespace blendcut {
namespace {

// Permanent, env-gated diagnostics: MZR_BLENDCUT_DEBUG=1 traces which stage
// refuses a fallback build — this saga kept needing it re-added.
bool bcDebug() {
    static const bool on = std::getenv("MZR_BLENDCUT_DEBUG") != nullptr;
    return on;
}
#define BC_DBG(...) do { if (bcDebug()) std::fprintf(stderr, __VA_ARGS__); } while (0)

struct EdgeInfo {
    TopoDS_Edge edge;
    gp_Lin line;               // underlying infinite line of the edge
    gp_Pnt p0, p1, pm;         // endpoints + midpoint
    TopoDS_Face fRef, fOther;  // dRef is measured along fRef
    gp_Pln plnRef, plnOther;
    gp_Dir nRef, nOther;       // outward face normals at the edge
    gp_Dir dRefDir, dOtherDir; // in-face directions away from the edge
    bool concave = false;      // interior corner (chamfer FILLS, can't cut)
};

// A run of collinear fragments sharing one plane pair: swept as ONE tool
// over the union of their spans, which is what carries the blend across the
// gap a hole or pocket bit out of the edge.
struct Group {
    EdgeInfo rep;      // representative — dirs/normals valid for all members
    double tmin, tmax; // span along rep.line covered by every member
};

// A cutting solid plus the face of it that IS the blend surface (chamfer
// plane / fillet cylinder) — the caller finds the bevel on the cut result
// through it.
struct Tool {
    TopoDS_Shape solid;
    TopoDS_Face blendTemplate;
};

bool outwardNormal(const TopoDS_Face& f, gp_Dir& out) {
    try {
        BRepGProp_Face gf(f);
        Standard_Real u0, u1, v0, v1;
        gf.Bounds(u0, u1, v0, v1);
        gp_Pnt p;
        gp_Vec n;
        gf.Normal((u0 + u1) * 0.5, (v0 + v1) * 0.5, p, n);
        if (n.Magnitude() < 1e-12) return false;
        out = gp_Dir(n);
        return true;
    } catch (...) { return false; }
}

bool onFace(const TopoDS_Face& f, const gp_Pnt& p) {
    BRepClass_FaceClassifier cls(f, p, 1e-6);
    return cls.State() == TopAbs_IN || cls.State() == TopAbs_ON;
}

bool planesMatch(const gp_Pln& a, const gp_Pln& b) {
    if (!a.Axis().Direction().IsParallel(b.Axis().Direction(), 1e-4))
        return false;
    return a.Distance(b.Location()) < 1e-5;
}

bool analyzeEdge(const TopoDS_Shape& body, const TopoDS_Edge& e,
                 const TopTools_IndexedDataMapOfShapeListOfShape& efm,
                 const gp_Pln* refPln, double probeEps, EdgeInfo& out) {
    BRepAdaptor_Curve c(e);
    if (c.GetType() != GeomAbs_Line) return false;
    out.edge = e;
    out.line = c.Line();
    out.p0 = c.Value(c.FirstParameter());
    out.p1 = c.Value(c.LastParameter());
    out.pm = c.Value((c.FirstParameter() + c.LastParameter()) * 0.5);

    if (!efm.Contains(e)) return false;
    const TopTools_ListOfShape& fs = efm.FindFromKey(e);
    if (fs.Extent() < 2) return false;
    TopoDS_Face f1 = TopoDS::Face(fs.First());
    TopoDS_Face f2;
    for (const TopoDS_Shape& f : fs)
        if (!f.IsSame(f1)) { f2 = TopoDS::Face(f); break; }
    if (f2.IsNull()) return false;
    if (BRepAdaptor_Surface(f1).GetType() != GeomAbs_Plane ||
        BRepAdaptor_Surface(f2).GetType() != GeomAbs_Plane)
        return false;
    // dRef belongs to the asymmetric reference when one of this edge's pair
    // lies on its PLANE (by plane, not face identity: a pocket may have
    // fragmented the reference face, and the fragment bordering this edge is
    // a different TopoDS face on the same plane), else to the first face
    // (native Add() semantics).
    if (refPln && planesMatch(BRepAdaptor_Surface(f2).Plane(), *refPln))
        std::swap(f1, f2);
    out.fRef = f1;
    out.fOther = f2;
    out.plnRef = BRepAdaptor_Surface(f1).Plane();
    out.plnOther = BRepAdaptor_Surface(f2).Plane();

    if (!outwardNormal(f1, out.nRef) || !outwardNormal(f2, out.nOther))
        return false;
    gp_Dir t = out.line.Direction();
    // TRUE in-face directions, robust against nearby features (#57): of the
    // two candidates ±(n × t), pick the one whose probe points land ON the
    // face — by MAJORITY across samples along the edge (a hole under one
    // sample can't flip the answer, unlike the old single-midpoint probe) at
    // a small offset (0.2mm, retry 0.05 for very narrow faces).
    auto pickDir = [&](const TopoDS_Face& f, const gp_Dir& n,
                       gp_Dir& outDir) -> bool {
        gp_Dir cand = n.Crossed(t);
        auto votes = [&](const gp_Dir& d) {
            int hit = 0;
            const int N = 9;
            for (int i = 0; i < N; ++i) {
                const double u = (i + 0.5) / N;
                gp_Pnt base(out.p0.XYZ() * (1.0 - u) + out.p1.XYZ() * u);
                for (double eps : {0.2, 0.05})
                    if (onFace(f, base.Translated(gp_Vec(d) * eps))) {
                        ++hit;
                        break;
                    }
            }
            return hit;
        };
        const int plus = votes(cand), minus = votes(cand.Reversed());
        if (plus == 0 && minus == 0) return false; // can't see the face at all
        outDir = (plus >= minus) ? cand : cand.Reversed();
        return true;
    };
    if (!pickDir(f1, out.nRef, out.dRefDir) ||
        !pickDir(f2, out.nOther, out.dOtherDir))
        return false;

    // Classify the corner from the directions: CONVEX = each face runs to the
    // material side of the other's plane (d·n(other) < 0); CONCAVE (interior
    // corner, 270° of material — a chamfer FILLS it) = both run to the open
    // side. Mixed / near-tangent → refuse; the tool degenerates there. (Note
    // a solid-classifier probe on the outward bisector can NOT tell these
    // apart — it reads OUT for both 90° and 270° corners.)
    const double s1 = out.dRefDir.Dot(out.nOther);
    const double s2 = out.dOtherDir.Dot(out.nRef);
    if (s1 < -0.05 && s2 < -0.05) out.concave = false;
    else if (s1 > 0.05 && s2 > 0.05) out.concave = true;
    else return false;
    (void)body;
    (void)probeEps;
    return true;
}

double paramOn(const gp_Lin& l, const gp_Pnt& p) {
    return gp_Vec(l.Location(), p).Dot(gp_Vec(l.Direction()));
}

// Analyze every selected edge (all-or-nothing: one unsupported edge refuses
// the whole fallback so a multi-edge blend never comes back half-built) and
// merge collinear fragments on the same plane pair into single-span groups.
bool buildGroups(const TopoDS_Shape& body,
                 const std::vector<TopoDS_Edge>& edges, const gp_Pln* refPln,
                 double probeEps, bool asymmetric, bool wantConcave,
                 std::vector<Group>& groups) {
    TopTools_IndexedDataMapOfShapeListOfShape efm;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, efm);

    std::vector<EdgeInfo> infos;
    for (const auto& e : edges) {
        EdgeInfo info;
        if (!analyzeEdge(body, e, efm, refPln, probeEps, info)) return false;
        if (info.concave != wantConcave) return false; // wrong tool family
        infos.push_back(info);
    }

    for (const auto& info : infos) {
        Group* home = nullptr;
        for (auto& g : groups) {
            if (!g.rep.line.Direction().IsParallel(info.line.Direction(),
                                                   1e-4))
                continue;
            if (g.rep.line.Distance(info.pm) > 1e-5) continue;
            const bool straight = planesMatch(g.rep.plnRef, info.plnRef) &&
                                  planesMatch(g.rep.plnOther, info.plnOther);
            const bool crossed = planesMatch(g.rep.plnRef, info.plnOther) &&
                                 planesMatch(g.rep.plnOther, info.plnRef);
            if (!straight && !crossed) continue;
            // Crossed = the per-edge "first face" ordering flipped between
            // fragments. Harmless when both setbacks are equal; genuinely
            // ambiguous for an asymmetric chamfer with no shared reference
            // face, so refuse rather than guess.
            if (crossed && !straight && asymmetric && !refPln) return false;
            home = &g;
            break;
        }
        if (!home) {
            groups.push_back({info, 0.0, 0.0});
            home = &groups.back();
            home->tmin = home->tmax = paramOn(info.line, info.p0);
        }
        for (const gp_Pnt* p : {&info.p0, &info.p1}) {
            double t = paramOn(home->rep.line, *p);
            home->tmin = std::min(home->tmin, t);
            home->tmax = std::max(home->tmax, t);
        }
    }
    return !groups.empty();
}

// Profile plane basis for one group: the corner point at the span start and
// the sweep vector covering the merged span.
bool groupSpan(const Group& g, gp_Pnt& P0, gp_Vec& sweep) {
    const double len = g.tmax - g.tmin;
    if (len < 1e-6) return false;
    P0 = g.rep.line.Location().Translated(gp_Vec(g.rep.line.Direction()) *
                                          g.tmin);
    sweep = gp_Vec(g.rep.line.Direction()) * len;
    return true;
}

// Sweep a closed planar profile wire and report the face generated by
// `blendEdge` (the profile edge that IS the blend cross-section).
TopoDS_Shape sweepProfile(const TopoDS_Wire& w, const TopoDS_Edge& blendEdge,
                          const gp_Vec& sweep, TopoDS_Face& outTemplate) {
    BRepBuilderAPI_MakeFace mf(w, Standard_True);
    if (!mf.IsDone()) return TopoDS_Shape();
    BRepPrimAPI_MakePrism prism(mf.Face(), sweep);
    if (!prism.IsDone()) return TopoDS_Shape();
    const TopTools_ListOfShape& gen = prism.Generated(blendEdge);
    if (gen.IsEmpty() || gen.First().ShapeType() != TopAbs_FACE)
        return TopoDS_Shape();
    outTemplate = TopoDS::Face(gen.First());
    return prism.Shape();
}

// Does the setback line (edge line displaced by `off`) land ON `f` for at
// least ONE of several samples along the group's span? A single-point check
// at the edge midpoint wrongly refused the exact case this fallback exists
// for: a bevel that legitimately sweeps ACROSS a hole in the adjacent face
// (the sample lands in the void). One sample on the face proves the blend
// runs along real face material somewhere; only when EVERY sample misses is
// the blend genuinely bigger than the face it runs along — refuse then.
bool setbackTouchesFace(const Group& g, const TopoDS_Face& f,
                        const gp_Vec& off) {
    const int kSamples = 9;
    for (int i = 0; i < kSamples; ++i) {
        const double t =
            g.tmin + (g.tmax - g.tmin) * (i + 0.5) / kSamples;
        gp_Pnt p = g.rep.line.Location()
                       .Translated(gp_Vec(g.rep.line.Direction()) * t)
                       .Translated(off);
        if (onFace(f, p)) return true;
    }
    return false;
}

// The chamfer tool: a triangular wedge — apex just outside the corner, the
// two setback points A/B exactly where the chamfer plane meets the faces.
bool makeChamferTool(const Group& g, double dRef, double dOther, Tool& out) {
    const EdgeInfo& e = g.rep;
    gp_Pnt P0;
    gp_Vec sweep;
    if (!groupSpan(g, P0, sweep)) return false;
    gp_Pnt A = P0.Translated(gp_Vec(e.dRefDir) * dRef);
    gp_Pnt B = P0.Translated(gp_Vec(e.dOtherDir) * dOther);
    // Overshoot guard, hole-tolerant (see setbackTouchesFace).
    if (!setbackTouchesFace(g, e.fRef, gp_Vec(e.dRefDir) * dRef))
        return false;
    if (!setbackTouchesFace(g, e.fOther, gp_Vec(e.dOtherDir) * dOther))
        return false;
    gp_Vec outv = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    if (outv.Magnitude() < 1e-9) return false;
    outv.Normalize();
    // Apex slightly OUTSIDE the corner so the tool's side faces cross the
    // body's faces transversally instead of lying exactly on them (tangent
    // boolean inputs are where cuts get fragile). Immediately outside a
    // convex corner is empty space, so a small bulge cannot over-cut.
    gp_Pnt C = P0.Translated(outv * (std::min(dRef, dOther) * 0.05));

    BRepBuilderAPI_MakePolygon poly;
    poly.Add(C);
    poly.Add(A);
    poly.Add(B);
    poly.Close();
    if (!poly.IsDone()) return false;
    // The profile edge running A–B sweeps into the chamfer-plane face.
    TopoDS_Edge abEdge;
    for (TopExp_Explorer ex(poly.Wire(), TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(TopoDS::Edge(ex.Current()), v1, v2);
        gp_Pnt q1 = BRep_Tool::Pnt(v1), q2 = BRep_Tool::Pnt(v2);
        if ((q1.Distance(A) < 1e-7 && q2.Distance(B) < 1e-7) ||
            (q1.Distance(B) < 1e-7 && q2.Distance(A) < 1e-7)) {
            abEdge = TopoDS::Edge(ex.Current());
            break;
        }
    }
    if (abEdge.IsNull()) return false;
    out.solid = sweepProfile(poly.Wire(), abEdge, sweep, out.blendTemplate);
    return !out.solid.IsNull();
}

// The fillet tool: same wedge region but bounded by the arc of radius r
// tangent to both faces — apex outside the corner, straight sides to the
// tangency points A/B, arc A→B bulging toward the corner. Cutting it leaves
// exactly the convex fillet cylinder.
bool makeFilletTool(const Group& g, double r, Tool& out) {
    const EdgeInfo& e = g.rep;
    gp_Pnt P0;
    gp_Vec sweep;
    if (!groupSpan(g, P0, sweep)) return false;
    const double c = gp_Vec(e.nRef).Dot(gp_Vec(e.nOther));
    if (c <= -1.0 + 1e-9) return false; // knife edge — no wedge to round
    // Fillet centre: at distance r from BOTH planes, on the material side.
    gp_Vec w = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    if (w.Magnitude() < 1e-9) return false;
    w.Normalize();
    w.Reverse(); // inward bisector
    const double s = r * std::sqrt(2.0 / (1.0 + c));
    gp_Pnt O = P0.Translated(w * s);
    // Tangency points: feet of O on each plane.
    gp_Pnt A = O.Translated(gp_Vec(e.nRef) * r);
    gp_Pnt B = O.Translated(gp_Vec(e.nOther) * r);
    // Same hole-tolerant overshoot guard as the chamfer: the tangency lines
    // must land on their faces SOMEWHERE along the span.
    if (!setbackTouchesFace(g, e.fRef, gp_Vec(P0, A)) ||
        !setbackTouchesFace(g, e.fOther, gp_Vec(P0, B)))
        return false;
    // Arc through the point of the circle nearest the corner (it bulges
    // toward the edge — the removed region lies between arc and corner).
    gp_Vec toCorner(O, P0);
    if (toCorner.Magnitude() < 1e-12) return false;
    toCorner.Normalize();
    gp_Pnt M = O.Translated(toCorner * r);
    GC_MakeArcOfCircle arc(A, M, B);
    if (!arc.IsDone()) return false;
    gp_Vec outv = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    outv.Normalize();
    gp_Pnt C = P0.Translated(outv * (r * 0.05));

    BRepBuilderAPI_MakeEdge ca(C, A), bc(B, C), ab(arc.Value());
    if (!ca.IsDone() || !bc.IsDone() || !ab.IsDone()) return false;
    BRepBuilderAPI_MakeWire mw(ca.Edge(), ab.Edge(), bc.Edge());
    if (!mw.IsDone()) return false;
    // MakeWire may rework the edges it was fed (shared vertices, orientation)
    // so Generated() must be asked about the wire's OWN arc edge — the only
    // circular one of the three.
    TopoDS_Edge arcEdge;
    for (TopExp_Explorer ex(mw.Wire(), TopAbs_EDGE); ex.More(); ex.Next()) {
        if (BRepAdaptor_Curve(TopoDS::Edge(ex.Current())).GetType() ==
            GeomAbs_Circle) {
            arcEdge = TopoDS::Edge(ex.Current());
            break;
        }
    }
    if (arcEdge.IsNull()) return false;
    out.solid = sweepProfile(mw.Wire(), arcEdge, sweep, out.blendTemplate);
    return !out.solid.IsNull();
}

// Does `f` lie on the same support surface as the blend template? Covers the
// two surfaces the tools produce: the chamfer plane and the fillet cylinder.
bool sameSupportSurface(const TopoDS_Face& tmpl, const TopoDS_Face& f) {
    BRepAdaptor_Surface st(tmpl), sf(f);
    if (st.GetType() != sf.GetType()) return false;
    if (st.GetType() == GeomAbs_Plane)
        return planesMatch(st.Plane(), sf.Plane());
    if (st.GetType() == GeomAbs_Cylinder) {
        gp_Cylinder a = st.Cylinder(), b = sf.Cylinder();
        if (std::abs(a.Radius() - b.Radius()) > 1e-6) return false;
        if (!a.Axis().Direction().IsParallel(b.Axis().Direction(), 1e-4))
            return false;
        gp_Lin axisA(a.Axis());
        return axisA.Distance(b.Axis().Location()) < 1e-5;
    }
    return false;
}

// Subtract the tools from the body, validate, and collect the blend faces on
// the result. Captures the cut's generation maps into `ledger` on success.
bool applyCut(const TopoDS_Shape& body, const std::vector<Tool>& tools,
              topo::GenerationLedger& ledger, TopoDS_Shape& outShape,
              std::vector<TopoDS_Shape>& outBlendFaces) {
    BRep_Builder bb;
    TopoDS_Compound comp;
    bb.MakeCompound(comp);
    for (const auto& t : tools) bb.Add(comp, t.solid);

    BRepAlgoAPI_Cut cut(body, comp);
    if (!cut.IsDone()) return false;
    TopoDS_Shape res = cut.Shape();
    if (res.IsNull()) return false;

    // The cut must have actually removed material (a sign error in the tool
    // directions would miss the body entirely and silently produce a no-op
    // "blend"), and the result must be sound.
    GProp_GProps gin, gout;
    BRepGProp::VolumeProperties(body, gin);
    BRepGProp::VolumeProperties(res, gout);
    if (gout.Mass() < 1e-9 || gout.Mass() >= gin.Mass() - 1e-9) return false;
    if (!BRepCheck_Analyzer(res).IsValid()) return false;

    // Blend faces on the result: the surviving pieces of each tool's blend
    // face. Modified() is authoritative; fall back to a same-surface scan if
    // a builder doesn't report tool-face history.
    for (const auto& t : tools) {
        bool found = false;
        try {
            for (const TopoDS_Shape& m : cut.Modified(t.blendTemplate))
                if (m.ShapeType() == TopAbs_FACE) {
                    outBlendFaces.push_back(m);
                    found = true;
                }
        } catch (...) {}
        if (!found) {
            for (TopExp_Explorer ex(res, TopAbs_FACE); ex.More(); ex.Next()) {
                if (sameSupportSurface(t.blendTemplate,
                                       TopoDS::Face(ex.Current()))) {
                    outBlendFaces.push_back(ex.Current());
                    found = true;
                }
            }
        }
        if (!found) return false; // a tool left no blend — over-cut or miss
    }

    ledger.capture(cut, body, TopAbs_EDGE);
    ledger.captureAdd(cut, body, TopAbs_FACE);
    outShape = res;
    return true;
}

// ── Concave (interior-corner) chamfer as a FILL (#57) ──────────────────────
// An interior corner (floor meets a rising wall) is chamfered by ADDING a
// ramp, not cutting. Native OCCT does this fine on clean geometry but gives
// up when the ramp's footprint crosses a feature (a hole in the floor). The
// additive twin of the wedge cut: fuse a ramp prism swept over the full span
// — straight across any feature — then RE-PIERCE it with each crossed void's
// own outline so a hole stays a hole, exactly as if the chamfer had preceded
// the feature in history.

// The fill tool: triangle P0→A→B where A/B sit ON the two faces at the
// chamfer setbacks and the apex is nudged INTO the corner material so the
// fuse meets the body transversally.
bool makeFillTool(const Group& g, double dRef, double dOther, Tool& out) {
    const EdgeInfo& e = g.rep;
    gp_Pnt P0;
    gp_Vec sweep;
    if (!groupSpan(g, P0, sweep)) return false;
    gp_Pnt A = P0.Translated(gp_Vec(e.dRefDir) * dRef);
    gp_Pnt B = P0.Translated(gp_Vec(e.dOtherDir) * dOther);
    // The ramp must rest on real face material somewhere along the span —
    // same hole-tolerant overshoot guard as the cut.
    if (!setbackTouchesFace(g, e.fRef, gp_Vec(e.dRefDir) * dRef)) {
        BC_DBG("[bc] fillTool: dRef=%.2f off fRef\n", dRef);
        return false;
    }
    if (!setbackTouchesFace(g, e.fOther, gp_Vec(e.dOtherDir) * dOther)) {
        BC_DBG("[bc] fillTool: dOther=%.2f off fOther\n", dOther);
        return false;
    }
    // For a concave corner the outward normal bisector points into the OPEN
    // quadrant the ramp will occupy; nudge the apex the other way (into the
    // corner material) so the fuse overlaps instead of kissing.
    gp_Vec inv = gp_Vec(e.nRef) + gp_Vec(e.nOther);
    if (inv.Magnitude() < 1e-9) return false;
    inv.Normalize();
    gp_Pnt C = P0.Translated(inv * (-std::min(dRef, dOther) * 0.05));

    BRepBuilderAPI_MakePolygon poly;
    poly.Add(C);
    poly.Add(A);
    poly.Add(B);
    poly.Close();
    if (!poly.IsDone()) return false;
    TopoDS_Edge abEdge;
    for (TopExp_Explorer ex(poly.Wire(), TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(TopoDS::Edge(ex.Current()), v1, v2);
        gp_Pnt q1 = BRep_Tool::Pnt(v1), q2 = BRep_Tool::Pnt(v2);
        if ((q1.Distance(A) < 1e-7 && q2.Distance(B) < 1e-7) ||
            (q1.Distance(B) < 1e-7 && q2.Distance(A) < 1e-7)) {
            abEdge = TopoDS::Edge(ex.Current());
            break;
        }
    }
    if (abEdge.IsNull()) return false;
    out.solid = sweepProfile(poly.Wire(), abEdge, sweep, out.blendTemplate);
    return !out.solid.IsNull();
}

// Hip-miter the fill prisms: a prism ends in a FLAT vertical cap, which at a
// corner punches straight past the neighbouring chamfer's slope instead of
// meeting it in a hip. Real blends meet along the intersection line of their
// two slope planes — so clip each prism by (a) the slope plane of any SIBLING
// prism whose span shares the corner vertex (multi-edge fill in one op), and
// (b) the plane of any EXISTING bevel face on the body touching a span end
// (the neighbour was chamfered in an earlier step — Steve's case). The clip
// plane only bites near the corner: away from it the neighbour's slope rises
// far above the prism, so the keep-side (chosen by the prism's own centroid)
// contains everything else. Clip failures degrade to the flat cap.
void hipClipFillTools(const TopoDS_Shape& body,
                      const std::vector<const Group*>& groups,
                      std::vector<Tool>& tools) {
    auto linePt = [](const Group& g, double t) {
        return g.rep.line.Location().Translated(
            gp_Vec(g.rep.line.Direction()) * t);
    };
    for (size_t i = 0; i < tools.size(); ++i) {
        const Group& g = *groups[i];
        const gp_Pnt ends[2] = { linePt(g, g.tmin), linePt(g, g.tmax) };
        std::vector<gp_Pln> clips;
        // (a) sibling prisms meeting this one at a shared corner vertex.
        for (size_t j = 0; j < tools.size(); ++j) {
            if (j == i) continue;
            const Group& h = *groups[j];
            const gp_Pnt hEnds[2] = { linePt(h, h.tmin), linePt(h, h.tmax) };
            bool touch = false;
            for (const auto& a : ends)
                for (const auto& b : hEnds)
                    if (a.Distance(b) < 1e-3) touch = true;
            if (touch)
                clips.push_back(
                    BRepAdaptor_Surface(tools[j].blendTemplate).Plane());
        }
        // (b) existing bevel faces on the body at a span end: planar, inclined
        // to BOTH parent faces (a wall or floor never qualifies), touching the
        // end point.
        for (TopExp_Explorer fx(body, TopAbs_FACE); fx.More(); fx.Next()) {
            const TopoDS_Face f = TopoDS::Face(fx.Current());
            BRepAdaptor_Surface s(f);
            if (s.GetType() != GeomAbs_Plane) continue;
            const gp_Pln pl = s.Plane();
            const gp_Dir n = pl.Axis().Direction();
            // A bevel is INCLINED to at least one of this corner's parent
            // faces. A corner-mate's bevel is typically PERPENDICULAR to our
            // wall (it belongs to the other wall) — requiring inclination to
            // both parents would skip exactly the face we're mitering
            // against. Parents themselves (dot ≈ 1) and plain walls (both
            // dots ≈ 0 or 1) still never qualify.
            const double dr = std::abs(n.Dot(g.rep.nRef));
            const double doth = std::abs(n.Dot(g.rep.nOther));
            const bool inclR = (dr > 0.05 && dr < 0.95);
            const bool inclO = (doth > 0.05 && doth < 0.95);
            if (!inclR && !inclO) continue;
            bool nearEnd = false;
            for (const auto& p : ends) {
                try {
                    BRepExtrema_DistShapeShape dss(
                        BRepBuilderAPI_MakeVertex(p).Vertex(), f);
                    if (dss.IsDone() && dss.Value() < 1.0) nearEnd = true;
                } catch (...) {}
            }
            if (nearEnd) clips.push_back(pl);
        }
        // Apply: subtract the half-space on the far side of each clip plane.
        for (const gp_Pln& pl : clips) {
            try {
                GProp_GProps gc;
                BRepGProp::VolumeProperties(tools[i].solid, gc);
                const gp_Pnt keep = gc.CentreOfMass();
                const gp_Dir n = pl.Axis().Direction();
                const double sd =
                    gp_Vec(pl.Location(), keep).Dot(gp_Vec(n));
                if (std::abs(sd) < 1e-9) continue;
                const gp_Pnt discard =
                    keep.Translated(gp_Vec(n) * (-2.0 * sd));
                TopoDS_Face plane = BRepBuilderAPI_MakeFace(pl).Face();
                TopoDS_Shape half =
                    BRepPrimAPI_MakeHalfSpace(plane, discard).Solid();
                BRepAlgoAPI_Cut cut(tools[i].solid, half);
                if (!cut.IsDone()) continue;
                TopoDS_Shape clipped = cut.Shape();
                if (clipped.IsNull()) continue;
                GProp_GProps gv;
                BRepGProp::VolumeProperties(clipped, gv);
                if (gv.Mass() < 1e-9) continue; // clip ate the whole ramp
                tools[i].solid = clipped;
                BC_DBG("[bc] fill: hip-clipped tool %zu (vol %.1f -> %.1f)\n",
                       i, gc.Mass(), gv.Mass());
            } catch (...) {}
        }
    }
}

// Fuse the ramp(s) onto the body, then re-pierce every void whose outline
// (an inner wire of one of the corner's faces) the ramp roofed over. Boss
// outlines — inner wires with material ABOVE the face — are left alone.
bool applyFill(const TopoDS_Shape& body, const std::vector<Tool>& tools,
               const std::vector<const Group*>& groups, double maxSetback,
               topo::GenerationLedger& ledger, TopoDS_Shape& outShape,
               std::vector<TopoDS_Shape>& outBlendFaces) {
    double toolVol = 0.0;
    TopoDS_Shape res = body;
    for (const auto& t : tools) {
        GProp_GProps gt;
        BRepGProp::VolumeProperties(t.solid, gt);
        toolVol += gt.Mass();
        BRepAlgoAPI_Fuse fuse(res, t.solid);
        if (!fuse.IsDone()) { BC_DBG("[bc] fill: fuse not done\n"); return false; }
        res = fuse.Shape();
        if (res.IsNull()) { BC_DBG("[bc] fill: fuse null\n"); return false; }
        ledger.capture(fuse, body, TopAbs_EDGE);
        ledger.captureAdd(fuse, body, TopAbs_FACE);
    }

    // Re-pierce: for each inner wire of each corner face, if it outlines a
    // VOID the ramp actually roofed over, extrude the outline through the
    // ramp height and subtract — the hole punches through the new ramp just
    // as a feature cut after the chamfer would have. Two guards keep this
    // surgical: the outline's bbox must intersect a ramp's bbox (a wire on
    // the far side of the part is none of our business), and the region just
    // inside the outline must be EMPTY above the face all around its rim —
    // a centroid-only probe misread a screw boss's ring footprint (probe
    // fell down the boss's own bore) as a void and decapitated the boss.
    Bnd_Box toolBox;
    for (const auto& t : tools) BRepBndLib::Add(t.solid, toolBox);
    toolBox.Enlarge(0.1);
    for (const Group* g : groups) {
        for (const TopoDS_Face* fp : {&g->rep.fRef, &g->rep.fOther}) {
            const TopoDS_Face& f = *fp;
            BRepAdaptor_Surface surf(f);
            if (surf.GetType() != GeomAbs_Plane) continue;
            gp_Dir n;
            if (!outwardNormal(f, n)) continue;
            const TopoDS_Wire outer = BRepTools::OuterWire(f);
            for (TopExp_Explorer wx(f, TopAbs_WIRE); wx.More(); wx.Next()) {
                if (wx.Current().IsSame(outer)) continue;
                TopoDS_Wire w = TopoDS::Wire(wx.Current());
                Bnd_Box wb;
                BRepBndLib::Add(w, wb);
                if (toolBox.IsOut(wb)) continue; // ramp never reaches it
                // Face bounded by the void outline, on the face's plane.
                BRepBuilderAPI_MakeFace mfw(surf.Plane(), w, Standard_True);
                if (!mfw.IsDone()) continue;
                GProp_GProps gw;
                BRepGProp::SurfaceProperties(mfw.Face(), gw);
                if (gw.Mass() < 1e-9) continue;
                const gp_Pnt centroid = gw.CentreOfMass();
                // Void all around? Probe just above the face at points a
                // little inside the rim (plus the centroid) — ANY material
                // hit means a boss lives inside this outline; keep it.
                bool boss = false;
                {
                    auto probeIn = [&](const gp_Pnt& q) {
                        BRepClass3d_SolidClassifier sc(
                            body, q.Translated(gp_Vec(n) * 0.1), 1e-7);
                        return sc.State() == TopAbs_IN;
                    };
                    if (probeIn(centroid)) boss = true;
                    for (TopExp_Explorer exw(w, TopAbs_EDGE);
                         !boss && exw.More(); exw.Next()) {
                        BRepAdaptor_Curve wc(TopoDS::Edge(exw.Current()));
                        gp_Pnt m = wc.Value(
                            (wc.FirstParameter() + wc.LastParameter()) * 0.5);
                        gp_Vec toC(m, centroid);
                        if (toC.Magnitude() < 1e-9) continue;
                        toC.Normalize();
                        if (probeIn(m.Translated(
                                toC * std::min(0.2, m.Distance(centroid)))))
                            boss = true;
                    }
                }
                if (boss) continue;
                // Start the pierce slightly BELOW the face: the ramp's apex
                // nudge dips a hair under the face plane, and over a void
                // that sliver would otherwise survive as a floating skin.
                const double down = 0.05 * maxSetback + 0.01;
                gp_Trsf shift;
                shift.SetTranslation(gp_Vec(n) * (-down));
                TopoDS_Shape base =
                    BRepBuilderAPI_Transform(mfw.Face(), shift, true).Shape();
                BRepPrimAPI_MakePrism pierce(
                    base, gp_Vec(n) * (maxSetback + 1.0 + down));
                if (!pierce.IsDone()) continue;
                BRepAlgoAPI_Cut cut(res, pierce.Shape());
                if (!cut.IsDone()) { BC_DBG("[bc] fill: pierce cut not done\n"); return false; }
                TopoDS_Shape s = cut.Shape();
                if (s.IsNull()) { BC_DBG("[bc] fill: pierce cut null\n"); return false; }
                res = s;
            }
        }
    }

    // Must have ADDED material, no more than the ramps themselves, and sound.
    GProp_GProps gin, gout;
    BRepGProp::VolumeProperties(body, gin);
    BRepGProp::VolumeProperties(res, gout);
    BC_DBG("[bc] fill: vol in=%.1f out=%.1f toolVol=%.1f valid=%d\n",
           gin.Mass(), gout.Mass(), toolVol,
           (int)BRepCheck_Analyzer(res).IsValid());
    if (gout.Mass() <= gin.Mass() + 1e-9 ||
        gout.Mass() > gin.Mass() + toolVol + 1e-3)
        return false;
    if (!BRepCheck_Analyzer(res).IsValid()) return false;

    // The ramp's slanted faces on the result.
    for (const auto& t : tools) {
        bool found = false;
        for (TopExp_Explorer ex(res, TopAbs_FACE); ex.More(); ex.Next()) {
            if (sameSupportSurface(t.blendTemplate, TopoDS::Face(ex.Current()))) {
                outBlendFaces.push_back(ex.Current());
                found = true;
            }
        }
        if (!found) { BC_DBG("[bc] fill: no blend face for a tool\n"); return false; }
    }

    outShape = res;
    return true;
}

} // namespace

bool cutChamfer(const TopoDS_Shape& body,
                const std::vector<TopoDS_Edge>& edges,
                double dRef, double dOther,
                const TopoDS_Face& refFace,
                topo::GenerationLedger& ledger,
                TopoDS_Shape& outShape,
                std::vector<TopoDS_Shape>& outBlendFaces) {
    outShape.Nullify();
    outBlendFaces.clear();
    if (body.IsNull() || edges.empty() || dRef <= 0.0 || dOther <= 0.0)
        return false;
    try {
        const bool asymmetric = std::abs(dRef - dOther) > 1e-12;
        gp_Pln refPln;
        bool hasRefPln = false;
        if (!refFace.IsNull()) {
            BRepAdaptor_Surface rs(refFace);
            if (rs.GetType() != GeomAbs_Plane) {
                if (asymmetric) return false; // can't aim dRef reliably
            } else {
                refPln = rs.Plane();
                hasRefPln = true;
            }
        }

        std::vector<Group> groups;
        if (!buildGroups(body, edges, hasRefPln ? &refPln : nullptr,
                         0.1 * std::min(dRef, dOther), asymmetric,
                         /*wantConcave=*/false, groups))
            return false;

        std::vector<Tool> tools;
        for (const auto& g : groups) {
            Tool t;
            if (!makeChamferTool(g, dRef, dOther, t)) return false;
            tools.push_back(t);
        }
        return applyCut(body, tools, ledger, outShape, outBlendFaces);
    } catch (...) {
        return false;
    }
}

bool fillChamfer(const TopoDS_Shape& body,
                 const std::vector<TopoDS_Edge>& edges,
                 double dRef, double dOther,
                 const TopoDS_Face& refFace,
                 topo::GenerationLedger& ledger,
                 TopoDS_Shape& outShape,
                 std::vector<TopoDS_Shape>& outBlendFaces) {
    outShape.Nullify();
    outBlendFaces.clear();
    if (body.IsNull() || edges.empty() || dRef <= 0.0 || dOther <= 0.0)
        return false;
    try {
        const bool asymmetric = std::abs(dRef - dOther) > 1e-12;
        gp_Pln refPln;
        bool hasRefPln = false;
        if (!refFace.IsNull()) {
            BRepAdaptor_Surface rs(refFace);
            if (rs.GetType() != GeomAbs_Plane) {
                if (asymmetric) return false;
            } else {
                refPln = rs.Plane();
                hasRefPln = true;
            }
        }

        std::vector<Group> groups;
        if (!buildGroups(body, edges, hasRefPln ? &refPln : nullptr,
                         0.1 * std::min(dRef, dOther), asymmetric,
                         /*wantConcave=*/true, groups))
            return false;

        std::vector<Tool> tools;
        std::vector<const Group*> groupPtrs;
        for (const auto& g : groups) {
            Tool t;
            if (!makeFillTool(g, dRef, dOther, t)) return false;
            tools.push_back(t);
            groupPtrs.push_back(&g);
        }
        hipClipFillTools(body, groupPtrs, tools);
        return applyFill(body, tools, groupPtrs, std::max(dRef, dOther),
                         ledger, outShape, outBlendFaces);
    } catch (...) {
        return false;
    }
}

bool cutFillet(const TopoDS_Shape& body,
               const std::vector<TopoDS_Edge>& edges, double radius,
               topo::GenerationLedger& ledger, TopoDS_Shape& outShape,
               std::vector<TopoDS_Shape>& outBlendFaces) {
    outShape.Nullify();
    outBlendFaces.clear();
    if (body.IsNull() || edges.empty() || radius <= 0.0) return false;
    try {
        std::vector<Group> groups;
        if (!buildGroups(body, edges, nullptr, 0.1 * radius, false,
                         /*wantConcave=*/false, groups))
            return false;

        std::vector<Tool> tools;
        for (const auto& g : groups) {
            Tool t;
            if (!makeFilletTool(g, radius, t)) return false;
            tools.push_back(t);
        }
        return applyCut(body, tools, ledger, outShape, outBlendFaces);
    } catch (...) {
        return false;
    }
}

} // namespace blendcut
} // namespace materializr
