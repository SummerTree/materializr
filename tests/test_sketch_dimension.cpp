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
