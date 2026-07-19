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
