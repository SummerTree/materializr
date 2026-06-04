#include "SweepOp.h"
#include <BRepOffsetAPI_MakePipe.hxx>
#include <TopoDS.hxx>
#include <imgui.h>

SweepOp::SweepOp() = default;

void SweepOp::setProfile(const TopoDS_Shape& profile) {
    m_profile = profile;
}

void SweepOp::setPath(const TopoDS_Wire& path) {
    m_path = path;
}

bool SweepOp::execute(Document& doc) {
    if (m_profile.IsNull() || m_path.IsNull()) {
        return false;
    }

    try {
        BRepOffsetAPI_MakePipe pipe(m_path, m_profile);
        pipe.Build();
        if (!pipe.IsDone()) {
            return false;
        }

        TopoDS_Shape sweptShape = pipe.Shape();
        doc.addOrPutBody(m_createdBodyId, sweptShape, "Sweep");

        return true;
    } catch (...) {
        return false;
    }
}

bool SweepOp::undo(Document& doc) {
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

std::string SweepOp::description() const {
    return "Sweep profile along path";
}

void SweepOp::renderProperties() {
    ImGui::Text("Sweep");
    ImGui::Separator();

    if (m_profile.IsNull()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No profile selected");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Profile set");
    }

    if (m_path.IsNull()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No path selected");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Path set");
    }

    ImGui::Separator();
    ImGui::TextWrapped("Select a profile (face or wire) and a path (wire) to sweep along.");
}

OperationDiff SweepOp::captureDiff() const {
    OperationDiff d;
    if (m_createdBodyId >= 0) d.created.push_back(m_createdBodyId);
    return d;
}
