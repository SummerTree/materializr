// DistancePointLine: perpendicular distance from a point (entityA) to the
// infinite line carried by a sketch line (entityB). Solver drives the point
// and the line's endpoints apart/together along the line normal.

#include "modeling/Sketch.h"
#include "modeling/SketchSolver.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>

using materializr::Constraint;
using materializr::ConstraintType;
using materializr::Sketch;
using materializr::SketchSolver;

namespace {

double pointLineDist(const Sketch& sk, int ptId, int lineId) {
    const auto* p = sk.getPoint(ptId);
    for (const auto& l : sk.getLines()) {
        if (l.id != lineId) continue;
        const auto* a = sk.getPoint(l.startPointId);
        const auto* b = sk.getPoint(l.endPointId);
        glm::vec2 d = b->pos - a->pos;
        glm::vec2 r = p->pos - a->pos;
        return std::abs(d.x * r.y - d.y * r.x) / glm::length(d);
    }
    return -1.0;
}

Constraint makeDPL(int ptId, int lineId, double value) {
    Constraint c{};
    c.id = 0;
    c.type = ConstraintType::DistancePointLine;
    c.entityA = ptId;
    c.entityB = lineId;
    c.value = value;
    return c;
}

} // namespace

TEST(DistancePointLine, ConvergesToTarget) {
    Sketch sk;
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({10.0f, 0.0f});
    int ln = sk.addLine(a, b);
    int p = sk.addPoint({5.0f, 3.0f});
    sk.addConstraint(makeDPL(p, ln, 7.0));

    SketchSolver solver;
    EXPECT_TRUE(solver.solve(sk, 500, 1e-4));
    EXPECT_NEAR(pointLineDist(sk, p, ln), 7.0, 1e-3);
}

TEST(DistancePointLine, DegenerateLineDoesNotNaN) {
    Sketch sk;
    int a = sk.addPoint({2.0f, 2.0f});
    int b = sk.addPoint({2.0f, 2.0f}); // zero-length line
    int ln = sk.addLine(a, b);
    int p = sk.addPoint({5.0f, 3.0f});
    sk.addConstraint(makeDPL(p, ln, 4.0));

    SketchSolver solver;
    solver.solve(sk, 100, 1e-4); // must not crash or NaN, return value unspecified
    for (const auto& pt : sk.getPoints()) {
        EXPECT_TRUE(std::isfinite(pt.pos.x));
        EXPECT_TRUE(std::isfinite(pt.pos.y));
    }
}

TEST(DistancePointLine, MissingEntitiesAreInert) {
    Sketch sk;
    int p = sk.addPoint({1.0f, 1.0f});
    sk.addConstraint(makeDPL(p, 9999, 4.0)); // no such line
    SketchSolver solver;
    solver.solve(sk, 50, 1e-4);
    EXPECT_NEAR(sk.getPoint(p)->pos.x, 1.0f, 1e-6);
    EXPECT_NEAR(sk.getPoint(p)->pos.y, 1.0f, 1e-6);
}

TEST(DistancePointLine, DegeneratePointOnOwnLineStaysBounded) {
    // entityA == the line's own start point is a degenerate constraint that
    // resolveDimension now refuses to produce (see the DimensionResolve
    // rejection tests below), but a constraint can still reach the solver
    // another way — an older project file, or direct injection as here.
    // Before the applyCorrection identity guard, this residual-0 constraint
    // fed a non-zero ~value/2 correction into BOTH the point and the line's
    // endpoints every iteration, which cancelled out for the point (moved
    // back to where it started) but not for the far endpoint, which got
    // flung outward without bound. The guard should leave everything inert.
    Sketch sk;
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({10.0f, 0.0f});
    int ln = sk.addLine(a, b);
    glm::vec2 origA = sk.getPoint(a)->pos;
    glm::vec2 origB = sk.getPoint(b)->pos;
    const double value = 5.0;
    sk.addConstraint(makeDPL(a, ln, value));

    SketchSolver solver;
    solver.solve(sk, 200, 1e-4); // must not crash, NaN, or diverge

    for (const auto& pt : sk.getPoints()) {
        EXPECT_TRUE(std::isfinite(pt.pos.x));
        EXPECT_TRUE(std::isfinite(pt.pos.y));
    }
    double dispA = glm::length(sk.getPoint(a)->pos - origA);
    double dispB = glm::length(sk.getPoint(b)->pos - origB);
    EXPECT_LT(dispA, 10.0 * value);
    EXPECT_LT(dispB, 10.0 * value);
}

#include "core/Document.h"
#include "io/ProjectIO.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>

using materializr::ProjectIO;

namespace {

std::string tmpProjectPath(const char* name) {
    const char* t = std::getenv("TMPDIR");
    std::string dir = t ? t : "/tmp";
    if (!dir.empty() && dir.back() != '/') dir += '/';
    return dir + name;
}

} // namespace

TEST(DimensionPersistence, KLineRoundTripsTypeAndLabelOffsets) {
    Document doc;
    auto sk = std::make_shared<Sketch>();
    int a = sk->addPoint({0.0f, 0.0f});
    int b = sk->addPoint({10.0f, 0.0f});
    int ln = sk->addLine(a, b);
    int p = sk->addPoint({5.0f, 3.0f});
    Constraint c{};
    c.type = ConstraintType::DistancePointLine;
    c.entityA = p;
    c.entityB = ln;
    c.value = 3.0;
    c.labelOffX = 1.5;
    c.labelOffY = -2.25;
    sk->addConstraint(c);
    doc.addSketch(sk, "dim-test");

    std::string path = tmpProjectPath("dim_roundtrip.mzr");
    ASSERT_TRUE(ProjectIO::save(path, doc).success);

    Document loaded;
    ASSERT_TRUE(ProjectIO::load(path, loaded).success);
    std::remove(path.c_str());

    // First (only) sketch in the loaded doc.
    auto ids = loaded.getAllSketchIds();
    ASSERT_EQ(ids.size(), 1u);
    auto lsk = loaded.getSketch(ids[0]);
    ASSERT_TRUE(lsk);
    ASSERT_EQ(lsk->getConstraints().size(), 1u);
    const Constraint& lc = lsk->getConstraints()[0];
    EXPECT_EQ(lc.type, ConstraintType::DistancePointLine);
    EXPECT_DOUBLE_EQ(lc.value, 3.0);
    EXPECT_DOUBLE_EQ(lc.labelOffX, 1.5);
    EXPECT_DOUBLE_EQ(lc.labelOffY, -2.25);
}

TEST(DimensionPersistence, LegacySixFieldKLineDefaultsOffsetsToZero) {
    // Legacy (pre-offset) K lines carry 6 fields; parseSketchBody must
    // default offsets to 0 and still load the constraint.
    std::string body =
        "PLANE 0 0 0 0 0 1 1 0 0 0 1 0\n"
        "POINT_COUNT 2\n"
        "P 1 0 0 0 0\n"
        "P 2 4 0 0 0\n"
        "LINE_COUNT 1\n"
        "L 3 1 2 0 0\n"
        "CIRCLE_COUNT 0\n"
        "ARC_COUNT 0\n"
        "SPLINE_COUNT 0\n"
        "POLYGON_COUNT 0\n"
        "CONSTRAINT_COUNT 1\n"
        "K 4 3 1 2 4 0\n"          // 6 fields, no offsets — ConstraintType 3 = Distance
        "SKETCH_END\n";
    std::istringstream is(body);
    Sketch sk;
    materializr::ProjectIO::parseSketchBody(is, sk, "SKETCH_END");
    ASSERT_EQ(sk.getConstraints().size(), 1u);
    EXPECT_EQ(sk.getConstraints()[0].type, ConstraintType::Distance);
    EXPECT_DOUBLE_EQ(sk.getConstraints()[0].value, 4.0);
    EXPECT_DOUBLE_EQ(sk.getConstraints()[0].labelOffX, 0.0);
    EXPECT_DOUBLE_EQ(sk.getConstraints()[0].labelOffY, 0.0);
}

TEST(DimensionPersistence, WriteSketchBodyPreservesLabelOffsets) {
    // writeSketchBody is used by SketchEditOp for undo/redo snapshots.
    // Verify that label offsets roundtrip through it.
    Sketch sk;
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({10.0f, 0.0f});
    int ln = sk.addLine(a, b);
    int p = sk.addPoint({5.0f, 3.0f});
    Constraint c{};
    c.type = ConstraintType::DistancePointLine;
    c.entityA = p;
    c.entityB = ln;
    c.value = 3.0;
    c.labelOffX = 2.5;
    c.labelOffY = -1.75;
    sk.addConstraint(c);

    // Serialize via writeSketchBody
    std::stringstream ss;
    materializr::ProjectIO::writeSketchBody(ss, sk);

    // Deserialize via parseSketchBody
    Sketch loaded;
    materializr::ProjectIO::parseSketchBody(ss, loaded, "SKETCH_END");

    ASSERT_EQ(loaded.getConstraints().size(), 1u);
    const Constraint& lc = loaded.getConstraints()[0];
    EXPECT_EQ(lc.type, ConstraintType::DistancePointLine);
    EXPECT_DOUBLE_EQ(lc.value, 3.0);
    EXPECT_DOUBLE_EQ(lc.labelOffX, 2.5);
    EXPECT_DOUBLE_EQ(lc.labelOffY, -1.75);
}

#include "modeling/SketchTool.h"

using materializr::DimEntityKind;
using materializr::DimPick;
using materializr::PendingDimension;
using materializr::SketchTool;

namespace {

// 10-unit horizontal line at y=0 and a second line at `deg` degrees from it,
// plus a free point at (5,3). Returns ids via out-params.
struct DimFixture {
    Sketch sk;
    int pA, pB, lnAB;      // horizontal line
    int pC, pD, lnCD;      // rotated line
    int pFree;
    explicit DimFixture(float deg) {
        pA = sk.addPoint({0.0f, 0.0f});
        pB = sk.addPoint({10.0f, 0.0f});
        lnAB = sk.addLine(pA, pB);
        float r = deg * 3.14159265358979f / 180.0f;
        pC = sk.addPoint({0.0f, 5.0f});
        pD = sk.addPoint({10.0f * std::cos(r), 5.0f + 10.0f * std::sin(r)});
        lnCD = sk.addLine(pC, pD);
        pFree = sk.addPoint({5.0f, 3.0f});
    }
};

DimPick pick(DimEntityKind k, int id) { return DimPick{k, id}; }

} // namespace

TEST(DimensionResolve, CircleAloneIsRadius) {
    Sketch sk;
    int c = sk.addPoint({0.0f, 0.0f});
    int ci = sk.addCircle(c, 6.5);
    auto r = SketchTool::resolveDimension(sk, pick(DimEntityKind::Circle, ci), DimPick{});
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.type, ConstraintType::Radius);
    EXPECT_EQ(r.entityA, ci);
    EXPECT_NEAR(r.measured, 6.5, 1e-9);
}

TEST(DimensionResolve, LineAloneIsEndpointDistance) {
    DimFixture f(30.0f);
    auto r = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Line, f.lnAB), DimPick{});
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.type, ConstraintType::Distance);
    EXPECT_EQ(r.entityA, f.pA);
    EXPECT_EQ(r.entityB, f.pB);
    EXPECT_NEAR(r.measured, 10.0, 1e-6);
}

TEST(DimensionResolve, PointPointIsDistance) {
    DimFixture f(30.0f);
    auto r = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Point, f.pA),
                                          pick(DimEntityKind::Point, f.pFree));
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.type, ConstraintType::Distance);
    EXPECT_NEAR(r.measured, std::sqrt(25.0 + 9.0), 1e-6);
}

TEST(DimensionResolve, PointLineEitherOrderIsDistancePointLine) {
    DimFixture f(30.0f);
    auto r1 = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Point, f.pFree),
                                           pick(DimEntityKind::Line, f.lnAB));
    auto r2 = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Line, f.lnAB),
                                           pick(DimEntityKind::Point, f.pFree));
    for (const auto& r : {r1, r2}) {
        ASSERT_TRUE(r.valid);
        EXPECT_EQ(r.type, ConstraintType::DistancePointLine);
        EXPECT_EQ(r.entityA, f.pFree);
        EXPECT_EQ(r.entityB, f.lnAB);
        EXPECT_NEAR(r.measured, 3.0, 1e-6);
    }
}

TEST(DimensionResolve, ParallelLinesGiveDistance_NonParallelGiveAngle) {
    DimFixture par(0.5f);   // inside the 1° parallel threshold
    auto rp = SketchTool::resolveDimension(par.sk, pick(DimEntityKind::Line, par.lnAB),
                                           pick(DimEntityKind::Line, par.lnCD));
    ASSERT_TRUE(rp.valid);
    EXPECT_EQ(rp.type, ConstraintType::DistancePointLine);
    EXPECT_EQ(rp.entityA, par.pC);   // second line's start point
    EXPECT_EQ(rp.entityB, par.lnAB); // measured against the first line

    DimFixture ang(30.0f);
    auto ra = SketchTool::resolveDimension(ang.sk, pick(DimEntityKind::Line, ang.lnAB),
                                           pick(DimEntityKind::Line, ang.lnCD));
    ASSERT_TRUE(ra.valid);
    EXPECT_EQ(ra.type, ConstraintType::Angle);
    EXPECT_EQ(ra.entityA, ang.lnAB);
    EXPECT_EQ(ra.entityB, ang.lnCD);
    EXPECT_NEAR(ra.measured, 30.0 * 3.14159265358979 / 180.0, 1e-4); // signed, B rel A
}

TEST(DimensionResolve, AntiParallelLinesGiveDistance) {
    // 179.5° apart folds to 0.5° — inside the parallel threshold.
    DimFixture f(179.5f);
    auto r = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Line, f.lnAB),
                                          pick(DimEntityKind::Line, f.lnCD));
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.type, ConstraintType::DistancePointLine);
    EXPECT_EQ(r.entityA, f.pC);
    EXPECT_EQ(r.entityB, f.lnAB);
}

TEST(DimensionResolve, ParallelPickPrefersEndpointOverFirstSegment) {
    // Second line drawn so its START's perpendicular foot lands past the
    // first segment's end (t≈1.5) while its END's foot lands inside (t≈0.5).
    // The dim must pin the END point, or the label hangs off to the side
    // over unrelated geometry (the rectangle "third line" bug).
    Sketch sk;
    int a = sk.addPoint({0.0f, 0.0f});
    int b = sk.addPoint({10.0f, 0.0f});
    int lnA = sk.addLine(a, b);
    int cS = sk.addPoint({15.0f, 5.0f});
    int cE = sk.addPoint({5.0f, 5.0f});
    int lnC = sk.addLine(cS, cE);
    auto r = SketchTool::resolveDimension(sk, pick(DimEntityKind::Line, lnA),
                                          pick(DimEntityKind::Line, lnC));
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.type, ConstraintType::DistancePointLine);
    EXPECT_EQ(r.entityA, cE); // endpoint whose foot is inside the first segment
    EXPECT_EQ(r.entityB, lnA);
    EXPECT_NEAR(r.measured, 5.0, 1e-6);
}

TEST(DimensionResolve, InvalidCombosAreInvalid) {
    Sketch sk;
    int c = sk.addPoint({0.0f, 0.0f});
    int ci = sk.addCircle(c, 2.0);
    int p = sk.addPoint({5.0f, 0.0f});
    // circle + point is out of scope (spec non-goal)
    auto r = SketchTool::resolveDimension(sk, pick(DimEntityKind::Circle, ci),
                                          pick(DimEntityKind::Point, p));
    EXPECT_FALSE(r.valid);
    // lone point
    auto r2 = SketchTool::resolveDimension(sk, pick(DimEntityKind::Point, p), DimPick{});
    EXPECT_FALSE(r2.valid);
    // dangling id
    auto r3 = SketchTool::resolveDimension(sk, pick(DimEntityKind::Line, 9999), DimPick{});
    EXPECT_FALSE(r3.valid);
}

// Degenerate pick: a point that IS an endpoint of the target line measures a
// perpendicular distance of 0 by construction, which — if resolveDimension
// let it through — would create a DistancePointLine constraint whose
// applyCorrection has nothing to correct along (see the solver-side
// DegeneratePointOnOwnLineStaysBounded test above). Both reachable paths
// (direct point+line pick, and the line-line parallel branch whose derived
// point is the second line's own start) must reject instead.
TEST(DimensionResolve, RejectsPointOnOwnLineEndpoint) {
    DimFixture f(30.0f);
    // pA is the START point of lnAB itself.
    auto r1 = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Point, f.pA),
                                           pick(DimEntityKind::Line, f.lnAB));
    EXPECT_FALSE(r1.valid);
    // pB is the END point of lnAB — same rejection, opposite endpoint and
    // opposite pick order (line first, point second).
    auto r2 = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Line, f.lnAB),
                                           pick(DimEntityKind::Point, f.pB));
    EXPECT_FALSE(r2.valid);
    // Sanity: a genuinely off-line point through the same branch still
    // resolves normally (guards against an over-broad rejection).
    auto r3 = SketchTool::resolveDimension(f.sk, pick(DimEntityKind::Point, f.pFree),
                                           pick(DimEntityKind::Line, f.lnAB));
    EXPECT_TRUE(r3.valid);
}

TEST(DimensionResolve, RejectsChainedCollinearLineLineSharedVertex) {
    Sketch sk;
    int pA = sk.addPoint({0.0f, 0.0f});
    int pB = sk.addPoint({10.0f, 0.0f});
    int lnAB = sk.addLine(pA, pB);
    // Second line starts exactly AT pB (a chained segment sharing the
    // vertex, e.g. drawn as a continuing line chain) and is nearly
    // collinear with AB (~0.29 deg) — inside the parallel threshold. The
    // parallel branch's derived point (second line's start == pB) is an
    // endpoint of the FIRST line, so this must reject just like the direct
    // point+line pick above.
    int pF = sk.addPoint({20.0f, 0.05f});
    int lnBF = sk.addLine(pB, pF);
    auto r = SketchTool::resolveDimension(sk, pick(DimEntityKind::Line, lnAB),
                                          pick(DimEntityKind::Line, lnBF));
    EXPECT_FALSE(r.valid);
}

// TEST GAP 9: pin the parallel/angle threshold's boundary behaviour. The
// tolerance check in resolveDimension is `folded <= kParallelTol` with
// kParallelTol == 1.0 degree in double precision. DimFixture builds its
// rotated line through a FLOAT pi/180 conversion and float sin/cos, so the
// angle it actually produces for deg=1.0 is not bit-identical to the double
// 1-degree tolerance — empirically (see scratch probe) it lands a hair
// UNDER the tolerance, so <= keeps 1.0 deg on the parallel
// (DistancePointLine) side. 1.5 deg is unambiguously past the threshold.
TEST(DimensionResolve, ParallelThresholdBoundary) {
    DimFixture at1(1.0f);
    auto atThreshold = SketchTool::resolveDimension(
        at1.sk, pick(DimEntityKind::Line, at1.lnAB), pick(DimEntityKind::Line, at1.lnCD));
    ASSERT_TRUE(atThreshold.valid);
    EXPECT_EQ(atThreshold.type, ConstraintType::DistancePointLine);

    DimFixture at15(1.5f);
    auto pastThreshold = SketchTool::resolveDimension(
        at15.sk, pick(DimEntityKind::Line, at15.lnAB), pick(DimEntityKind::Line, at15.lnCD));
    ASSERT_TRUE(pastThreshold.valid);
    EXPECT_EQ(pastThreshold.type, ConstraintType::Angle);
}

// linesParallelWithinDimTol is the shared "same tolerance as
// resolveDimension's line-line branch" helper the mirrored-DistancePointLine
// dedup in Application::applyPendingDimension gates on (Application-level
// logic remains untestable here per the established exception; this
// standalone helper is the part of that fix that IS unit-testable).
TEST(DimensionResolve, LinesParallelWithinDimTol) {
    DimFixture par(0.5f);   // inside the 1 deg threshold
    EXPECT_TRUE(SketchTool::linesParallelWithinDimTol(par.sk, par.lnAB, par.lnCD));

    DimFixture ang(30.0f);  // well past the threshold
    EXPECT_FALSE(SketchTool::linesParallelWithinDimTol(ang.sk, ang.lnAB, ang.lnCD));

    DimFixture anti(179.5f); // anti-parallel: folds to 0.5 deg, inside threshold
    EXPECT_TRUE(SketchTool::linesParallelWithinDimTol(anti.sk, anti.lnAB, anti.lnCD));

    // Dangling / self / degenerate inputs are all false, not crashes.
    EXPECT_FALSE(SketchTool::linesParallelWithinDimTol(par.sk, par.lnAB, 9999));
    Sketch degenerate;
    int dp1 = degenerate.addPoint({0.0f, 0.0f});
    int dp2 = degenerate.addPoint({0.0f, 0.0f}); // zero-length line
    int dln = degenerate.addLine(dp1, dp2);
    int dp3 = degenerate.addPoint({5.0f, 0.0f});
    int dp4 = degenerate.addPoint({10.0f, 0.0f});
    int dln2 = degenerate.addLine(dp3, dp4);
    EXPECT_FALSE(SketchTool::linesParallelWithinDimTol(degenerate, dln, dln2));
}

// The triangle-altitude scenario the parallel gate exists to prevent: two
// lines sharing endpoint p2 but at a real (non-parallel) angle. Dimensioning
// p1-to-L2 then, separately, p2-to-L1 satisfies the OLD mirrored-DPL
// endpoint-membership check (each picked point is an endpoint of the OTHER
// line) but is not the same physical gap — the fix's parallel gate must
// reject it so applyPendingDimension (untestable here) falls through to
// adding a second, independent constraint instead of overwriting the first.
TEST(DimensionResolve, TriangleAltitudeIsNotMirroredDPL) {
    Sketch sk;
    // A right-angle-ish triangle: L1 = p1-p2 (horizontal), L2 = p2-p3
    // (vertical) — 90 deg apart, nowhere near the 1 deg parallel tolerance.
    int p1 = sk.addPoint({0.0f, 0.0f});
    int p2 = sk.addPoint({10.0f, 0.0f});
    int l1 = sk.addLine(p1, p2);
    int p3 = sk.addPoint({10.0f, 10.0f});
    int l2 = sk.addLine(p2, p3);
    EXPECT_FALSE(SketchTool::linesParallelWithinDimTol(sk, l1, l2));
}
