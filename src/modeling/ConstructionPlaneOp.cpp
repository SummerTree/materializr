#include "ConstructionPlaneOp.h"
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Pnt.hxx>
#include <imgui.h>
#include <cmath>

ConstructionPlaneOp::ConstructionPlaneOp() = default;

void ConstructionPlaneOp::setType(PlaneCreationType type) {
    m_type = type;
}

void ConstructionPlaneOp::setOffset(double distance) {
    m_offset = distance;
}

void ConstructionPlaneOp::setBasePlane(const gp_Pln& plane) {
    m_basePlane = plane;
}

void ConstructionPlaneOp::setPoints(const gp_Pnt& p1, const gp_Pnt& p2,
                                     const gp_Pnt& p3) {
    m_p1 = p1;
    m_p2 = p2;
    m_p3 = p3;
}

void ConstructionPlaneOp::setName(const std::string& name) {
    m_planeName = name;
}

gp_Pln ConstructionPlaneOp::computePlane() const {
    // Plane names are in the user's Z-up convention (user Z = world Y).
    // What the popup labels "XY" is the floor; its normal is the up axis,
    // which in world coords is +Y. Same remap applies to the other two.
    //   user XY  →  world plane normal = world +Y  (floor; offset is height)
    //   user XZ  →  world plane normal = world +Z  (front/back wall)
    //   user YZ  →  world plane normal = world +X  (left/right wall)
    // Offset slides along that normal so the slider reads as user-Z offset
    // for the floor case, user-Y for the front, user-X for the side.
    switch (m_type) {
        case PlaneCreationType::XY: {
            return gp_Pln(gp_Pnt(0, m_offset, 0), gp_Dir(0, 1, 0));
        }

        case PlaneCreationType::XZ: {
            return gp_Pln(gp_Pnt(0, 0, m_offset), gp_Dir(0, 0, 1));
        }

        case PlaneCreationType::YZ: {
            return gp_Pln(gp_Pnt(m_offset, 0, 0), gp_Dir(1, 0, 0));
        }

        case PlaneCreationType::OffsetFromPlane: {
            // Translate the base plane along its normal by the offset distance
            gp_Dir normal = m_basePlane.Axis().Direction();
            gp_Pnt origin = m_basePlane.Axis().Location();
            gp_Pnt newOrigin(
                origin.X() + normal.X() * m_offset,
                origin.Y() + normal.Y() * m_offset,
                origin.Z() + normal.Z() * m_offset
            );
            return gp_Pln(newOrigin, normal);
        }

        case PlaneCreationType::ThroughThreePoints: {
            // Compute plane from 3 points
            gp_Vec v1(m_p1, m_p2);
            gp_Vec v2(m_p1, m_p3);
            gp_Vec normal = v1.Crossed(v2);

            // Check for degenerate case (collinear points)
            if (normal.Magnitude() < 1e-10) {
                // Fall back to XY plane
                return gp_Pln(m_p1, gp_Dir(0, 0, 1));
            }

            gp_Dir dir(normal);
            return gp_Pln(m_p1, dir);
        }

        case PlaneCreationType::ParallelToFace: {
            // Use the base plane's normal but place it at point p1
            gp_Dir normal = m_basePlane.Axis().Direction();
            return gp_Pln(m_p1, normal);
        }
    }

    // Default: XY
    return gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
}

bool ConstructionPlaneOp::execute(Document& doc) {
    try {
        gp_Pln plane = computePlane();
        m_createdPlaneId = doc.addPlane(plane, m_planeName);
        return m_createdPlaneId >= 0;
    } catch (...) {
        return false;
    }
}

bool ConstructionPlaneOp::undo(Document& doc) {
    // Actually remove the plane now that Document exposes the API. Without
    // this every preview cycle (radio-click XY/XZ/YZ, drag the offset
    // slider) would stack a fresh plane on top of the previous one — the
    // visible "every selection shows" + "offset has no effect" symptoms.
    if (m_createdPlaneId >= 0) {
        doc.removePlane(m_createdPlaneId);
        m_createdPlaneId = -1;
    }
    return true;
}

std::string ConstructionPlaneOp::description() const {
    std::string typeStr;
    switch (m_type) {
        case PlaneCreationType::XY:               typeStr = "XY"; break;
        case PlaneCreationType::XZ:               typeStr = "XZ"; break;
        case PlaneCreationType::YZ:               typeStr = "YZ"; break;
        case PlaneCreationType::OffsetFromPlane:   typeStr = "Offset (" + std::to_string(m_offset) + " mm)"; break;
        case PlaneCreationType::ThroughThreePoints: typeStr = "3 Points"; break;
        case PlaneCreationType::ParallelToFace:    typeStr = "Parallel to Face"; break;
    }
    return "Construction Plane: " + m_planeName + " (" + typeStr + ")";
}

void ConstructionPlaneOp::renderProperties() {
    ImGui::Text("Construction Plane");
    ImGui::Separator();

    // Plane name
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_planeName.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        m_planeName = nameBuf;
    }

    // Plane type
    const char* typeItems[] = {
        "XY", "XZ", "YZ",
        "Offset from Plane",
        "Through 3 Points",
        "Parallel to Face"
    };
    int typeIndex = static_cast<int>(m_type);
    if (ImGui::Combo("Type", &typeIndex, typeItems, 6)) {
        m_type = static_cast<PlaneCreationType>(typeIndex);
    }

    // Type-specific parameters
    switch (m_type) {
        case PlaneCreationType::XY:
        case PlaneCreationType::XZ:
        case PlaneCreationType::YZ:
            ImGui::TextWrapped("Standard reference plane through the origin.");
            break;

        case PlaneCreationType::OffsetFromPlane:
            ImGui::InputDouble("Offset Distance", &m_offset, 0.1, 1.0, "%.3f");
            ImGui::TextWrapped("Creates a plane parallel to the base plane, "
                               "offset along its normal.");
            break;

        case PlaneCreationType::ThroughThreePoints: {
            double coords1[3] = { m_p1.X(), m_p1.Y(), m_p1.Z() };
            double coords2[3] = { m_p2.X(), m_p2.Y(), m_p2.Z() };
            double coords3[3] = { m_p3.X(), m_p3.Y(), m_p3.Z() };

            if (ImGui::InputScalarN("Point 1", ImGuiDataType_Double, coords1, 3, nullptr, nullptr, "%.3f")) {
                m_p1.SetCoord(coords1[0], coords1[1], coords1[2]);
            }
            if (ImGui::InputScalarN("Point 2", ImGuiDataType_Double, coords2, 3, nullptr, nullptr, "%.3f")) {
                m_p2.SetCoord(coords2[0], coords2[1], coords2[2]);
            }
            if (ImGui::InputScalarN("Point 3", ImGuiDataType_Double, coords3, 3, nullptr, nullptr, "%.3f")) {
                m_p3.SetCoord(coords3[0], coords3[1], coords3[2]);
            }
            break;
        }

        case PlaneCreationType::ParallelToFace: {
            double coords[3] = { m_p1.X(), m_p1.Y(), m_p1.Z() };
            if (ImGui::InputScalarN("Through Point", ImGuiDataType_Double, coords, 3, nullptr, nullptr, "%.3f")) {
                m_p1.SetCoord(coords[0], coords[1], coords[2]);
            }
            ImGui::TextWrapped("Creates a plane parallel to the selected face, "
                               "passing through the specified point.");
            break;
        }
    }

    if (m_createdPlaneId >= 0) {
        ImGui::Separator();
        ImGui::Text("Created Plane ID: %d", m_createdPlaneId);
    }
}
