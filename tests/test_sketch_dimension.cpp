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
    // Save with the new writer, then truncate every K line back to the legacy
    // 6-field form and reload — offsets must default to 0, load must succeed.
    Document doc;
    auto sk = std::make_shared<Sketch>();
    int a = sk->addPoint({0.0f, 0.0f});
    int b = sk->addPoint({4.0f, 0.0f});
    sk->addLine(a, b);
    Constraint c{};
    c.type = ConstraintType::Distance;
    c.entityA = a;
    c.entityB = b;
    c.value = 4.0;
    c.labelOffX = 9.0; // will be stripped below
    c.labelOffY = 9.0;
    sk->addConstraint(c);
    doc.addSketch(sk, "legacy");

    std::string path = tmpProjectPath("dim_legacy.mzr");
    ASSERT_TRUE(ProjectIO::save(path, doc).success);

    // Strip trailing fields from K lines: keep "K id type eA eB value valueY".
    std::ifstream in(path);
    std::stringstream out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("K ", 0) == 0) {
            std::istringstream ls(line);
            std::string tok, kept;
            for (int i = 0; i < 7 && (ls >> tok); ++i) { // "K" + 6 fields
                if (i) kept += ' ';
                kept += tok;
            }
            out << kept << '\n';
        } else {
            out << line << '\n';
        }
    }
    in.close();
    std::ofstream ow(path, std::ios::trunc);
    ow << out.str();
    ow.close();

    Document loaded;
    ASSERT_TRUE(ProjectIO::load(path, loaded).success);
    std::remove(path.c_str());
    auto lsk = loaded.getSketch(loaded.getAllSketchIds()[0]);
    ASSERT_EQ(lsk->getConstraints().size(), 1u);
    EXPECT_DOUBLE_EQ(lsk->getConstraints()[0].labelOffX, 0.0);
    EXPECT_DOUBLE_EQ(lsk->getConstraints()[0].labelOffY, 0.0);
}
