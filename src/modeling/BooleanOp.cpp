#include "BooleanOp.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <cstdio>
#include <cstdlib>
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

        // Run the boolean at a given fuzzy tolerance, returning a VALID result
        // shape or null. Null/degenerate are rejected here so the caller can
        // escalate the fuzzy value instead of committing junk. (IsDone() is
        // necessary but not sufficient — OCCT can report success yet hand back a
        // null or zero-volume compound.)
        auto attempt = [&](double fuzzy) -> TopoDS_Shape {
            TopoDS_Shape s;
            try {
                switch (m_mode) {
                    case BooleanMode::Union: {
                        BRepAlgoAPI_Fuse op(m_previousTargetShape, m_previousToolShape);
                        if (fuzzy > 0) op.SetFuzzyValue(fuzzy);
                        op.Build();
                        if (!op.IsDone()) return TopoDS_Shape();
                        s = op.Shape();
                        // Publish face lineage from BOTH inputs so "gen" can
                        // name seam faces/edges by their generating faces.
                        m_ledger.capture(op, m_previousTargetShape, TopAbs_FACE);
                        m_ledger.captureAdd(op, m_previousToolShape, TopAbs_FACE);
                        // Merge coplanar/tangent neighbours so the union has no seam.
                        try {
                            ShapeUpgrade_UnifySameDomain u(s, true, true, true);
                            u.Build();
                            TopoDS_Shape uu = u.Shape();
                            if (!uu.IsNull()) s = uu;
                        } catch (...) {}
                        break;
                    }
                    case BooleanMode::Subtract: {
                        BRepAlgoAPI_Cut op(m_previousTargetShape, m_previousToolShape);
                        if (fuzzy > 0) op.SetFuzzyValue(fuzzy);
                        op.Build();
                        if (!op.IsDone()) return TopoDS_Shape();
                        s = op.Shape();
                        // Publish face lineage from BOTH inputs so "gen" can
                        // name seam faces/edges by their generating faces.
                        m_ledger.capture(op, m_previousTargetShape, TopAbs_FACE);
                        m_ledger.captureAdd(op, m_previousToolShape, TopAbs_FACE);
                        break;
                    }
                    case BooleanMode::Intersect: {
                        BRepAlgoAPI_Common op(m_previousTargetShape, m_previousToolShape);
                        if (fuzzy > 0) op.SetFuzzyValue(fuzzy);
                        op.Build();
                        if (!op.IsDone()) return TopoDS_Shape();
                        s = op.Shape();
                        // Publish face lineage from BOTH inputs so "gen" can
                        // name seam faces/edges by their generating faces.
                        m_ledger.capture(op, m_previousTargetShape, TopAbs_FACE);
                        m_ledger.captureAdd(op, m_previousToolShape, TopAbs_FACE);
                        break;
                    }
                }
            } catch (...) { return TopoDS_Shape(); }
            if (s.IsNull()) return TopoDS_Shape();
            GProp_GProps gp;
            BRepGProp::VolumeProperties(s, gp);
            if (gp.Mass() < 1e-6) return TopoDS_Shape();
            // Reject topologically INVALID results (self-intersections, bad
            // faces) — a fuzzy boolean can return a non-null, non-zero-volume
            // shape that's still garbage. Only a valid solid is worth committing;
            // otherwise the caller escalates the fuzzy value or fails cleanly.
            if (!BRepCheck_Analyzer(s).IsValid()) return TopoDS_Shape();
            return s;
        };

        TopoDS_Shape resultShape = attempt(0.0);
        if (resultShape.IsNull()) {
            // Exact booleans fail on near-coincident / overlapping faces (a body
            // sitting flush on another, a thin sliver of overlap). A TINY fuzzy
            // tolerance usually resolves it. Keep it sub-micron-to-micron: larger
            // values (it used to go to 0.1 mm) let OCCT snap distant entities
            // together and visibly distort the model.
            for (double f : {1e-5, 1e-4, 1e-3}) {
                resultShape = attempt(f);
                if (!resultShape.IsNull()) {
                    std::fprintf(stderr, "[Boolean] %s succeeded with fuzzy=%.4g "
                                 "(target=%d tool=%d)\n",
                                 m_mode == BooleanMode::Subtract ? "Cut" :
                                 m_mode == BooleanMode::Union ? "Fuse" : "Common",
                                 f, m_targetBodyId, m_toolBodyId);
                    break;
                }
            }
        }
        if (resultShape.IsNull()) {
            std::fprintf(stderr, "[Boolean] %s failed (target=%d tool=%d) even "
                         "with fuzzy — bodies may not overlap, or the geometry is "
                         "too degenerate.\n",
                         m_mode == BooleanMode::Subtract ? "Cut" :
                         m_mode == BooleanMode::Union ? "Fuse" : "Common",
                         m_targetBodyId, m_toolBodyId);
            return false;
        }

        // Update target body with the result
        doc.updateBody(m_targetBodyId, resultShape);
        doc.setBodyLedger(m_targetBodyId, &m_ledger);

        // Remove the tool body — unless we're keeping it (the "keep cutters"
        // option, or a cutter still needed by another target).
        if (m_keepTool) {
            m_removedToolId = -1;
        } else {
            doc.removeBody(m_toolBodyId);
            m_removedToolId = m_toolBodyId;
        }

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

        // Re-add the tool body that was removed — restore it under its ORIGINAL
        // id (not a fresh addBody id). editStep rolls a boolean back then
        // re-executes the steps above it; an upstream op that targets the tool
        // body (e.g. a fillet on it) must still find it by its old id, and the
        // boolean's own re-execute looks the tool up by m_toolBodyId. putBody
        // also pulls folder/colour/visibility back from the tombstone.
        if (m_removedToolId >= 0 && !m_previousToolShape.IsNull()) {
            doc.putBody(m_toolBodyId, m_previousToolShape, "Boolean Tool (restored)");
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
    // The target mutates in place; the tool body is consumed by the boolean
    // unless we kept it (then it isn't deleted, so it's not in the diff).
    if (m_targetBodyId >= 0 && !m_previousTargetShape.IsNull())
        d.modifiedBefore.push_back({m_targetBodyId, m_previousTargetShape});
    if (!m_keepTool && m_toolBodyId >= 0 && !m_previousToolShape.IsNull())
        d.deletedBefore.push_back({m_toolBodyId, m_previousToolShape});
    return d;
}

std::string BooleanOp::serializeParams() const {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "target=%d;tool=%d;mode=%d;keeptool=%d",
                  m_targetBodyId, m_toolBodyId, static_cast<int>(m_mode),
                  m_keepTool ? 1 : 0);
    return buf;
}

bool BooleanOp::deserializeParams(const std::string& blob) {
    // Tolerant key=value parser (same scheme as FilletOp/ChamferOp).
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string key = blob.substr(pos, eq - pos);
        std::string val = blob.substr(eq + 1, end - eq - 1);
        if      (key == "target") { m_targetBodyId = std::atoi(val.c_str()); any = true; }
        else if (key == "tool")   { m_toolBodyId   = std::atoi(val.c_str()); any = true; }
        else if (key == "mode")   { int m = std::atoi(val.c_str());
                                    if (m >= 0 && m <= 2) m_mode = static_cast<BooleanMode>(m);
                                    any = true; }
        else if (key == "keeptool") { m_keepTool = std::atoi(val.c_str()) != 0; any = true; }
        pos = end + 1;
    }
    return any;
}

bool BooleanOp::rehydrateFromReload(const ReloadState& state, Document& /*doc*/) {
    if (m_targetBodyId < 0 || m_toolBodyId < 0) return false;

    // Restore the pre-boolean shapes from the saved step diff: the target was
    // modified in place, the tool was consumed (deleted). Both are needed so
    // undo()/redo() and an editStep replay can roll the boolean back and re-run
    // it against the (possibly edited) upstream geometry.
    m_previousTargetShape.Nullify();
    m_previousToolShape.Nullify();
    for (const auto& [id, shp] : state.modifiedBefore)
        if (id == m_targetBodyId) { m_previousTargetShape = shp; break; }
    for (const auto& [id, shp] : state.deletedBefore)
        if (id == m_toolBodyId) { m_previousToolShape = shp; break; }
    if (m_previousTargetShape.IsNull()) return false;

    if (m_keepTool) {
        // The tool wasn't consumed, so it isn't in the step's deleted set — it's
        // still a live body. execute() re-fetches it; nothing to restore on undo.
        m_removedToolId = -1;
    } else {
        if (m_previousToolShape.IsNull()) return false;
        // Post-execution bookkeeping: this step consumed the tool body, so undo()
        // knows to restore it.
        m_removedToolId = m_toolBodyId;
    }
    return true;
}
