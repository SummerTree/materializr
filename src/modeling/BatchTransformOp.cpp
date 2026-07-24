#include "BatchTransformOp.h"

#include <BRepBuilderAPI_GTransform.hxx>
#include <cstdio>
#include <cstdlib>
#include <string>

bool BatchTransformOp::execute(Document& doc) {
    if (m_bodyIds.empty()) return false;
    try {
        m_previousShapes.clear();
        m_prevFaceIds.clear();
        for (int id : m_bodyIds) {
            TopoDS_Shape before;
            try { before = doc.getBody(id); } catch (...) { continue; }
            if (before.IsNull()) continue;

            // Face lineage in — carried 1:1 through the move so a downstream
            // fillet/chamfer keeps resolving its edges; restored by undo.
            materializr::topo::FaceIdMap inMap;
            if (const auto* im = doc.bodyFaceIds(id)) inMap = *im;

            BRepBuilderAPI_GTransform tf(before, m_gtrsf, /*copy=*/true);
            if (!tf.IsDone() || tf.Shape().IsNull()) return false;
            TopoDS_Shape after = tf.Shape();

            m_previousShapes.push_back({id, before});
            m_prevFaceIds[id] = inMap;
            doc.updateBody(id, after);

            if (!inMap.empty()) {
                materializr::topo::FaceIdMap moved;
                for (const auto& e : inMap) {
                    try {
                        TopoDS_Shape nf = tf.ModifiedShape(e.face);
                        if (!nf.IsNull()) moved.push_back({nf, e.ids});
                    } catch (...) {}
                }
                if (!moved.empty()) doc.setBodyFaceIds(id, std::move(moved));
            }
        }
        return !m_previousShapes.empty();
    } catch (...) {
        return false;
    }
}

bool BatchTransformOp::undo(Document& doc) {
    try {
        for (const auto& [id, shp] : m_previousShapes) {
            doc.updateBody(id, shp);
            auto it = m_prevFaceIds.find(id);
            if (it != m_prevFaceIds.end() && !it->second.empty())
                doc.setBodyFaceIds(id, it->second);
        }
        m_previousShapes.clear();
        return true;
    } catch (...) {
        return false;
    }
}

OperationDiff BatchTransformOp::captureDiff() const {
    OperationDiff d;
    for (const auto& [id, shp] : m_previousShapes)
        if (!shp.IsNull()) d.modifiedBefore.push_back({id, shp});
    return d;
}

std::string BatchTransformOp::serializeParams() const {
    std::string s = "bodies=";
    for (size_t i = 0; i < m_bodyIds.size(); ++i)
        s += (i ? "," : "") + std::to_string(m_bodyIds[i]);
    char buf[512];
    // gp_GTrsf as a 3x4 affine matrix (row 1..3, col 1..4).
    std::snprintf(buf, sizeof(buf),
        ";g=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
        m_gtrsf.Value(1,1), m_gtrsf.Value(1,2), m_gtrsf.Value(1,3), m_gtrsf.Value(1,4),
        m_gtrsf.Value(2,1), m_gtrsf.Value(2,2), m_gtrsf.Value(2,3), m_gtrsf.Value(2,4),
        m_gtrsf.Value(3,1), m_gtrsf.Value(3,2), m_gtrsf.Value(3,3), m_gtrsf.Value(3,4));
    s += buf;
    // label/desc last — they carry no ';' or '=' (Move/Rotate/Scale strings).
    if (!m_label.empty()) s += ";label=" + m_label;
    if (!m_desc.empty())  s += ";desc="  + m_desc;
    return s;
}

bool BatchTransformOp::deserializeParams(const std::string& blob) {
    bool any = false;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eq = blob.find('=', pos);
        if (eq == std::string::npos) break;
        size_t end = blob.find(';', eq);
        if (end == std::string::npos) end = blob.size();
        std::string k = blob.substr(pos, eq - pos);
        std::string v = blob.substr(eq + 1, end - eq - 1);
        if (k == "bodies") {
            m_bodyIds.clear();
            size_t p = 0;
            while (p < v.size()) {
                size_t c = v.find(',', p);
                if (c == std::string::npos) c = v.size();
                m_bodyIds.push_back(std::atoi(v.substr(p, c - p).c_str()));
                p = c + 1;
            }
            any = true;
        } else if (k == "g") {
            double m[12] = {0}; int n = 0; size_t p = 0;
            while (n < 12 && p < v.size()) {
                size_t c = v.find(',', p);
                if (c == std::string::npos) c = v.size();
                m[n++] = std::atof(v.substr(p, c - p).c_str());
                p = c + 1;
            }
            if (n == 12) {
                m_gtrsf.SetValue(1,1,m[0]); m_gtrsf.SetValue(1,2,m[1]);
                m_gtrsf.SetValue(1,3,m[2]); m_gtrsf.SetValue(1,4,m[3]);
                m_gtrsf.SetValue(2,1,m[4]); m_gtrsf.SetValue(2,2,m[5]);
                m_gtrsf.SetValue(2,3,m[6]); m_gtrsf.SetValue(2,4,m[7]);
                m_gtrsf.SetValue(3,1,m[8]); m_gtrsf.SetValue(3,2,m[9]);
                m_gtrsf.SetValue(3,3,m[10]); m_gtrsf.SetValue(3,4,m[11]);
                any = true;
            }
        } else if (k == "label") { m_label = v; any = true; }
        else if (k == "desc")   { m_desc  = v; any = true; }
        pos = end + 1;
    }
    return any;
}

bool BatchTransformOp::rehydrateFromReload(const ReloadState& state,
                                           Document& /*doc*/) {
    if (m_bodyIds.empty()) return false;
    // Capture the per-body pre-op shapes (the state editStep rolls back to) so
    // undo/captureDiff work in-session; execute re-applies m_gtrsf on replay.
    m_previousShapes.clear();
    for (int id : m_bodyIds)
        for (const auto& [mid, shp] : state.modifiedBefore)
            if (mid == id) { m_previousShapes.push_back({id, shp}); break; }
    return !m_previousShapes.empty();
}
