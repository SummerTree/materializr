// Scale Face past 100%: which mode can actually GROW a face?
//
// Steve, 2026-08-04: "scale only makes a face smaller, not larger." Both the
// panel and the drag allow 5–200%, so the UI implies growth works in either
// mode. It doesn't: Pinch is Common(body, frustum) — an intersection, which
// can only ever remove material — while Extend is Fuse(body, tip loft), which
// adds it. These tests pin that down so the UI can be honest about it.
#include <gtest/gtest.h>
#include "core/Document.h"
#include "modeling/ScaleFaceOp.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cstdio>

namespace {

double volumeOf(const TopoDS_Shape& s) {
    GProp_GProps g; BRepGProp::VolumeProperties(s, g); return g.Mass();
}

// The +Z top face of a box at the origin.
TopoDS_Face topFace(const TopoDS_Shape& s, double z) {
    for (TopExp_Explorer fx(s, TopAbs_FACE); fx.More(); fx.Next()) {
        const TopoDS_Face& f = TopoDS::Face(fx.Current());
        GProp_GProps g; BRepGProp::SurfaceProperties(f, g);
        if (std::abs(g.CentreOfMass().Z() - z) < 1e-6) return f;
    }
    return {};
}

struct Boxed {
    Document doc;
    int bodyId = -1;
    TopoDS_Face top;
    double vol0 = 0.0;
    Boxed() {
        TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
        bodyId = doc.addBody(box, "box");
        top = topFace(box, 20.0);
        vol0 = volumeOf(box);
    }
};

} // namespace

// Shrinking is what Pinch is for, and it works.
TEST(ScaleFaceRange, PinchBelow100Shrinks) {
    Boxed f;
    ASSERT_FALSE(f.top.IsNull());
    ScaleFaceOp op;
    op.setBody(f.bodyId);
    op.setFace(f.top);
    op.setMode(ScaleFaceOp::Mode::Pinch);
    op.setScalePercent(50.0);
    op.setLength(20.0);           // full depth: sides follow from the base
    ASSERT_TRUE(op.execute(f.doc));
    const double v = volumeOf(f.doc.getBody(f.bodyId));
    std::printf("  pinch 50%%: %.1f -> %.1f\n", f.vol0, v);
    EXPECT_LT(v, f.vol0) << "pinch below 100% should remove material";
}

// THE REPORT: Pinch past 100% cannot grow — Common() can only subtract, so the
// oversized frustum clips to the body and the result is the body back.
TEST(ScaleFaceRange, PinchAbove100CannotGrow) {
    Boxed f;
    ScaleFaceOp op;
    op.setBody(f.bodyId);
    op.setFace(f.top);
    op.setMode(ScaleFaceOp::Mode::Pinch);
    op.setScalePercent(150.0);
    op.setLength(20.0);
    const bool ran = op.execute(f.doc);
    const double v = ran ? volumeOf(f.doc.getBody(f.bodyId)) : f.vol0;
    std::printf("  pinch 150%%: ran=%d  %.1f -> %.1f\n", (int)ran, f.vol0, v);
    EXPECT_LE(v, f.vol0 + 1e-6)
        << "pinch grew the body — then the UI cap added for this is wrong";
}

// Extend DOES grow: it fuses a tip loft on, so >100% adds material.
TEST(ScaleFaceRange, ExtendAbove100Grows) {
    Boxed f;
    ScaleFaceOp op;
    op.setBody(f.bodyId);
    op.setFace(f.top);
    op.setMode(ScaleFaceOp::Mode::Extend);
    op.setScalePercent(150.0);
    op.setLength(10.0);
    ASSERT_TRUE(op.execute(f.doc)) << "extend refused";
    const double v = volumeOf(f.doc.getBody(f.bodyId));
    std::printf("  extend 150%%: %.1f -> %.1f\n", f.vol0, v);
    EXPECT_GT(v, f.vol0) << "extend past 100% should ADD material";
}
