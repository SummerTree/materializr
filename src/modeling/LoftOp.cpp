#include "LoftOp.h"
#include <BRepOffsetAPI_ThruSections.hxx>
#include <TopoDS.hxx>
#include <imgui.h>

LoftOp::LoftOp() = default;

void LoftOp::addProfile(const TopoDS_Wire& wire) {
    m_profiles.push_back(wire);
}

void LoftOp::clearProfiles() {
    m_profiles.clear();
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
