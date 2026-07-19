// Regression test for the BREP length-prefix DoS: a tiny .brep file whose
// header declares an impossible section count (e.g. "Curves 999999999") made
// OCCT's BRepTools::Read spin/OOM for many seconds before the pre-scan guard
// in BrepIO::import was added. The guard rejects any section count larger than
// the file's own byte size (physically impossible for a real file) up front.
//
// If the guard regresses, ImpossibleCountIsRejectedFast doesn't just fail — it
// hangs, and CTest's per-test timeout turns that into a red build, which is the
// intended signal.

#include "io/BrepIO.h"
#include "core/Document.h"

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>

#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string tempPath(const char* name) {
    const char* dir = std::getenv("TMPDIR");
    std::string base = (dir && *dir) ? dir : "/tmp";
    if (!base.empty() && base.back() != '/') base += '/';
    return base + name;
}

void writeFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::trunc | std::ios::binary);
    ASSERT_TRUE(f.is_open()) << "cannot write " << path;
    f << text;
}

} // namespace

TEST(BrepIoDos, ImpossibleCountIsRejectedFast) {
    const std::string path = tempPath("mz_test_brep_hugecount.brep");
    // ~90 bytes, but the header claims ~1e9 curves — no real file can.
    writeFile(path,
              "DBRep_DrawableShape\n\n"
              "CASCADE Topology V3, (c) Open Cascade\n"
              "Locations 0\nCurve2ds 0\nCurves 999999999\n");

    Document doc;
    auto r = materializr::BrepIO::import(path, doc);
    std::remove(path.c_str());

    EXPECT_FALSE(r.success);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
    // The message names the guard so a future refactor that drops it and lets
    // the file fail elsewhere (or hang) is visible here.
    EXPECT_NE(r.errorMessage.find("impossible"), std::string::npos)
        << "expected the count-sanity guard to fire, got: " << r.errorMessage;
}

TEST(BrepIoDos, ValidFileStillRoundTrips) {
    // Guard must not reject a legitimate file: export a real box, re-import it.
    const std::string path = tempPath("mz_test_brep_valid.brep");
    Document out;
    out.addBody(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape(), "box");
    ASSERT_TRUE(materializr::BrepIO::exportFile(path, out).success);

    Document in;
    auto r = materializr::BrepIO::import(path, in);
    std::remove(path.c_str());

    EXPECT_TRUE(r.success) << r.errorMessage;
    EXPECT_EQ(in.getAllBodyIds().size(), 1u);
}

TEST(BrepIoDos, TruncatedFileFailsCleanly) {
    // A believable-but-truncated file (counts within the byte size) sails past
    // the guard and is rejected by the reader itself — still no crash/hang.
    const std::string path = tempPath("mz_test_brep_truncated.brep");
    writeFile(path,
              "DBRep_DrawableShape\n\n"
              "CASCADE Topology V3, (c) Open Cascade\n"
              "Locations 0\nCurve2ds 0\nCurves 2\n1 0 0 0 0 -1 0\n");

    Document doc;
    auto r = materializr::BrepIO::import(path, doc);
    std::remove(path.c_str());

    EXPECT_FALSE(r.success);
    EXPECT_TRUE(doc.getAllBodyIds().empty());
}
