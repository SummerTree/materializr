#include "CopyOp.h"
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <imgui.h>

CopyOp::CopyOp() = default;

void CopyOp::setSourceBodyId(int id) {
    m_sourceBodyId = id;
}

void CopyOp::setOffset(double dx, double dy, double dz) {
    m_dx = dx;
    m_dy = dy;
    m_dz = dz;
}

bool CopyOp::execute(Document& doc) {
    if (m_sourceBodyId < 0) {
        return false;
    }

    try {
        // Get the source shape
        const TopoDS_Shape& sourceShape = doc.getBody(m_sourceBodyId);

        // Apply translation offset
        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(m_dx, m_dy, m_dz));

        BRepBuilderAPI_Transform transform(sourceShape, trsf, true);
        transform.Build();
        if (!transform.IsDone()) {
            return false;
        }

        // Add as a new body with a descriptive name
        std::string srcName = doc.getBodyName(m_sourceBodyId);
        std::string copyName = srcName.empty() ? "Copy" : srcName + " Copy";
        // Reuse the prior id on redo so the body's folder/colour/etc. survive
        // through undo+redo via Document's tombstone restore.
        doc.addOrPutBody(m_createdBodyId, transform.Shape(), copyName);

        return true;
    } catch (...) {
        return false;
    }
}

bool CopyOp::undo(Document& doc) {
    try {
        if (m_createdBodyId >= 0) {
            doc.removeBody(m_createdBodyId);
            // Keep m_createdBodyId for the redo path.
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string CopyOp::description() const {
    return "Duplicate body " + std::to_string(m_sourceBodyId) +
           " offset (" + std::to_string(m_dx) + ", " +
           std::to_string(m_dy) + ", " + std::to_string(m_dz) + ")";
}

void CopyOp::renderProperties() {
    ImGui::Text("Duplicate");
    ImGui::Separator();

    ImGui::InputInt("Source Body ID", &m_sourceBodyId);

    ImGui::Text("Offset");
    ImGui::InputDouble("X", &m_dx, 0.1, 1.0, "%.3f");
    ImGui::InputDouble("Y", &m_dy, 0.1, 1.0, "%.3f");
    ImGui::InputDouble("Z", &m_dz, 0.1, 1.0, "%.3f");

    if (m_createdBodyId >= 0) {
        ImGui::Text("Created body ID: %d", m_createdBodyId);
    }
}

OperationDiff CopyOp::captureDiff() const {
    OperationDiff d;
    if (m_createdBodyId >= 0) d.created.push_back(m_createdBodyId);
    return d;
}
