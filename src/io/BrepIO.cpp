#include "BrepIO.h"
#include "../core/Document.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Standard_ErrorHandler.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Trsf.hxx>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace materializr {

namespace {

// Disk convention matches StepIO: files are Z-up (the CAD-world norm — FreeCAD
// included), the scene is Y-up. Rotate about +X by ±90°.
TopoDS_Shape rotated(const TopoDS_Shape& s, double angle) {
    gp_Trsf t;
    t.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)), angle);
    try {
        BRepBuilderAPI_Transform xf(s, t, /*copy=*/true);
        if (xf.IsDone() && !xf.Shape().IsNull()) return xf.Shape();
    } catch (...) {}
    return s;
}

// Add a shape's solids as bodies; fall back to shells, then faces, then the
// shape itself — the IgesIO cascade, so a faces-only file still lands visibly.
int addShapeAsBodies(const TopoDS_Shape& shape, Document& doc, int& counter) {
    int added = 0;
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
        doc.addBody(ex.Current(), "Imported_" + std::to_string(++counter));
        ++added;
    }
    if (added == 0) {
        for (TopExp_Explorer ex(shape, TopAbs_SHELL, TopAbs_SOLID); ex.More(); ex.Next()) {
            doc.addBody(ex.Current(), "Imported_" + std::to_string(++counter));
            ++added;
        }
    }
    if (added == 0) {
        for (TopExp_Explorer ex(shape, TopAbs_FACE, TopAbs_SHELL); ex.More(); ex.Next()) {
            doc.addBody(ex.Current(), "Imported_" + std::to_string(++counter));
            ++added;
        }
    }
    if (added == 0 && !shape.IsNull()) {
        doc.addBody(shape, "Imported_" + std::to_string(++counter));
        ++added;
    }
    return added;
}

// Pre-scan guard against a length-prefix DoS in OCCT's BREP reader. The ASCII
// BREP format is a run of section tables (Curves, Curve2ds, Surfaces, …) each
// introduced by a "Keyword <count>" line, and BRepTools::Read trusts each
// count — it reserves/reads that many records before validating anything. A
// ~90-byte file whose header says "Curves 999999999" makes it spin/OOM for
// many seconds, which neither OCC_CATCH_SIGNALS nor the try/catch below can
// interrupt (it's a long allocation/read loop, not a signal or a throw). No
// legitimate file can have a section count larger than its own byte size — a
// single record is always several bytes on disk — so a count exceeding the
// file length is provably fake. Reject those (and absurdly large files) up
// front, before handing the path to the kernel reader.
//
// Returns false and fills `why` when the file should be refused; true = safe
// to hand to BRepTools::Read (including genuinely-broken-but-bounded files,
// which the reader itself rejects quickly and cleanly).
bool brepHeaderCountsSane(const std::string& filePath, std::string& why) {
    constexpr std::uintmax_t kMaxBytes = 512ull * 1024 * 1024; // 512 MB, as ProjectIO

    std::ifstream in(filePath, std::ios::in | std::ios::binary);
    if (!in.is_open()) return true; // let BRepTools::Read report the open error

    in.seekg(0, std::ios::end);
    std::streamoff endPos = in.tellg();
    in.seekg(0, std::ios::beg);
    if (endPos < 0) return true; // unseekable — nothing to pre-scan, let OCCT try
    const std::uintmax_t size = static_cast<std::uintmax_t>(endPos);
    if (size > kMaxBytes) {
        why = "BREP file too large (> 512 MB) — refusing to load";
        return false;
    }

    // The dangerous counts live on their own "Keyword <int>" lines in the
    // header run. Scan line-by-line; a section count can never exceed the file
    // size in bytes, so that's the reject threshold (generous — real records
    // are far bigger than a byte, so this never trips a valid file).
    static const char* kKeywords[] = {
        "Locations", "Curve2ds", "Curves", "Polygon3D",
        "PolygonOnTriangulations", "Surfaces", "Triangulations", "TShapes",
    };
    std::string line;
    while (std::getline(in, line)) {
        for (const char* kw : kKeywords) {
            const std::size_t klen = std::char_traits<char>::length(kw);
            if (line.compare(0, klen, kw) != 0) continue;
            if (line.size() <= klen || line[klen] != ' ') continue;
            // Parse the count token; a value beyond the file size is impossible.
            std::istringstream ls(line.substr(klen + 1));
            unsigned long long count = 0;
            if (!(ls >> count)) break; // not a count line for this keyword
            if (count > size) {
                why = std::string("BREP header declares an impossible ") + kw +
                      " count — refusing (likely a malformed/hostile file)";
                return false;
            }
            break;
        }
    }
    return true;
}

} // namespace

ImportResult BrepIO::import(const std::string& filePath, Document& doc) {
    ImportResult result;
    try {
        OCC_CATCH_SIGNALS // kernel fault on a crafted file → the catch below
        // Bound the untrusted length-prefixed section counts before the kernel
        // reader trusts them (see brepHeaderCountsSane).
        if (std::string why; !brepHeaderCountsSane(filePath, why)) {
            result.errorMessage = why;
            return result;
        }
        TopoDS_Shape shape;
        BRep_Builder builder;
        if (!BRepTools::Read(shape, filePath.c_str(), builder)) {
            result.errorMessage = "Failed to read BREP file: " + filePath;
            return result;
        }
        if (shape.IsNull()) {
            result.errorMessage = "BREP file contained no shape.";
            return result;
        }
        // Disk Z-up → scene Y-up: −90° about +X, matching StepIO/StlIO.
        // (The signs were briefly inverted — self-consistent, so a round-trip
        // through our own pair looked fine, but files disagreed with our STEP
        // exports by 180°. #45.)
        shape = rotated(shape, -M_PI * 0.5);

        // A top-level compound is our own multi-body export (or FreeCAD's) —
        // each child becomes its own body so they stay individually editable.
        int counter = 0;
        int imported = 0;
        if (shape.ShapeType() == TopAbs_COMPOUND) {
            for (TopoDS_Iterator it(shape); it.More(); it.Next())
                imported += addShapeAsBodies(it.Value(), doc, counter);
        } else {
            imported = addShapeAsBodies(shape, doc, counter);
        }
        if (imported == 0) {
            result.errorMessage = "No usable geometry in BREP file.";
            return result;
        }
        result.success = true;
        result.bodiesImported = imported;
        return result;
    } catch (const Standard_Failure& e) {
        result.errorMessage = std::string("OCCT error reading BREP: ") +
                              (e.GetMessageString() ? e.GetMessageString() : "unknown");
        return result;
    } catch (const std::exception& e) {
        result.errorMessage = std::string("Error reading BREP: ") + e.what();
        return result;
    } catch (...) {
        result.errorMessage = "Unknown error reading BREP file.";
        return result;
    }
}

ExportResult BrepIO::exportFile(const std::string& filePath, const Document& doc) {
    ExportResult result;
    try {
        OCC_CATCH_SIGNALS
        std::vector<int> ids = doc.getAllBodyIds();
        if (ids.empty()) {
            result.errorMessage = "No bodies to export.";
            return result;
        }
        // Single body exports bare; several go in one compound (round-trips
        // through our own import as separate bodies again).
        TopoDS_Shape out;
        if (ids.size() == 1) {
            out = doc.getBody(ids.front());
        } else {
            TopoDS_Compound comp;
            BRep_Builder bb;
            bb.MakeCompound(comp);
            for (int id : ids) {
                const TopoDS_Shape& s = doc.getBody(id);
                if (!s.IsNull()) bb.Add(comp, s);
            }
            out = comp;
        }
        if (out.IsNull()) {
            result.errorMessage = "No exportable geometry.";
            return result;
        }
        out = rotated(out, M_PI * 0.5); // scene Y-up → disk Z-up (+90°, see import)
        if (!BRepTools::Write(out, filePath.c_str())) {
            result.errorMessage = "Failed to write BREP file: " + filePath;
            return result;
        }
        result.success = true;
        return result;
    } catch (const Standard_Failure& e) {
        result.errorMessage = std::string("OCCT error writing BREP: ") +
                              (e.GetMessageString() ? e.GetMessageString() : "unknown");
        return result;
    } catch (const std::exception& e) {
        result.errorMessage = std::string("Error writing BREP: ") + e.what();
        return result;
    } catch (...) {
        result.errorMessage = "Unknown error writing BREP file.";
        return result;
    }
}

} // namespace materializr
