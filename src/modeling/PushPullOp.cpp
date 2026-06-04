#include "PushPullOp.h"
#include "Sketch.h"
#include <cstdio>
#include <cstdlib>

#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <Bnd_Box.hxx>
#include <BRep_Tool.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <imgui.h>
#include <cmath>
#include <unordered_set>

PushPullOp::PushPullOp() = default;

void PushPullOp::setTargets(std::vector<Target> targets) {
    m_targets = std::move(targets);
    // Keep the cascade-source arrays sized in lockstep with m_targets. -1
    // entries mean "no sketch source" — face-driven push/pulls stay opaque
    // to the cascade walker.
    m_sketchSourceIds.assign(m_targets.size(), -1);
    m_sketchSourceRegions.assign(m_targets.size(), -1);
}

void PushPullOp::setDistance(double d) {
    m_distance = d;
}

void PushPullOp::setSketchSource(int targetIndex, int sketchId, int regionIndex) {
    if (targetIndex < 0 ||
        targetIndex >= static_cast<int>(m_sketchSourceIds.size())) return;
    m_sketchSourceIds[targetIndex]     = sketchId;
    m_sketchSourceRegions[targetIndex] = regionIndex;
}

bool PushPullOp::hasAnySketchSource() const {
    for (int id : m_sketchSourceIds) if (id >= 0) return true;
    return false;
}

int PushPullOp::getSketchIdAt(int targetIndex) const {
    if (targetIndex < 0 ||
        targetIndex >= static_cast<int>(m_sketchSourceIds.size())) return -1;
    return m_sketchSourceIds[targetIndex];
}

// Rebuild any target.profile that was originally produced by the given
// sketch. Used by the cascade walker after a constraint commit. Returns
// true if at least one profile was rebuilt — caller should then re-execute
// the op so the body shape catches up.
bool PushPullOp::rebuildProfileFromSketch(Document& doc, int sketchId) {
    if (sketchId < 0) return false;
    auto sk = doc.getSketch(sketchId);
    if (!sk) return false;
    auto regions = sk->buildRegions();
    if (regions.empty()) return false;
    bool any = false;
    for (size_t i = 0; i < m_targets.size(); ++i) {
        if (i >= m_sketchSourceIds.size() ||
            m_sketchSourceIds[i] != sketchId) continue;
        int idx = (i < m_sketchSourceRegions.size())
                      ? m_sketchSourceRegions[i] : -1;
        if (idx < 0 || idx >= static_cast<int>(regions.size())) idx = 0;
        if (regions[idx].face.IsNull()) continue;
        m_targets[i].profile = regions[idx].face;
        any = true;
    }
    return any;
}

bool PushPullOp::execute(Document& doc) {
    // Direct re-execute support (e.g. cascade after a sketch constraint
    // edit): fold the previously-created body ids back into the reuse pool
    // so addOrPutBody re-uses the same ids and updates the existing bodies
    // in place. Without this each re-execute would allocate fresh body ids
    // and pile up duplicate bodies at the same coordinates.
    if (m_reuseBodyIds.empty() && !m_createdBodyIds.empty()) {
        m_reuseBodyIds = std::move(m_createdBodyIds);
    }
    m_previousBodies.clear();
    m_createdBodyIds.clear();
    m_reuseIdx = 0; // walks m_reuseBodyIds as each free-floating output is emitted
    if (m_targets.empty() || std::abs(m_distance) < 1e-6) return false;

    std::unordered_set<int> savedBodies;

    for (const auto& tgt : m_targets) {
        if (tgt.profile.IsNull()) continue;

        // Compute push/pull direction. For a flat face this is the face's
        // outward normal at its UV midpoint. For a CURVED face (chamfer cone,
        // fillet torus, cylinder side, etc.) that UV-midpoint normal is the
        // surface tangent perpendicular at one specific point — sloped and
        // dependent on where you happened to click. The user expects a stable
        // axis-aligned direction instead, so we use the surface's natural
        // rotation axis for chamfers/fillets/cylinders/revolves. Sign-correct
        // so positive distance still pushes outward.
        // (Must mirror the logic in Application::beginPushPull so the live
        // arrow and the executed extrusion agree on direction.)
        gp_Vec faceNormal(0, 0, 1);
        try {
            BRepGProp_Face prop(tgt.profile);
            double u1, u2, v1, v2;
            prop.Bounds(u1, u2, v1, v2);
            gp_Pnt center;
            gp_Vec n;
            prop.Normal((u1 + u2) * 0.5, (v1 + v2) * 0.5, center, n);
            if (n.Magnitude() > 1e-10) {
                faceNormal = n.Normalized();
                // Verify the surface normal actually points OUTWARD from the
                // source body. STEP-imported faces sometimes carry surface
                // normals that point INTO the body, in which case the prism
                // ends up extruding into the solid (push) and pulling into
                // empty space (cut with no overlap). Probe the body's
                // classifier on BOTH sides of the face at 1 mm; only flip
                // when "forward is IN, backward is OUT" — an unambiguous
                // disagreement that's safe to act on.
                if (tgt.sourceBodyId >= 0) {
                    try {
                        TopoDS_Shape body = doc.getBody(tgt.sourceBodyId);
                        if (!body.IsNull()) {
                            // Pick the outward direction geometrically: the
                            // body's bbox centre lies roughly in the body's
                            // interior, so a face whose normal points TOWARD
                            // the bbox centre is pointing INWARD and needs
                            // to be flipped. This is far more robust than the
                            // BRepClass3d probe approach on thin bodies and
                            // around edges (where the classifier returns
                            // ON / OUT instead of IN and the reversal misses).
                            Bnd_Box bodyBB;
                            BRepBndLib::Add(body, bodyBB);
                            Bnd_Box faceBB;
                            BRepBndLib::Add(tgt.profile, faceBB);
                            if (!bodyBB.IsVoid() && !faceBB.IsVoid()) {
                                double bxmn,bymn,bzmn,bxmx,bymx,bzmx;
                                double fxmn,fymn,fzmn,fxmx,fymx,fzmx;
                                bodyBB.Get(bxmn,bymn,bzmn,bxmx,bymx,bzmx);
                                faceBB.Get(fxmn,fymn,fzmn,fxmx,fymx,fzmx);
                                gp_Vec toBodyCentre(
                                    (bxmn+bxmx)*0.5 - (fxmn+fxmx)*0.5,
                                    (bymn+bymx)*0.5 - (fymn+fymx)*0.5,
                                    (bzmn+bzmx)*0.5 - (fzmn+fzmx)*0.5);
                                if (faceNormal.Dot(toBodyCentre) > 0) {
                                    faceNormal.Reverse();
                                }
                            }
                        }
                    } catch (...) {}
                }
                Handle(Geom_Surface) surf = BRep_Tool::Surface(tgt.profile);
                gp_Dir axis; bool hasAxis = false;
                if (auto cone = Handle(Geom_ConicalSurface)::DownCast(surf);
                        !cone.IsNull()) { axis = cone->Axis().Direction(); hasAxis = true; }
                else if (auto tor = Handle(Geom_ToroidalSurface)::DownCast(surf);
                        !tor.IsNull()) { axis = tor->Axis().Direction(); hasAxis = true; }
                else if (auto cyl = Handle(Geom_CylindricalSurface)::DownCast(surf);
                        !cyl.IsNull()) { axis = cyl->Axis().Direction(); hasAxis = true; }
                else if (auto rev = Handle(Geom_SurfaceOfRevolution)::DownCast(surf);
                        !rev.IsNull()) { axis = rev->Axis().Direction(); hasAxis = true; }
                if (hasAxis) {
                    gp_Vec axisVec(axis);
                    if (axisVec.Dot(faceNormal) < 0) axisVec.Reverse();
                    faceNormal = axisVec.Normalized();
                }
            }
        } catch (...) {}

        // Sign of distance determines prism direction & boolean mode
        double dir = m_distance >= 0.0 ? 1.0 : -1.0;
        gp_Vec prismVec = faceNormal * (std::abs(m_distance) * dir);

        TopoDS_Shape prism;
        try {
            BRepPrimAPI_MakePrism mk(tgt.profile, prismVec);
            mk.Build();
            if (!mk.IsDone()) continue;
            prism = mk.Shape();
        } catch (...) { continue; }

        if (tgt.sourceBodyId >= 0) {
            // Save original (once per body)
            if (!savedBodies.count(tgt.sourceBodyId)) {
                try {
                    m_previousBodies.emplace_back(tgt.sourceBodyId,
                                                  doc.getBody(tgt.sourceBodyId));
                    savedBodies.insert(tgt.sourceBodyId);
                } catch (...) { continue; }
            }

            TopoDS_Shape current = doc.getBody(tgt.sourceBodyId);
            TopoDS_Shape result;
            try {
                if (m_distance > 0) {
                    BRepAlgoAPI_Fuse fuse(current, prism);
                    fuse.Build();
                    if (!fuse.IsDone()) continue;
                    result = fuse.Shape();
                } else {
                    BRepAlgoAPI_Cut cut(current, prism);
                    cut.Build();
                    if (!cut.IsDone()) continue;
                    result = cut.Shape();
                }
                try {
                    ShapeUpgrade_UnifySameDomain unifier(result, true, true, true);
                    unifier.Build();
                    TopoDS_Shape unified = unifier.Shape();
                    if (!unified.IsNull()) result = unified;
                } catch (...) {}
                doc.updateBody(tgt.sourceBodyId, result);
            } catch (...) { continue; }
        } else {
            // Free-floating: create a new body. On redo, m_reuseBodyIds holds
            // the ids from the previous execute so addOrPutBody picks the
            // same one back up (Document's tombstone restore then brings the
            // folder/colour/visibility/name back).
            int id = (m_reuseIdx < m_reuseBodyIds.size())
                       ? m_reuseBodyIds[m_reuseIdx] : -1;
            doc.addOrPutBody(id, prism, m_distance > 0 ? "Push" : "Pull");
            m_createdBodyIds.push_back(id);
            ++m_reuseIdx;
        }
    }

    return !m_previousBodies.empty() || !m_createdBodyIds.empty();
}

bool PushPullOp::undo(Document& doc) {
    try {
        // Remove created bodies first
        for (int id : m_createdBodyIds) {
            doc.removeBody(id);
        }
        // Move the just-removed ids into the reuse pool so the next execute
        // (redo) reinserts each free-floating result under the same id and
        // recovers the tombstoned metadata.
        m_reuseBodyIds = std::move(m_createdBodyIds);
        m_createdBodyIds.clear();
        m_reuseIdx = 0;
        // Restore mutated bodies
        for (const auto& [id, shape] : m_previousBodies) {
            doc.updateBody(id, shape);
        }
        m_previousBodies.clear();
        return true;
    } catch (...) {
        return false;
    }
}

std::string PushPullOp::description() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Push/Pull %.2f mm (%zu region%s)",
                  m_distance, m_targets.size(), m_targets.size() == 1 ? "" : "s");
    return buf;
}

void PushPullOp::renderProperties() {
    ImGui::Text("Push/Pull");
    ImGui::Separator();
    ImGui::InputDouble("Distance", &m_distance, 0.1, 1.0, "%.3f");
    ImGui::Text("Regions: %zu", m_targets.size());
}

OperationDiff PushPullOp::captureDiff() const {
    OperationDiff d;
    for (const auto& [id, shape] : m_previousBodies)
        if (id >= 0 && !shape.IsNull()) d.modifiedBefore.push_back({id, shape});
    for (int id : m_createdBodyIds)
        if (id >= 0) d.created.push_back(id);
    return d;
}

std::string PushPullOp::serializeParams() const {
    // Profiles are NOT stored — each sketch-sourced target re-derives its face
    // from (sketch id, region index) on reload. Targets without a sketch
    // source (a push/pull on a bare body face) still serialise their scalars,
    // but rehydrateFromReload declines them — the face reference needs
    // persistent topological naming to survive a reload.
    std::string blob;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "dist=%.6f;count=%d",
                  m_distance, static_cast<int>(m_targets.size()));
    blob += buf;
    for (size_t i = 0; i < m_targets.size(); ++i) {
        int sk = (i < m_sketchSourceIds.size())     ? m_sketchSourceIds[i]     : -1;
        int rg = (i < m_sketchSourceRegions.size()) ? m_sketchSourceRegions[i] : -1;
        std::snprintf(buf, sizeof(buf), ";s%zu=%d;r%zu=%d;b%zu=%d",
                      i, sk, i, rg, i, m_targets[i].sourceBodyId);
        blob += buf;
    }
    return blob;
}

bool PushPullOp::deserializeParams(const std::string& blob) {
    bool any = false;
    int count = 0;
    // First pass: scalars + target count, so the vectors can be sized before
    // the per-target keys land.
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if      (key == "dist")  { m_distance = std::atof(val.c_str()); any = true; }
        else if (key == "count") { count = std::atoi(val.c_str()); any = true; }
        pos = end + 1;
    }
    if (count <= 0) return any;
    m_targets.assign(count, Target{});          // profiles rebuilt on rehydrate
    m_sketchSourceIds.assign(count, -1);
    m_sketchSourceRegions.assign(count, -1);
    pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if (key.size() >= 2 && (key[0] == 's' || key[0] == 'r' || key[0] == 'b')) {
            int idx = std::atoi(key.c_str() + 1);
            int v   = std::atoi(val.c_str());
            if (idx >= 0 && idx < count) {
                if      (key[0] == 's') m_sketchSourceIds[idx]      = v;
                else if (key[0] == 'r') m_sketchSourceRegions[idx]  = v;
                else                    m_targets[idx].sourceBodyId = v;
                any = true;
            }
        }
        pos = end + 1;
    }
    return any;
}

bool PushPullOp::rehydrateFromReload(const ReloadState& state, Document& doc) {
    if (m_targets.empty()) return false;
    // Every target must be sketch-sourced to re-derive its profile; a single
    // face-driven target poisons the whole op (its face can't be rebuilt), so
    // decline and let the loader fall back to a baked ReplayOp.
    for (size_t i = 0; i < m_targets.size(); ++i) {
        if (i >= m_sketchSourceIds.size() || m_sketchSourceIds[i] < 0) return false;
    }
    // Rebuild each distinct source sketch's targets (the helper fills every
    // target bound to that sketch in one call).
    for (size_t i = 0; i < m_targets.size(); ++i) {
        if (!m_targets[i].profile.IsNull()) continue; // already rebuilt
        if (!rebuildProfileFromSketch(doc, m_sketchSourceIds[i])) return false;
    }
    for (const auto& t : m_targets) {
        if (t.profile.IsNull()) return false;
    }
    // A push/pull must have created or mutated SOMETHING; both sets empty
    // means the step's diff is missing from the file — decline so undo
    // doesn't silently no-op.
    if (state.created.empty() && state.modifiedBefore.empty()) return false;
    // Post-execution bookkeeping from the saved step's body delta, so undo()
    // removes/restores exactly the right bodies and a distance edit re-executes
    // under the same ids (tombstone metadata included).
    m_createdBodyIds = state.created;
    m_previousBodies = state.modifiedBefore;
    m_reuseBodyIds.clear();
    m_reuseIdx = 0;
    return true;
}
