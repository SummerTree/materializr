#include "MirrorOp.h"
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <imgui.h>

MirrorOp::MirrorOp() = default;

void MirrorOp::setBody(int id) {
    m_bodyId = id;
}

void MirrorOp::setPlane(MirrorPlane p) {
    m_plane = p;
}

void MirrorOp::setCustomPlane(const gp_Ax2& ax) {
    m_customPlane = ax;
}

void MirrorOp::setKeepOriginal(bool keep) {
    m_keepOriginal = keep;
}

gp_Ax2 MirrorOp::getMirrorAxis() const {
    gp_Pnt origin(0, 0, 0);
    switch (m_plane) {
        case MirrorPlane::XY:
            return gp_Ax2(origin, gp_Dir(0, 0, 1)); // normal along Z
        case MirrorPlane::XZ:
            return gp_Ax2(origin, gp_Dir(0, 1, 0)); // normal along Y
        case MirrorPlane::YZ:
            return gp_Ax2(origin, gp_Dir(1, 0, 0)); // normal along X
        case MirrorPlane::Custom:
            return m_customPlane;
    }
    return gp_Ax2(origin, gp_Dir(1, 0, 0));
}

bool MirrorOp::execute(Document& doc) {
    if (m_bodyId < 0) {
        return false;
    }

    try {
        // Store previous shape for undo
        m_previousShape = doc.getBody(m_bodyId);

        // Create mirror transformation
        gp_Trsf trsf;
        trsf.SetMirror(getMirrorAxis());

        BRepBuilderAPI_Transform transform(m_previousShape, trsf, true);
        transform.Build();
        if (!transform.IsDone()) {
            return false;
        }

        TopoDS_Shape mirroredShape = transform.Shape();

        if (m_keepOriginal) {
            // Reuse prior id on redo to preserve folder/colour/etc. via the
            // Document tombstone restore.
            doc.addOrPutBody(m_mirroredBodyId, mirroredShape, "Mirror");
        } else {
            // Replace the original body with the mirrored version
            doc.updateBody(m_bodyId, mirroredShape);
            m_mirroredBodyId = -1;
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool MirrorOp::undo(Document& doc) {
    try {
        if (m_keepOriginal) {
            // Remove the mirrored body
            if (m_mirroredBodyId >= 0) {
                doc.removeBody(m_mirroredBodyId);
                // Keep m_mirroredBodyId so redo via addOrPutBody picks up the
                // tombstone-stashed folder/colour/etc.
            }
        } else {
            // Restore the original shape
            if (m_bodyId >= 0 && !m_previousShape.IsNull()) {
                doc.updateBody(m_bodyId, m_previousShape);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string MirrorOp::description() const {
    std::string planeStr;
    switch (m_plane) {
        case MirrorPlane::XY: planeStr = "XY"; break;
        case MirrorPlane::XZ: planeStr = "XZ"; break;
        case MirrorPlane::YZ: planeStr = "YZ"; break;
        case MirrorPlane::Custom: planeStr = "Custom"; break;
    }
    return "Mirror across " + planeStr + (m_keepOriginal ? " (keep original)" : "");
}

void MirrorOp::renderProperties() {
    ImGui::Text("Mirror");
    ImGui::Separator();

    const char* planeItems[] = { "XY", "XZ", "YZ", "Custom" };
    int planeIndex = static_cast<int>(m_plane);
    if (ImGui::Combo("Mirror Plane", &planeIndex, planeItems, 4)) {
        m_plane = static_cast<MirrorPlane>(planeIndex);
    }

    ImGui::Checkbox("Keep Original", &m_keepOriginal);

    ImGui::Text("Body ID: %d", m_bodyId);

    if (m_mirroredBodyId >= 0) {
        ImGui::Text("Mirrored body ID: %d", m_mirroredBodyId);
    }
}

OperationDiff MirrorOp::captureDiff() const {
    OperationDiff d;
    if (m_keepOriginal) {
        if (m_mirroredBodyId >= 0) d.created.push_back(m_mirroredBodyId);
    } else if (m_bodyId >= 0 && !m_previousShape.IsNull()) {
        d.modifiedBefore.push_back({m_bodyId, m_previousShape});
    }
    return d;
}
