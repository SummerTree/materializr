#include "BooleanOp.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <imgui.h>

BooleanOp::BooleanOp() = default;

void BooleanOp::setTargetBodyId(int id) {
    m_targetBodyId = id;
}

void BooleanOp::setToolBodyId(int id) {
    m_toolBodyId = id;
}

void BooleanOp::setMode(BooleanMode mode) {
    m_mode = mode;
}

bool BooleanOp::execute(Document& doc) {
    if (m_targetBodyId < 0 || m_toolBodyId < 0) {
        return false;
    }

    try {
        // Store previous shapes for undo
        m_previousTargetShape = doc.getBody(m_targetBodyId);
        m_previousToolShape = doc.getBody(m_toolBodyId);

        TopoDS_Shape resultShape;

        switch (m_mode) {
            case BooleanMode::Union: {
                BRepAlgoAPI_Fuse fuse(m_previousTargetShape, m_previousToolShape);
                fuse.Build();
                if (!fuse.IsDone()) {
                    return false;
                }
                resultShape = fuse.Shape();
                // Merge coplanar/tangent neighbouring faces so the union doesn't leave
                // a spurious seam edge between the two original bodies.
                try {
                    ShapeUpgrade_UnifySameDomain unifier(resultShape,
                                                        /*UnifyEdges*/ true,
                                                        /*UnifyFaces*/ true,
                                                        /*ConcatBSplines*/ true);
                    unifier.Build();
                    TopoDS_Shape unified = unifier.Shape();
                    if (!unified.IsNull()) resultShape = unified;
                } catch (...) { /* fall back to un-unified result */ }
                break;
            }
            case BooleanMode::Subtract: {
                BRepAlgoAPI_Cut cut(m_previousTargetShape, m_previousToolShape);
                cut.Build();
                if (!cut.IsDone()) {
                    return false;
                }
                resultShape = cut.Shape();
                break;
            }
            case BooleanMode::Intersect: {
                BRepAlgoAPI_Common common(m_previousTargetShape, m_previousToolShape);
                common.Build();
                if (!common.IsDone()) {
                    return false;
                }
                resultShape = common.Shape();
                break;
            }
        }

        // Update target body with the result
        doc.updateBody(m_targetBodyId, resultShape);

        // Remove the tool body
        doc.removeBody(m_toolBodyId);
        m_removedToolId = m_toolBodyId;

        return true;
    } catch (...) {
        return false;
    }
}

bool BooleanOp::undo(Document& doc) {
    try {
        // Restore target body to previous shape
        if (m_targetBodyId >= 0 && !m_previousTargetShape.IsNull()) {
            doc.updateBody(m_targetBodyId, m_previousTargetShape);
        }

        // Re-add the tool body that was removed
        if (m_removedToolId >= 0 && !m_previousToolShape.IsNull()) {
            doc.addBody(m_previousToolShape, "Boolean Tool (restored)");
            m_removedToolId = -1;
        }

        return true;
    } catch (...) {
        return false;
    }
}

std::string BooleanOp::description() const {
    std::string modeStr;
    switch (m_mode) {
        case BooleanMode::Union:     modeStr = "Union"; break;
        case BooleanMode::Subtract:  modeStr = "Subtract"; break;
        case BooleanMode::Intersect: modeStr = "Intersect"; break;
    }
    return "Boolean " + modeStr + " (body " + std::to_string(m_targetBodyId) +
           " with body " + std::to_string(m_toolBodyId) + ")";
}

void BooleanOp::renderProperties() {
    ImGui::Text("Boolean Operation");
    ImGui::Separator();

    const char* modeItems[] = { "Union", "Subtract", "Intersect" };
    int modeIndex = static_cast<int>(m_mode);
    if (ImGui::Combo("Mode", &modeIndex, modeItems, 3)) {
        m_mode = static_cast<BooleanMode>(modeIndex);
    }

    ImGui::InputInt("Target Body ID", &m_targetBodyId);
    ImGui::InputInt("Tool Body ID", &m_toolBodyId);
}

OperationDiff BooleanOp::captureDiff() const {
    OperationDiff d;
    // The target mutates in place; the tool body is consumed by the boolean.
    if (m_targetBodyId >= 0 && !m_previousTargetShape.IsNull())
        d.modifiedBefore.push_back({m_targetBodyId, m_previousTargetShape});
    if (m_toolBodyId >= 0 && !m_previousToolShape.IsNull())
        d.deletedBefore.push_back({m_toolBodyId, m_previousToolShape});
    return d;
}
