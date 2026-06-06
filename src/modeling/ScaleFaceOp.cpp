#include "ScaleFaceOp.h"
#include "SubShapeIndex.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <GProp_GProps.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <ShapeFix_Shape.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Pln.hxx>
#include <imgui.h>

ScaleFaceOp::ScaleFaceOp() = default;

void ScaleFaceOp::setBody(int id) { m_bodyId = id; }
void ScaleFaceOp::setFace(const TopoDS_Face& f) { m_face = f; }
void ScaleFaceOp::setScalePercent(double s) { m_scalePct = s; }
void ScaleFaceOp::setLength(double l) { m_length = l; }
void ScaleFaceOp::setMode(Mode m) { m_mode = m; }

bool ScaleFaceOp::execute(Document& doc) {
    if (m_bodyId < 0 || m_face.IsNull() || m_length <= 1e-6 ||
        m_scalePct < 1.0 || m_scalePct > 500.0 ||
        std::abs(m_scalePct - 100.0) < 1e-6) {
        return false;
    }
    try {
        m_previousShape = doc.getBody(m_bodyId);

        // Planar end faces only (a wing tip cap, a box end…).
        Handle(Geom_Plane) pl =
            Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(m_face));
        if (pl.IsNull()) {
            std::fprintf(stderr, "[ScaleFace] face is not planar\n");
            return false;
        }

        // Outward normal (orientation-aware) and the face centroid — the
        // scale pivot.
        BRepGProp_Face gpf(m_face);
        double u1, u2, v1, v2;
        gpf.Bounds(u1, u2, v1, v2);
        gp_Pnt onFace;
        gp_Vec nv;
        gpf.Normal(0.5 * (u1 + u2), 0.5 * (v1 + v2), onFace, nv);
        if (nv.Magnitude() < 1e-9) return false;
        gp_Dir n(nv);
        GProp_GProps fp;
        BRepGProp::SurfaceProperties(m_face, fp);
        gp_Pnt centroid = fp.CentreOfMass();

        TopoDS_Wire capWire = BRepTools::OuterWire(m_face);
        if (capWire.IsNull()) return false;

        double s = m_scalePct / 100.0;

        // Scaled copy of the cap outline about its centroid, then a second
        // copy offset along ±normal. Both loft profiles are transformed
        // copies of ONE wire, so ThruSections compatibility is exact.
        gp_Trsf scaleT;
        scaleT.SetScale(centroid, s);

        auto movedWire = [&](const TopoDS_Wire& w,
                             const gp_Trsf& t) -> TopoDS_Wire {
            BRepBuilderAPI_Transform xf(w, t, Standard_True);
            return TopoDS::Wire(xf.Shape());
        };

        TopoDS_Shape result;
        if (m_mode == Mode::Extend) {
            // Tip extension: cap outline → scaled outline at +L outward.
            gp_Trsf off;
            off.SetTranslation(gp_Vec(n) * m_length);
            gp_Trsf comb = off.Multiplied(scaleT);
            TopoDS_Wire wTip = movedWire(capWire, comb);

            BRepOffsetAPI_ThruSections loft(Standard_True);
            loft.AddWire(capWire);
            loft.AddWire(wTip);
            loft.Build();
            if (!loft.IsDone()) {
                std::fprintf(stderr, "[ScaleFace] tip loft failed\n");
                return false;
            }
            BRepAlgoAPI_Fuse fuse(m_previousShape, loft.Shape());
            fuse.SetFuzzyValue(1.0e-4);
            fuse.Build();
            if (!fuse.IsDone()) {
                std::fprintf(stderr, "[ScaleFace] fuse failed\n");
                return false;
            }
            result = fuse.Shape();
        } else {
            // Pinch: cut the last L off, intersect the removed tip with a
            // pinching frustum, fuse it back.
            gp_Pnt cutPt = centroid.Translated(gp_Vec(n) * (-m_length));

            // Half-space substitute: a huge box beyond the cut plane on
            // the tip side, built as a prism of a big rectangle face on
            // the cut plane (prism of a planar face — robust).
            Bnd_Box bb;
            BRepBndLib::Add(m_previousShape, bb);
            double bx0, by0, bz0, bx1, by1, bz1;
            bb.Get(bx0, by0, bz0, bx1, by1, bz1);
            double diag = gp_Pnt(bx0, by0, bz0).Distance(
                gp_Pnt(bx1, by1, bz1)) + m_length;

            gp_Pln cutPln(cutPt, n);
            TopoDS_Face bigFace = BRepBuilderAPI_MakeFace(
                cutPln, -diag, diag, -diag, diag).Face();
            TopoDS_Shape tipBox =
                BRepPrimAPI_MakePrism(bigFace, gp_Vec(n) * (2.0 * diag))
                    .Shape();

            BRepAlgoAPI_Cut mainCut(m_previousShape, tipBox);
            mainCut.SetFuzzyValue(1.0e-4);
            mainCut.Build();
            if (!mainCut.IsDone()) return false;
            TopoDS_Shape mainPiece = mainCut.Shape();

            // Pinching frustum: full-size outline AT the cut plane →
            // scaled outline at the original cap plane.
            gp_Trsf back;
            back.SetTranslation(gp_Vec(n) * (-m_length));
            TopoDS_Wire wBase = movedWire(capWire, back);
            TopoDS_Wire wTip = movedWire(capWire, scaleT);

            BRepOffsetAPI_ThruSections loft(Standard_True);
            loft.AddWire(wBase);
            loft.AddWire(wTip);
            loft.Build();
            if (!loft.IsDone()) {
                std::fprintf(stderr, "[ScaleFace] frustum loft failed\n");
                return false;
            }

            BRepAlgoAPI_Common tipCommon(m_previousShape, loft.Shape());
            tipCommon.SetFuzzyValue(1.0e-4);
            tipCommon.Build();
            if (!tipCommon.IsDone()) return false;
            // Keep only the part of the pinched tip beyond the cut plane —
            // the frustum also overlaps inboard material.
            BRepAlgoAPI_Common tipPiece(tipCommon.Shape(), tipBox);
            tipPiece.SetFuzzyValue(1.0e-4);
            tipPiece.Build();
            if (!tipPiece.IsDone()) return false;

            BRepAlgoAPI_Fuse fuse(mainPiece, tipPiece.Shape());
            fuse.SetFuzzyValue(1.0e-4);
            fuse.Build();
            if (!fuse.IsDone()) return false;
            result = fuse.Shape();
        }

        if (result.IsNull()) return false;

        // Sanity: the result must still have volume, and pinch must not
        // have annihilated the body.
        GProp_GProps vp;
        BRepGProp::VolumeProperties(result, vp);
        if (vp.Mass() < 1e-6) {
            std::fprintf(stderr, "[ScaleFace] degenerate result\n");
            return false;
        }

        if (!BRepCheck_Analyzer(result).IsValid()) {
            ShapeFix_Shape fixer(result);
            fixer.Perform();
            result = fixer.Shape();
        }
        doc.updateBody(m_bodyId, result);
        return true;
    } catch (...) {
        std::fprintf(stderr, "[ScaleFace] execute threw\n");
        return false;
    }
}

bool ScaleFaceOp::undo(Document& doc) {
    if (m_bodyId < 0 || m_previousShape.IsNull()) return false;
    try {
        doc.updateBody(m_bodyId, m_previousShape);
        return true;
    } catch (...) { return false; }
}

std::string ScaleFaceOp::description() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Scale face to %.0f%% over %.1f mm (%s)",
                  m_scalePct, m_length,
                  m_mode == Mode::Extend ? "extend" : "pinch");
    return buf;
}

void ScaleFaceOp::renderProperties() {
    ImGui::Text("Scale Face");
    ImGui::Separator();
    ImGui::InputDouble("Scale (%)", &m_scalePct, 1.0, 10.0, "%.1f");
    ImGui::InputDouble("Length (mm)", &m_length, 0.5, 5.0, "%.2f");
    ImGui::Text("Mode: %s", m_mode == Mode::Extend ? "Extend" : "Pinch");
    ImGui::Text("Body ID: %d", m_bodyId);
}

std::string ScaleFaceOp::serializeParams() const {
    std::string blob;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "body=%d;scale=%.6f;len=%.6f;mode=%d",
                  m_bodyId, m_scalePct, m_length,
                  static_cast<int>(m_mode));
    blob += buf;
    if (!m_previousShape.IsNull() && !m_face.IsNull()) {
        std::vector<TopoDS_Shape> faces{m_face};
        std::string idx = SubShapeIndex::serialize(m_previousShape, faces,
                                                   TopAbs_FACE);
        if (!idx.empty()) blob += ";faces=" + idx;
    }
    return blob;
}

bool ScaleFaceOp::deserializeParams(const std::string& blob) {
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if      (key == "body")  { m_bodyId = std::atoi(val.c_str()); any = true; }
        else if (key == "scale") { m_scalePct = std::atof(val.c_str()); any = true; }
        else if (key == "len")   { m_length = std::atof(val.c_str()); any = true; }
        else if (key == "mode")  {
            m_mode = std::atoi(val.c_str()) == 1 ? Mode::Pinch : Mode::Extend;
            any = true;
        }
        else if (key == "faces") {
            m_faceIndices = SubShapeIndex::parse(val);
            any = true;
        }
        pos = end + 1;
    }
    return any;
}

bool ScaleFaceOp::rehydrateFromReload(const ReloadState& state,
                                      Document& /*doc*/) {
    if (m_bodyId < 0 || m_faceIndices.empty()) return false;

    m_previousShape.Nullify();
    for (const auto& [id, shp] : state.modifiedBefore)
        if (id == m_bodyId) { m_previousShape = shp; break; }
    if (m_previousShape.IsNull()) return false;

    std::vector<TopoDS_Shape> resolved;
    if (!SubShapeIndex::resolveAll(m_previousShape, m_faceIndices,
                                   TopAbs_FACE, resolved) ||
        resolved.empty()) {
        return false;
    }
    m_face = TopoDS::Face(resolved.front());
    return true;
}

OperationDiff ScaleFaceOp::captureDiff() const {
    OperationDiff d;
    if (m_bodyId >= 0 && !m_previousShape.IsNull())
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    return d;
}
