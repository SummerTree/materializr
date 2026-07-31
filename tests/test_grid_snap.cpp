// Regression: with snap-to-grid on, a click must land ON the snap lattice, and
// the grid the viewport draws must BE that lattice. Steve, 2026-07-31, at a
// 0.1 mm grid: "I cannot draw a line on that snap grid" — the grid appeared to
// wander arbitrarily. Two independent causes, one test each:
//
//   1. SketchTool::snap() — directional inference guides (perpendicular- and
//      parallel-to-previous, axis-from-point, angle snap) returned a point on
//      their guide LINE, grid-aligned on the guide's dominant axis only. The
//      free coordinate came out wherever the geometry put it, so placed points
//      drifted off the lattice a few hundredths of a millimetre at a time.
//      A guide that represents CONTACT with existing geometry (landing on an
//      edge) is the deliberate exception and keeps its exact on-edge position:
//      buildWires splits that edge at the contact point, and a point rounded
//      off the edge silently stops closing the region.
//
//   2. Sketch::latticeAnchor — the anchor the drawn grid is laid out from.
//      Rounding world XYZ and projecting onto the sketch plane (what shipped)
//      is NOT a lattice point in-plane: the drawn grid sat 10–50% of a cell
//      off the lattice clicks land on, so no click could ever appear to sit on
//      a drawn line.

#include "modeling/Sketch.h"
#include "modeling/SketchTool.h"
#include "modeling/SvgImport.h"
#include "modeling/TextSketchOp.h"

#include <gtest/gtest.h>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <glm/glm.hpp>
#include <cmath>
#include <set>
#include <string>

// SketchTool's Text / SVG stamp paths are not part of materializr_core (they
// pull in font rendering); stub them so this links.
namespace materializr {
int SvgImport::place(Sketch*, const SvgPaths&, glm::vec2, float, float) { return 0; }
int TextSketch::generate(Sketch*, const std::string&, const std::string&,
                         glm::vec2, float, float) { return 0; }
}

using materializr::InferenceGuide;
using materializr::Sketch;
using materializr::SketchTool;
using materializr::SketchToolMode;

namespace {

// How far `v` sits from the nearest multiple of `step`.
double offLattice(double v, double step) {
    return std::fabs(v - std::round(v / step) * step);
}

} // namespace

// ─── 1. clicks land on the lattice ───────────────────────────────────────────
TEST(GridSnap, PlacedPointsLandOnTheLattice) {
    const float step = 0.1f;

    Sketch sk;
    SketchTool tool;
    tool.setSketch(&sk);
    tool.setGridStep(step);
    tool.setSnapToGridEnabled(true);
    // Full is the shipping default and the tier that fires the most guides.
    tool.setInferenceLevel(SketchTool::InferenceLevel::Full);

    // Existing geometry, so the inference engine has something to grab: a
    // closed frame plus a circle, the way a real working sketch looks.
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({20.0f, 0.0f});
    int c = sk.addPoint({20.0f, 12.0f});
    int d = sk.addPoint({0.0f, 12.0f});
    sk.addLine(a, b); sk.addLine(b, c); sk.addLine(c, d); sk.addLine(d, a);
    int ctr = sk.addPoint({7.3f, 4.6f});
    sk.addCircle(ctr, 2.0);
    const std::set<int> preexisting{a, b, c, d, ctr};

    tool.setMode(SketchToolMode::Line);

    // A chain drawn at deliberately off-lattice cursor positions, routed near
    // the existing geometry so perpendicular / on-edge / axis guides fire.
    const glm::vec2 clicks[] = {
        {3.021f, 2.037f}, {9.114f, 2.052f}, {9.087f, 7.973f},
        {14.962f, 7.941f}, {14.933f, 11.968f}, {2.973f, 11.949f},
    };
    for (glm::vec2 p : clicks) {
        tool.onMouseMove(p);   // the guides are computed on hover
        tool.onMouseDown(p);
        tool.onMouseUp(p);
    }

    int placed = 0;
    for (const auto& pt : sk.getPoints()) {
        if (preexisting.count(pt.id)) continue;
        ++placed;
        EXPECT_LE(offLattice(pt.pos.x, step), 1e-4)
            << "point " << pt.id << " x=" << pt.pos.x << " is off the "
            << step << "mm lattice";
        EXPECT_LE(offLattice(pt.pos.y, step), 1e-4)
            << "point " << pt.id << " y=" << pt.pos.y << " is off the "
            << step << "mm lattice";
    }
    EXPECT_GT(placed, 0) << "the chain committed no points — test drew nothing";
}

// A point landing ON an existing edge stays on that edge. The lattice may
// choose WHERE along the edge, but must not lift the point off it, or the
// region walker can no longer route a loop through the split.
TEST(GridSnap, ContactWithAnEdgeStaysOnTheEdge) {
    const float step = 0.1f;

    Sketch sk;
    SketchTool tool;
    tool.setSketch(&sk);
    tool.setGridStep(step);
    tool.setSnapToGridEnabled(true);
    tool.setInferenceLevel(SketchTool::InferenceLevel::Full);

    // A single diagonal edge — a lattice point almost never sits exactly on
    // one, so this is the case where the two demands genuinely conflict.
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({10.0f, 7.0f});
    sk.addLine(a, b);

    tool.setMode(SketchToolMode::Line);
    // Aim just off the middle of the diagonal.
    const glm::vec2 target(5.02f, 3.53f);
    tool.onMouseMove(target);
    tool.onMouseDown(target);
    tool.onMouseUp(target);

    bool sawContact = false;
    for (const auto& pt : sk.getPoints()) {
        if (pt.id == a || pt.id == b) continue;
        // Distance from the point to the infinite line through a→b.
        glm::vec2 ab(10.0f, 7.0f);
        float len = glm::length(ab);
        float cross = std::fabs(ab.x * pt.pos.y - ab.y * pt.pos.x) / len;
        if (cross < 1e-3f) sawContact = true;
    }
    EXPECT_TRUE(sawContact)
        << "the point placed on the diagonal is no longer on it";
}

// ─── 2. the drawn grid is the same lattice ───────────────────────────────────
TEST(GridSnap, LatticeAnchorSitsOnTheSnapLattice) {
    struct Case { const char* name; gp_Ax3 ax; gp_Pnt lookAt; };
    // A sketch started on a face gets whatever plane origin the geometry has —
    // fractional world coordinates are the norm, not the exception.
    const Case cases[] = {
        {"XY plane at a fractional origin",
         gp_Ax3(gp_Pnt(12.37, 5.02, 3.5), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)),
         gp_Pnt(20.0, 9.0, 3.5)},
        {"XZ face of a 2mm wall",
         gp_Ax3(gp_Pnt(1.25, 0.0, 7.68), gp_Dir(0, 1, 0), gp_Dir(1, 0, 0)),
         gp_Pnt(30.0, 0.0, 40.0)},
        {"tilted plane",
         gp_Ax3(gp_Pnt(4.44, 2.22, 1.11), gp_Dir(0, 0.6, 0.8), gp_Dir(1, 0, 0)),
         gp_Pnt(10.0, 10.0, 10.0)},
    };

    for (double step : {1.0, 0.1}) {
        for (const Case& cs : cases) {
            const gp_Pln pln(cs.ax);
            const gp_Pnt anchor = Sketch::latticeAnchor(pln, cs.lookAt, step);

            // Express the anchor in the frame snapping rounds in: sketch (u,v)
            // from the plane origin along X/YDirection (see sketchToWorld).
            const gp_Pnt o = cs.ax.Location();
            const gp_Vec rel(o, anchor);
            const double u = rel.Dot(gp_Vec(cs.ax.XDirection()));
            const double v = rel.Dot(gp_Vec(cs.ax.YDirection()));

            EXPECT_LE(offLattice(u, step), 1e-6)
                << cs.name << " @ step " << step << ": u=" << u;
            EXPECT_LE(offLattice(v, step), 1e-6)
                << cs.name << " @ step " << step << ": v=" << v;

            // Still ON the plane, and still where it was asked to be: measured
            // against the target's own projection, it may only move by half a
            // cell along each axis (the anchor doubles as the camera target,
            // so it has to stay put).
            EXPECT_LE(std::fabs(pln.Distance(anchor)), 1e-6) << cs.name;
            const gp_Pnt projected = cs.lookAt.Translated(
                gp_Vec(pln.Axis().Direction()) *
                -gp_Vec(o, cs.lookAt).Dot(gp_Vec(pln.Axis().Direction())));
            EXPECT_LE(anchor.Distance(projected), step * 0.71 + 1e-6)
                << cs.name << " @ step " << step
                << ": anchor wandered further than half a cell";
        }
    }
}
