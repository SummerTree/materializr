// MoveHoleOp's two new modes: Tilt (pin the far rim, move the near one) and
// EdgeMove (drag one straight side of the near rim, neighbours follow).
//
// Verified by POINT CLASSIFICATION rather than by hunting for the rim faces.
// The earlier probe tried to find "the wire on the top face" by matching plane
// Z, got the void's silhouette instead, and sent me chasing three fixes for a
// measurement bug. Asking "is this point inside the solid?" can't be fooled the
// same way: it is the actual question — did material move or not.
//
// Block 40x40x20, Ø10 hole on the axis at (20,20). The move is 12 mm so the old
// and new openings are DISJOINT (x[15..25] vs x[27..37]) — at 4 mm they overlap
// and "the old spot is now solid" is simply false, which cost me a debugging
// round when the assertion failed for the right reason.
#include <gtest/gtest.h>

#include "core/Document.h"
#include "modeling/MoveHoleOp.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <string>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cstdio>

using namespace materializr;

namespace {

double volumeOf(const TopoDS_Shape& s) {
    GProp_GProps g; BRepGProp::VolumeProperties(s, g); return g.Mass();
}

bool insideSolid(const TopoDS_Shape& s, double x, double y, double z) {
    BRepClass3d_SolidClassifier cls(s, gp_Pnt(x, y, z), 1e-6);
    return cls.State() == TopAbs_IN;
}

// The hole's cylindrical wall — the face a user would click.
TopoDS_Face holeWall(const TopoDS_Shape& s) {
    for (TopExp_Explorer fx(s, TopAbs_FACE); fx.More(); fx.Next()) {
        const TopoDS_Face& f = TopoDS::Face(fx.Current());
        BRepAdaptor_Surface surf(f);
        if (surf.GetType() == GeomAbs_Cylinder) return f;
    }
    return {};
}

struct Holed {
    Document doc;
    int bodyId = -1;
    TopoDS_Face wall;
    Holed() {
        TopoDS_Shape block = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 40, 40, 20).Shape();
        gp_Trsf at; at.SetTranslation(gp_Vec(20, 20, -5));
        TopoDS_Shape drill = BRepBuilderAPI_Transform(
            BRepPrimAPI_MakeCylinder(5.0, 30.0).Shape(), at, true).Shape();
        TopoDS_Shape holed = BRepAlgoAPI_Cut(block, drill).Shape();
        bodyId = doc.addBody(holed, "block");
        wall = holeWall(holed);
    }
};

} // namespace

TEST(TiltOp, TopRimMovesAndBottomRimStaysPut) {
    Holed f;
    ASSERT_FALSE(f.wall.IsNull()) << "no cylindrical wall to seed from";
    const double before = volumeOf(f.doc.getBody(f.bodyId));

    MoveHoleOp op;
    op.setBody(f.bodyId);
    op.setSeedWall(f.wall);
    op.setMode(MoveHoleOp::Mode::Tilt);
    op.setMoveVector(gp_Vec(12.0, 0.0, 0.0));
    ASSERT_TRUE(op.execute(f.doc)) << "tilt refused";

    const TopoDS_Shape out = f.doc.getBody(f.bodyId);
    ASSERT_FALSE(out.IsNull());
    const bool valid = BRepCheck_Analyzer(out).IsValid();
    std::printf("  tilted: valid=%d vol=%.1f (was %.1f)\n",
                (int)valid, volumeOf(out), before);
    EXPECT_TRUE(valid);

    // An oblique bore of the same section removes the same material.
    EXPECT_NEAR(volumeOf(out), before, 1.0);

    // Ø10 hole moved 12 mm: the old opening spans x[15..25], the new x[27..37].
    // Disjoint, so these three points genuinely discriminate. (At a 4 mm move
    // they overlap and "the old spot is solid" is simply not true.)
    const bool botStillHole = !insideSolid(out, 20, 20, 1);
    const bool topOldSolid  =  insideSolid(out, 20, 20, 19);
    const bool topNewHole   = !insideSolid(out, 32, 20, 19);
    std::printf("  bottom (20,20,1) still hole : %d\n"
                "  top old (20,20,19) now solid: %d\n"
                "  top new (32,20,19) now hole : %d\n",
                (int)botStillHole, (int)topOldSolid, (int)topNewHole);

    EXPECT_TRUE(botStillHole) << "the pinned rim moved — the bottom closed up";
    EXPECT_TRUE(topOldSolid)  << "the near rim did not move — old spot still open";
    EXPECT_TRUE(topNewHole)   << "no opening where the rim was moved to";
}

// Slide must keep working exactly as before — the tilt shares its recipe.
TEST(TiltOp, SlideStillMovesBothRims) {
    Holed f;
    MoveHoleOp op;
    op.setBody(f.bodyId);
    op.setSeedWall(f.wall);
    op.setMode(MoveHoleOp::Mode::Slide);
    op.setMoveVector(gp_Vec(12.0, 0.0, 0.0));
    ASSERT_TRUE(op.execute(f.doc));

    const TopoDS_Shape out = f.doc.getBody(f.bodyId);
    EXPECT_TRUE(BRepCheck_Analyzer(out).IsValid());
    // Both ends moved: the whole bore is at x=24 now.
    EXPECT_FALSE(insideSolid(out, 32, 20, 1))  << "bottom did not follow";
    EXPECT_FALSE(insideSolid(out, 32, 20, 19)) << "top did not follow";
    EXPECT_TRUE(insideSolid(out, 20, 20, 10))  << "old bore not filled";
}

// ── Non-round holes ─────────────────────────────────────────────────────────
// The loft never inspects the profile, so in principle a square hole tilts by
// the same code. The risk is vertex CORRESPONDENCE: ThruSections pairs the two
// wires up, and if it starts them at different corners the loft twists — the
// bore would spiral instead of leaning. A square is the cheapest thing that
// would expose it (a twist shows as a wrong volume and a wrong opening).
namespace {

struct SquareHoled {
    Document doc;
    int bodyId = -1;
    TopoDS_Face wall;
    // `prismSide` mm square hole through a 40x40x20 block, centred at (20,20).
    explicit SquareHoled(double prismSide = 10.0) {
        TopoDS_Shape block = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 40, 40, 20).Shape();
        const double h = prismSide * 0.5;
        TopoDS_Shape cutter = BRepPrimAPI_MakeBox(
            gp_Pnt(20 - h, 20 - h, -5), prismSide, prismSide, 30).Shape();
        TopoDS_Shape holed = BRepAlgoAPI_Cut(block, cutter).Shape();
        bodyId = doc.addBody(holed, "block");
        // Any of the four flats is a valid seed; take the first planar face
        // whose plane is vertical and passes near the hole.
        for (TopExp_Explorer fx(holed, TopAbs_FACE); fx.More(); fx.Next()) {
            const TopoDS_Face& f = TopoDS::Face(fx.Current());
            BRepAdaptor_Surface s(f);
            if (s.GetType() != GeomAbs_Plane) continue;
            const gp_Pnt loc = s.Plane().Location();
            const gp_Dir n = s.Plane().Axis().Direction();
            if (std::abs(n.Z()) > 1e-6) continue;                 // not a side wall
            if (loc.X() < 1.0 || loc.X() > 39.0) continue;        // block's own side
            if (loc.Y() < 1.0 || loc.Y() > 39.0) continue;
            wall = f;
            break;
        }
    }
};

} // namespace

TEST(TiltOp, SquareHoleTilts) {
    SquareHoled f;
    ASSERT_FALSE(f.wall.IsNull()) << "no square-hole wall found to seed from";
    const double before = volumeOf(f.doc.getBody(f.bodyId));

    MoveHoleOp op;
    op.setBody(f.bodyId);
    op.setSeedWall(f.wall);
    op.setMode(MoveHoleOp::Mode::Tilt);
    op.setMoveVector(gp_Vec(12.0, 0.0, 0.0));
    const bool ok = op.execute(f.doc);
    std::printf("  square tilt executed: %d\n", (int)ok);
    ASSERT_TRUE(ok) << "square hole tilt refused";

    const TopoDS_Shape out = f.doc.getBody(f.bodyId);
    const bool valid = BRepCheck_Analyzer(out).IsValid();
    std::printf("  square tilted: valid=%d vol=%.1f (was %.1f)\n",
                (int)valid, volumeOf(out), before);
    EXPECT_TRUE(valid);
    // A twisted loft changes the swept volume; an honest lean does not.
    EXPECT_NEAR(volumeOf(out), before, 1.0)
        << "volume changed — the loft probably twisted between corners";

    // Square hole 10 wide moved 12: old opening x[15..25], new x[27..37].
    EXPECT_FALSE(insideSolid(out, 20, 20, 1))  << "pinned rim moved";
    EXPECT_TRUE(insideSolid(out, 20, 20, 19))  << "near rim did not move";
    EXPECT_FALSE(insideSolid(out, 32, 20, 19)) << "no opening at the new spot";
}

// ── EdgeMove: drag one straight side, neighbours follow ─────────────────────
namespace {
// The rim edge nearest a given x on the top face — what a user would grab.
TopoDS_Edge topRimEdgeNear(const TopoDS_Shape& s, double wantX) {
    TopoDS_Edge best; double bestD = 1e30;
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
        BRepAdaptor_Curve c(e);
        if (c.GetType() != GeomAbs_Line) continue;
        gp_Pnt mid = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
        if (std::abs(mid.Z() - 20.0) > 1e-6) continue;   // on the top face
        if (mid.X() < 10 || mid.X() > 30) continue;      // near the hole
        if (mid.Y() < 10 || mid.Y() > 30) continue;
        const double d = std::abs(mid.X() - wantX);
        if (d < bestD) { bestD = d; best = e; }
    }
    return best;
}
} // namespace

TEST(TiltOp, EdgeMoveWidensASquareHole) {
    SquareHoled f;
    const TopoDS_Shape in = f.doc.getBody(f.bodyId);
    const double before = volumeOf(in);
    TopoDS_Edge grabbed = topRimEdgeNear(in, 25.0);   // the +x side of the rim
    ASSERT_FALSE(grabbed.IsNull()) << "no rim edge found to grab";

    MoveHoleOp op;
    op.setBody(f.bodyId);
    op.setSeedWall(f.wall);
    op.setMode(MoveHoleOp::Mode::EdgeMove);
    op.setRimEdge(grabbed);
    op.setMoveVector(gp_Vec(5.0, 0.0, 0.0));      // widen by 5 mm at the top
    ASSERT_TRUE(op.execute(f.doc)) << "edge move refused";

    const TopoDS_Shape out = f.doc.getBody(f.bodyId);
    const bool valid = BRepCheck_Analyzer(out).IsValid();
    std::printf("  edge move: valid=%d vol=%.1f (was %.1f)\n",
                (int)valid, volumeOf(out), before);
    EXPECT_TRUE(valid);
    // More material removed: the opening got wider at the top only.
    EXPECT_LT(volumeOf(out), before - 100.0) << "nothing was removed";

    // Bottom keeps its 10 mm square; the top now reaches past x=25.
    EXPECT_TRUE(insideSolid(out, 28, 20, 1))   << "bottom widened — it shouldn't";
    EXPECT_FALSE(insideSolid(out, 28, 20, 19)) << "top did not widen";
}

TEST(TiltOp, EdgeMoveRefusesACurvedRim) {
    Holed f;   // round hole: every side is an arc
    const TopoDS_Shape in = f.doc.getBody(f.bodyId);
    TopoDS_Wire rim, exitRim;
    TopoDS_Shape v; gp_Vec n; bool pocket = false;
    ASSERT_TRUE(MoveHoleOp::buildVoid(in, f.wall, v, n, pocket, &rim, &exitRim));

    TopoDS_Edge anyEdge;
    for (TopExp_Explorer ex(rim, TopAbs_EDGE); ex.More(); ex.Next()) {
        anyEdge = TopoDS::Edge(ex.Current());
        break;
    }
    TopoDS_Wire edited;
    std::string why;
    const bool ok = MoveHoleOp::editRimWire(rim, anyEdge, gp_Vec(3, 0, 0),
                                            edited, &why);
    std::printf("  curved rim refused=%d: %s\n", (int)!ok, why.c_str());
    EXPECT_FALSE(ok) << "a round hole's rim should be refused, not guessed at";
    EXPECT_NE(why.find("curved side"), std::string::npos);
}

TEST(TiltOp, EdgeMoveRefusesAFoldThroughItself) {
    SquareHoled f;
    const TopoDS_Shape in = f.doc.getBody(f.bodyId);
    TopoDS_Wire rim, exitRim;
    TopoDS_Shape v; gp_Vec n; bool pocket = false;
    ASSERT_TRUE(MoveHoleOp::buildVoid(in, f.wall, v, n, pocket, &rim, &exitRim));
    TopoDS_Edge grabbed = topRimEdgeNear(in, 25.0);
    ASSERT_FALSE(grabbed.IsNull());

    // Drag the +x side 40 mm in -x: straight through the opposite side.
    TopoDS_Wire edited;
    std::string why;
    const bool ok = MoveHoleOp::editRimWire(rim, grabbed, gp_Vec(-40, 0, 0),
                                            edited, &why);
    std::printf("  fold refused=%d: %s\n", (int)!ok, why.c_str());
    EXPECT_FALSE(ok) << "folding the profile inside out should be refused";
}
