#include "ConstructionAxisOp.h"

#include <imgui.h>
#include <cmath>

ConstructionAxisOp::ConstructionAxisOp() = default;

void ConstructionAxisOp::setType(AxisCreationType type)        { m_type = type; }
void ConstructionAxisOp::setOrigin(const gp_Pnt& origin)        { m_origin = origin; }
void ConstructionAxisOp::setDirection(const gp_Dir& direction)  { m_direction = direction; }
void ConstructionAxisOp::setPoints(const gp_Pnt& p1, const gp_Pnt& p2) {
    m_p1 = p1; m_p2 = p2;
}
void ConstructionAxisOp::setName(const std::string& name)       { m_axisName = name; }

void ConstructionAxisOp::computeAxis(gp_Pnt& outOrigin, gp_Dir& outDir) const {
    switch (m_type) {
        case AxisCreationType::WorldX:
            outOrigin = m_origin;
            outDir = gp_Dir(1, 0, 0);
            return;
        case AxisCreationType::WorldY:
            // user Z (up) maps to world Y. "WorldY" here is the actual world
            // Y direction, which in user Z-up convention is the up axis.
            outOrigin = m_origin;
            outDir = gp_Dir(0, 1, 0);
            return;
        case AxisCreationType::WorldZ:
            outOrigin = m_origin;
            outDir = gp_Dir(0, 0, 1);
            return;
        case AxisCreationType::TwoPoints: {
            outOrigin = m_p1;
            gp_Vec v(m_p1, m_p2);
            if (v.Magnitude() > 1e-9) {
                outDir = gp_Dir(v);
            } else {
                outDir = gp_Dir(1, 0, 0);
            }
            return;
        }
        case AxisCreationType::ThroughFaceNormal:
            // Caller fills m_origin (a point on the face) and m_direction
            // (the face's normal). Just pass through.
            outOrigin = m_origin;
            outDir = m_direction;
            return;
    }
    outOrigin = m_origin;
    outDir = gp_Dir(1, 0, 0);
}

bool ConstructionAxisOp::execute(Document& doc) {
    try {
        gp_Pnt o; gp_Dir d;
        computeAxis(o, d);
        m_createdAxisId = doc.addAxis(o, d, m_axisName);
        return m_createdAxisId >= 0;
    } catch (...) {
        return false;
    }
}

bool ConstructionAxisOp::undo(Document& doc) {
    if (m_createdAxisId >= 0) {
        doc.removeAxis(m_createdAxisId);
        m_createdAxisId = -1;
    }
    return true;
}

std::string ConstructionAxisOp::description() const {
    const char* typeStr = "X";
    switch (m_type) {
        case AxisCreationType::WorldX:            typeStr = "World X"; break;
        case AxisCreationType::WorldY:            typeStr = "World Y (Z-up)"; break;
        case AxisCreationType::WorldZ:            typeStr = "World Z"; break;
        case AxisCreationType::TwoPoints:         typeStr = "Two points"; break;
        case AxisCreationType::ThroughFaceNormal: typeStr = "Face normal"; break;
    }
    return std::string("Construction Axis (") + typeStr + ")";
}

void ConstructionAxisOp::renderProperties() {
    ImGui::Text("Construction Axis");
    ImGui::Separator();
    ImGui::Text("Name: %s", m_axisName.c_str());
    ImGui::Text("Origin: (%.2f, %.2f, %.2f)",
                m_origin.X(), m_origin.Y(), m_origin.Z());
    ImGui::Text("Direction: (%.3f, %.3f, %.3f)",
                m_direction.X(), m_direction.Y(), m_direction.Z());
}
