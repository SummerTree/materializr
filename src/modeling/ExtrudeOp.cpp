#include "ExtrudeOp.h"
#include "Sketch.h"
#include <cstdio>
#include <cstdlib>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <TopoDS.hxx>
#include <imgui.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

ExtrudeOp::ExtrudeOp() = default;

void ExtrudeOp::setProfile(const TopoDS_Shape& wire) {
    m_profile = wire;
}

// Rebuild m_profile from the currently-live source sketch. Mirrors the
// Application::buildSketchProfileFace algorithm so the cascade produces the
// same shape the original extrude got (largest-bbox wire as outer, the rest
// reversed as holes). Returns false if there's nothing to rebuild from.
bool ExtrudeOp::rebuildProfileFromSketch(Document& doc) {
    if (m_sketchId < 0) return false;
    auto sk = doc.getSketch(m_sketchId);
    if (!sk) return false;

    auto wires = sk->buildWires();
    if (wires.empty()) return false;

    int outerIdx = 0;
    double bestExtent = -1.0;
    std::vector<double> extents(wires.size(), 0.0);
    for (size_t i = 0; i < wires.size(); ++i) {
        Bnd_Box bb;
        BRepBndLib::Add(wires[i], bb);
        if (bb.IsVoid()) continue;
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bb.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
        double diag = dx*dx + dy*dy + dz*dz;
        extents[i] = diag;
        if (diag > bestExtent) {
            bestExtent = diag;
            outerIdx = static_cast<int>(i);
        }
    }

    BRepBuilderAPI_MakeFace faceMaker(sk->getPlane(), wires[outerIdx]);
    for (size_t i = 0; i < wires.size(); ++i) {
        if (static_cast<int>(i) == outerIdx) continue;
        faceMaker.Add(TopoDS::Wire(wires[i].Reversed()));
    }
    if (!faceMaker.IsDone()) return false;
    m_profile = faceMaker.Face();
    return true;
}

void ExtrudeOp::setDistance(double distance) {
    m_distance = distance;
}

void ExtrudeOp::setDirection(ExtrudeDirection dir) {
    m_direction = dir;
}

void ExtrudeOp::setMode(ExtrudeMode mode) {
    m_mode = mode;
}

void ExtrudeOp::setTargetBody(int bodyId) {
    m_targetBodyId = bodyId;
}

void ExtrudeOp::setDraftAngle(double degrees) {
    m_draftAngle = degrees;
}

bool ExtrudeOp::execute(Document& doc) {
    if (m_profile.IsNull()) {
        return false;
    }

    try {
        TopoDS_Shape extrudedShape;

        // Compute extrude direction from the profile face's normal
        gp_Vec faceNormal(0, 0, 1); // default Z
        if (m_profile.ShapeType() == TopAbs_FACE) {
            BRepGProp_Face prop(TopoDS::Face(m_profile));
            gp_Pnt center;
            gp_Vec norm;
            double u1, u2, v1, v2;
            prop.Bounds(u1, u2, v1, v2);
            prop.Normal((u1+u2)*0.5, (v1+v2)*0.5, center, norm);
            if (norm.Magnitude() > 1e-10) {
                faceNormal = norm.Normalized();
            }
        }

        if (m_direction == ExtrudeDirection::Symmetric) {
            double halfDist = m_distance / 2.0;
            gp_Vec vecUp = faceNormal * halfDist;
            gp_Vec vecDown = faceNormal * (-halfDist);

            BRepPrimAPI_MakePrism prismUp(m_profile, vecUp);
            prismUp.Build();
            if (!prismUp.IsDone()) return false;

            BRepPrimAPI_MakePrism prismDown(m_profile, vecDown);
            prismDown.Build();
            if (!prismDown.IsDone()) return false;

            BRepAlgoAPI_Fuse fuse(prismUp.Shape(), prismDown.Shape());
            fuse.Build();
            if (!fuse.IsDone()) return false;
            extrudedShape = fuse.Shape();
            try {
                ShapeUpgrade_UnifySameDomain unifier(extrudedShape, true, true, true);
                unifier.Build();
                TopoDS_Shape unified = unifier.Shape();
                if (!unified.IsNull()) extrudedShape = unified;
            } catch (...) { /* keep un-unified result */ }
        } else {
            gp_Vec direction = faceNormal * m_distance;
            BRepPrimAPI_MakePrism prism(m_profile, direction);
            prism.Build();
            if (!prism.IsDone()) {
                return false;
            }
            extrudedShape = prism.Shape();

            // Apply draft angle to lateral faces if specified
            if (std::abs(m_draftAngle) > 0.01) {
                try {
                    double angleRad = m_draftAngle * M_PI / 180.0;
                    gp_Dir pullDir(faceNormal.IsParallel(gp_Vec(0,0,1), 0.01)
                        ? gp_Dir(0,0, m_distance > 0 ? 1 : -1)
                        : gp_Dir(m_distance > 0 ? faceNormal : faceNormal.Reversed()));
                    gp_Pln neutralPlane(gp_Pnt(0, 0, 0), pullDir);

                    BRepOffsetAPI_DraftAngle drafter(extrudedShape);
                    for (TopExp_Explorer exp(extrudedShape, TopAbs_FACE); exp.More(); exp.Next()) {
                        TopoDS_Face face = TopoDS::Face(exp.Current());
                        // Only draft lateral faces (skip top/bottom)
                        BRepAdaptor_Surface surf(face);
                        if (surf.GetType() == GeomAbs_Plane) {
                            gp_Pln facePln = surf.Plane();
                            double dot = std::abs(facePln.Axis().Direction().Dot(pullDir));
                            if (dot < 0.9) {
                                drafter.Add(face, pullDir, angleRad, neutralPlane);
                            }
                        }
                    }
                    drafter.Build();
                    if (drafter.IsDone()) {
                        extrudedShape = drafter.Shape();
                    }
                } catch (...) {
                    // Draft failed — keep the undrafted shape
                }
            }
        }

        // Apply boolean mode
        switch (m_mode) {
            case ExtrudeMode::NewBody: {
                // addOrPutBody: on redo (m_createdBodyId already set from a
                // prior execute), reuses the same id so the body's folderId /
                // colour / visibility / name are restored from the tombstone
                // that undo() left behind.
                doc.addOrPutBody(m_createdBodyId, extrudedShape, "Extrude");
                break;
            }
            case ExtrudeMode::Union: {
                if (m_targetBodyId < 0) {
                    return false;
                }
                m_previousTargetShape = doc.getBody(m_targetBodyId);
                BRepAlgoAPI_Fuse fuse(m_previousTargetShape, extrudedShape);
                fuse.Build();
                if (!fuse.IsDone()) {
                    return false;
                }
                TopoDS_Shape fused = fuse.Shape();
                try {
                    ShapeUpgrade_UnifySameDomain unifier(fused, true, true, true);
                    unifier.Build();
                    TopoDS_Shape unified = unifier.Shape();
                    if (!unified.IsNull()) fused = unified;
                } catch (...) { /* keep un-unified result */ }
                doc.updateBody(m_targetBodyId, fused);
                m_createdBodyId = -1;
                break;
            }
            case ExtrudeMode::Subtract: {
                if (m_targetBodyId < 0) {
                    return false;
                }
                m_previousTargetShape = doc.getBody(m_targetBodyId);
                BRepAlgoAPI_Cut cut(m_previousTargetShape, extrudedShape);
                cut.Build();
                if (!cut.IsDone()) {
                    return false;
                }
                doc.updateBody(m_targetBodyId, cut.Shape());
                m_createdBodyId = -1;
                break;
            }
            case ExtrudeMode::Intersect: {
                if (m_targetBodyId < 0) {
                    return false;
                }
                m_previousTargetShape = doc.getBody(m_targetBodyId);
                BRepAlgoAPI_Common common(m_previousTargetShape, extrudedShape);
                common.Build();
                if (!common.IsDone()) {
                    return false;
                }
                doc.updateBody(m_targetBodyId, common.Shape());
                m_createdBodyId = -1;
                break;
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool ExtrudeOp::undo(Document& doc) {
    try {
        if (m_mode == ExtrudeMode::NewBody) {
            if (m_createdBodyId >= 0) {
                doc.removeBody(m_createdBodyId);
                // Keep m_createdBodyId set so a future redo's addOrPutBody
                // reuses the same id and restores tombstone metadata.
            }
        } else {
            // Restore previous target shape for boolean operations
            if (m_targetBodyId >= 0 && !m_previousTargetShape.IsNull()) {
                doc.updateBody(m_targetBodyId, m_previousTargetShape);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string ExtrudeOp::serializeParams() const {
    // Profile geometry is NOT stored — it is re-derived from the source sketch
    // on reload. Only ops with a sketch source (m_sketchId >= 0) can rehydrate;
    // a face-driven extrude serialises its scalars but declines rehydration.
    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "sketch=%d;dist=%.6f;dir=%d;mode=%d;target=%d;draft=%.6f",
        m_sketchId, m_distance, static_cast<int>(m_direction),
        static_cast<int>(m_mode), m_targetBodyId, m_draftAngle);
    return buf;
}

bool ExtrudeOp::deserializeParams(const std::string& blob) {
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        double d = std::atof(val.c_str());
        int    i = std::atoi(val.c_str());
        if      (key == "sketch") { m_sketchId = i; any = true; }
        else if (key == "dist")   { m_distance = d; any = true; }
        else if (key == "dir")    { m_direction = static_cast<ExtrudeDirection>(i); any = true; }
        else if (key == "mode")   { m_mode = static_cast<ExtrudeMode>(i); any = true; }
        else if (key == "target") { m_targetBodyId = i; any = true; }
        else if (key == "draft")  { m_draftAngle = d; any = true; }
        pos = end + 1;
    }
    return any;
}

bool ExtrudeOp::rehydrateFromReload(const ReloadState& state, Document& doc) {
    // Re-derive the profile from the persistent source sketch. Without a sketch
    // source (e.g. a face-driven extrude) there's nothing to rebuild from, so
    // decline and let the loader fall back to a baked ReplayOp.
    if (m_sketchId < 0) return false;
    if (!rebuildProfileFromSketch(doc)) return false;

    if (m_mode == ExtrudeMode::NewBody) {
        // The body this step created is the extruded solid; adopt its id so
        // undo()/redo() and a distance edit recreate it under the same id
        // (addOrPutBody reuses it, restoring tombstoned folder/colour/name).
        if (state.created.empty()) return false;
        m_createdBodyId = state.created.front();
    } else {
        // Boolean mode mutates the target in place; restore its pre-step shape
        // so undo() reverts and editStep can recompute from it.
        for (const auto& [id, shp] : state.modifiedBefore) {
            if (id == m_targetBodyId) { m_previousTargetShape = shp; break; }
        }
        if (m_previousTargetShape.IsNull()) return false;
    }
    return true;
}

std::string ExtrudeOp::description() const {
    std::string desc = "Extrude " + std::to_string(m_distance) + "mm";
    switch (m_mode) {
        case ExtrudeMode::NewBody:   desc += " (New Body)"; break;
        case ExtrudeMode::Union:     desc += " (Union)"; break;
        case ExtrudeMode::Subtract:  desc += " (Subtract)"; break;
        case ExtrudeMode::Intersect: desc += " (Intersect)"; break;
    }
    return desc;
}

void ExtrudeOp::renderProperties() {
    ImGui::Text("Extrude");
    ImGui::Separator();

    ImGui::InputDouble("Distance", &m_distance, 0.1, 1.0, "%.3f");

    const char* modeItems[] = { "New Body", "Union", "Subtract", "Intersect" };
    int modeIndex = static_cast<int>(m_mode);
    if (ImGui::Combo("Mode", &modeIndex, modeItems, 4)) {
        m_mode = static_cast<ExtrudeMode>(modeIndex);
    }

    const char* dirItems[] = { "Normal", "Symmetric", "Custom" };
    int dirIndex = static_cast<int>(m_direction);
    if (ImGui::Combo("Direction", &dirIndex, dirItems, 3)) {
        m_direction = static_cast<ExtrudeDirection>(dirIndex);
    }

    ImGui::InputDouble("Draft Angle", &m_draftAngle, 0.1, 1.0, "%.1f");

    if (m_mode != ExtrudeMode::NewBody) {
        ImGui::InputInt("Target Body ID", &m_targetBodyId);
    }
}

OperationDiff ExtrudeOp::captureDiff() const {
    OperationDiff d;
    if (m_mode == ExtrudeMode::NewBody) {
        if (m_createdBodyId >= 0) d.created.push_back(m_createdBodyId);
    } else if (m_targetBodyId >= 0 && !m_previousTargetShape.IsNull()) {
        d.modifiedBefore.push_back({m_targetBodyId, m_previousTargetShape});
    }
    return d;
}
