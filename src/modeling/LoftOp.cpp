#include "LoftOp.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GProp_GProps.hxx>
#include <Standard_ErrorHandler.hxx> // OCC_CATCH_SIGNALS
#include <BRepAlgoAPI_Cut.hxx>
#include <TopoDS.hxx>
#include <imgui.h>

LoftOp::LoftOp() = default;

void LoftOp::addProfile(const TopoDS_Wire& wire) {
    m_profiles.push_back(wire);
    m_holeProfiles.emplace_back(); // no holes for this profile
}

void LoftOp::addProfile(const TopoDS_Wire& outer, const std::vector<TopoDS_Wire>& holes) {
    m_profiles.push_back(outer);
    m_holeProfiles.push_back(holes);
}

void LoftOp::clearProfiles() {
    m_profiles.clear();
    m_holeProfiles.clear();
}

void LoftOp::setSolid(bool solid) {
    m_solid = solid;
}

void LoftOp::setRuled(bool ruled) {
    m_ruled = ruled;
}

bool LoftOp::execute(Document& doc) {
    if (m_profiles.size() < 2) {
        return false;
    }

    try {
        // Degenerate section stacks (e.g. perpendicular "wall" profiles that
        // make the surface fold through itself) can drive ThruSections to a
        // kernel FAULT, not just a clean failure. OCC_CATCH_SIGNALS turns that
        // signal into a Standard_Failure the catch below absorbs — without it
        // the app dies (crash reproduced by repeated preview/cancel on a
        // weaving 3-section loft).
        OCC_CATCH_SIGNALS
        BRepOffsetAPI_ThruSections thruSections(m_solid ? Standard_True : Standard_False,
                                                 m_ruled ? Standard_True : Standard_False);

        for (const auto& wire : m_profiles) {
            thruSections.AddWire(wire);
        }

        thruSections.Build();
        if (!thruSections.IsDone()) {
            return false;
        }

        TopoDS_Shape loftedShape = thruSections.Shape();

        // Tube support: if the profiles carry holes (e.g. concentric circles),
        // loft each hole-channel into its own inner solid and cut it from the
        // outer loft. Only meaningful for a solid loft. Hole k is matched by
        // index across the profiles, and we require every profile to expose the
        // same number of holes so the channels pair up unambiguously.
        if (m_solid && !m_holeProfiles.empty()) {
            size_t nHoles = m_holeProfiles[0].size();
            bool uniform = nHoles > 0;
            for (const auto& hp : m_holeProfiles) {
                if (hp.size() != nHoles) { uniform = false; break; }
            }
            for (size_t k = 0; uniform && k < nHoles; ++k) {
                BRepOffsetAPI_ThruSections inner(Standard_True, // solid
                                                 m_ruled ? Standard_True : Standard_False);
                for (const auto& hp : m_holeProfiles) {
                    inner.AddWire(hp[k]);
                }
                inner.Build();
                if (!inner.IsDone()) continue; // skip a hole that won't loft
                BRepAlgoAPI_Cut cut(loftedShape, inner.Shape());
                cut.Build();
                if (!cut.IsDone()) continue;
                // Adopt the cut only if it's still a usable solid — a bad hole
                // channel can yield a null/empty/invalid result that would
                // otherwise replace a perfectly good outer loft.
                TopoDS_Shape cutShape = cut.Shape();
                if (cutShape.IsNull()) continue;
                GProp_GProps cutProps;
                BRepGProp::VolumeProperties(cutShape, cutProps);
                if (cutProps.Mass() < 1e-6) continue;
                if (!BRepCheck_Analyzer(cutShape).IsValid()) continue;
                loftedShape = cutShape;
            }
        }

        // Validate-or-refuse (same gate as BooleanOp/FilletOp/ShellOp): a
        // degenerate section stack can pass IsDone() yet produce a null or
        // topologically invalid shape that later crashes tessellation/save.
        // The volume check only applies to solid lofts — a surface loft
        // legitimately encloses no volume.
        if (loftedShape.IsNull()) return false;
        if (m_solid) {
            GProp_GProps gp;
            BRepGProp::VolumeProperties(loftedShape, gp);
            if (gp.Mass() < 1e-6) return false;
        }
        if (!BRepCheck_Analyzer(loftedShape).IsValid()) return false;

        doc.addOrPutBody(m_createdBodyId, loftedShape, "Loft");

        return true;
    } catch (...) {
        return false;
    }
}

bool LoftOp::undo(Document& doc) {
    try {
        if (m_createdBodyId >= 0) {
            doc.removeBody(m_createdBodyId);
            // Keep m_createdBodyId — tombstone restore on next execute().
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string LoftOp::description() const {
    std::string desc = "Loft through " + std::to_string(m_profiles.size()) + " profiles";
    if (m_solid) {
        desc += " (Solid)";
    } else {
        desc += " (Shell)";
    }
    if (m_ruled) {
        desc += " Ruled";
    }
    return desc;
}

void LoftOp::renderProperties() {
    ImGui::Text("Loft");
    ImGui::Separator();

    ImGui::Text("Profiles: %d", static_cast<int>(m_profiles.size()));

    if (m_profiles.size() < 2) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "At least 2 profiles required");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           "%d profiles ready", static_cast<int>(m_profiles.size()));
    }

    ImGui::Separator();
    ImGui::Checkbox("Solid", &m_solid);
    ImGui::Checkbox("Ruled Surface", &m_ruled);
}

OperationDiff LoftOp::captureDiff() const {
    OperationDiff d;
    if (m_createdBodyId >= 0) d.created.push_back(m_createdBodyId);
    return d;
}

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS.hxx>
#include <sstream>

std::string LoftOp::serializeParams() const {
    // Profiles are raw wires (picked from sketch regions / body loops at
    // create time) — no persistent source ids exist, so they persist as an
    // ASCII BREP compound embedded in the params. PARAMS_LEN stores raw
    // bytes, so the multi-line BREP is safe; it goes LAST, length-prefixed.
    // Compound order: profile0, its holes..., profile1, its holes..., etc.
    std::string blob = "solid=" + std::to_string(m_solid ? 1 : 0) +
                       ";ruled=" + std::to_string(m_ruled ? 1 : 0) +
                       ";created=" + std::to_string(m_createdBodyId) +
                       ";np=" + std::to_string(m_profiles.size());
    BRep_Builder bb;
    TopoDS_Compound comp;
    bb.MakeCompound(comp);
    for (size_t i = 0; i < m_profiles.size(); ++i) {
        const size_t nh = i < m_holeProfiles.size() ? m_holeProfiles[i].size() : 0;
        blob += ";h" + std::to_string(i) + "=" + std::to_string(nh);
        bb.Add(comp, m_profiles[i]);
        for (size_t j = 0; j < nh; ++j) bb.Add(comp, m_holeProfiles[i][j]);
    }
    std::ostringstream os;
    BRepTools::Write(comp, os);
    const std::string brep = os.str();
    blob += ";brep=" + std::to_string(brep.size()) + ":" + brep;
    return blob;
}

bool LoftOp::deserializeParams(const std::string& blob) {
    m_profiles.clear();
    m_holeProfiles.clear();
    std::vector<int> holeCounts;
    int np = 0;
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        std::string key = blob.substr(pos, eq - pos);
        if (key == "brep") {
            // <len>:<raw ascii brep>, runs to end.
            size_t colon = blob.find(':', eq);
            if (colon == std::string::npos) break;
            size_t n = static_cast<size_t>(
                std::atoll(blob.substr(eq + 1, colon - eq - 1).c_str()));
            if (colon + 1 + n > blob.size()) break;
            std::istringstream is(blob.substr(colon + 1, n));
            TopoDS_Shape comp;
            BRep_Builder bb;
            try { BRepTools::Read(comp, is, bb); } catch (...) { return false; }
            // Unpack: per profile i, one wire + holeCounts[i] hole wires.
            TopoDS_Iterator it(comp);
            for (int i = 0; i < np && it.More(); ++i) {
                if (it.Value().ShapeType() != TopAbs_WIRE) return false;
                m_profiles.push_back(TopoDS::Wire(it.Value()));
                it.Next();
                std::vector<TopoDS_Wire> holes;
                int nh = i < static_cast<int>(holeCounts.size()) ? holeCounts[i] : 0;
                for (int j = 0; j < nh && it.More(); ++j) {
                    holes.push_back(TopoDS::Wire(it.Value()));
                    it.Next();
                }
                m_holeProfiles.push_back(std::move(holes));
            }
            any = true;
            break;
        }
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if      (key == "solid")   { m_solid = val == "1"; any = true; }
        else if (key == "ruled")   { m_ruled = val == "1"; any = true; }
        else if (key == "created") { m_createdBodyId = std::atoi(val.c_str()); any = true; }
        else if (key == "np")      { np = std::atoi(val.c_str()); any = true; }
        else if (!key.empty() && key[0] == 'h') {
            int idx = std::atoi(key.c_str() + 1);
            if (idx >= static_cast<int>(holeCounts.size()))
                holeCounts.resize(idx + 1, 0);
            holeCounts[idx] = std::atoi(val.c_str());
        }
        pos = end + 1;
    }
    return any && static_cast<int>(m_profiles.size()) == np && np >= 2;
}

bool LoftOp::rehydrateFromReload(const ReloadState& state, Document&) {
    if (m_profiles.size() < 2) return false;
    if (m_createdBodyId < 0 && !state.created.empty())
        m_createdBodyId = state.created.front();
    return true;   // profiles are self-contained; execute() re-lofts them
}
