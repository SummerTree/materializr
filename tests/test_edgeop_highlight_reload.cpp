// A chamfer/fillet whose saved params carry no generated-face indices (a
// fill-fallback whose `gen=` was dropped during heavy undo/redo churn — the
// light cover's steps 68/69) must still recover its bevel faces on reload, or
// the history-hover highlight is blank. rehydrateFromReload falls back to "the
// faces present in the result but not the input" (facesCreatedVsPrev).
#include <gtest/gtest.h>

#include "core/Document.h"
#include "core/Operation.h"
#include "modeling/ChamferOp.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>

#include <string>

using namespace materializr;

namespace {
// Strip the ";gen=...;" token from a serialized param blob to simulate a save
// that lost its generated-face indices.
std::string stripGen(const std::string& blob) {
    size_t g = blob.find(";gen=");
    if (g == std::string::npos) return blob;
    size_t end = blob.find(';', g + 1);
    return blob.substr(0, g) + (end == std::string::npos ? "" : blob.substr(end));
}
} // namespace

TEST(EdgeOpHighlightReload, ChamferRecoversFacesWithoutGenIndices) {
    Document doc;
    const TopoDS_Shape pre = BRepPrimAPI_MakeBox(20.0, 20.0, 10.0).Shape();
    int id = doc.addBody(pre, "box");

    // Chamfer the top-front edge (y=0, z=10) — native, one bevel face.
    TopoDS_Edge e;
    for (TopExp_Explorer ex(doc.getBody(id), TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& c = TopoDS::Edge(ex.Current());
        if (BRepAdaptor_Curve(c).GetType() != GeomAbs_Line) continue;
        bool ok = true;
        int nv = 0;
        for (TopExp_Explorer vx(c, TopAbs_VERTEX); vx.More(); vx.Next(), ++nv) {
            gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vx.Current()));
            if (std::abs(p.Y()) > 1e-7 || std::abs(p.Z() - 10.0) > 1e-7) ok = false;
        }
        if (ok && nv == 2) { e = c; break; }
    }
    ASSERT_FALSE(e.IsNull());

    ChamferOp op;
    op.setBody(id);
    op.setEdges({e});
    op.setDistance(2.0);
    ASSERT_TRUE(op.execute(doc));
    ASSERT_FALSE(op.getGeneratedFaces().empty());
    const TopoDS_Shape post = doc.getBody(id);

    // Serialize, then DROP the gen= indices — the churn-corruption case.
    const std::string params = stripGen(op.serializeParams());
    ASSERT_EQ(params.find(";gen="), std::string::npos);

    // Reload: fresh op, deserialize, rehydrate against the before/after bodies.
    ChamferOp reloaded;
    ASSERT_TRUE(reloaded.deserializeParams(params));
    Operation::ReloadState rs;
    rs.modifiedBefore.push_back({id, pre});
    rs.modifiedAfter.push_back({id, post});
    ASSERT_TRUE(reloaded.rehydrateFromReload(rs, doc));

    // The geometric fallback must have recovered the bevel face(s) so the
    // history-hover highlight has something to draw.
    EXPECT_FALSE(reloaded.getGeneratedFaces().empty())
        << "bevel faces not recovered without gen= indices — highlight blank";
}
