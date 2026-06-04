#include "LoftOp.h"
#include <BRepOffsetAPI_ThruSections.hxx>
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
                if (cut.IsDone()) loftedShape = cut.Shape();
            }
        }

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
