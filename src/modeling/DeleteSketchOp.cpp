#include "DeleteSketchOp.h"
#include <cstdio>

bool DeleteSketchOp::execute(Document& doc) {
    if (m_sketchId < 0) return false;
    auto sk = doc.getSketch(m_sketchId);
    if (!sk) return false;
    m_sketch = sk;
    m_name = doc.getSketchName(m_sketchId);
    m_visible = doc.isSketchVisible(m_sketchId);
    doc.removeSketch(m_sketchId);
    return true;
}

bool DeleteSketchOp::undo(Document& doc) {
    if (m_sketchId < 0 || !m_sketch) return false;
    doc.putSketch(m_sketchId, m_sketch, m_name);
    doc.setSketchVisible(m_sketchId, m_visible);
    return true;
}

std::string DeleteSketchOp::description() const {
    if (!m_name.empty()) return "Delete \xE2\x86\x92 " + m_name;
    return "Delete sketch";
}

std::string DeleteSketchOp::serializeParams() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "id=%d", m_sketchId);
    return buf;
}

bool DeleteSketchOp::deserializeParams(const std::string& blob) {
    int id = -1;
    std::sscanf(blob.c_str(), "id=%d", &id);
    m_sketchId = id;
    return true;
}

bool DeleteSketchOp::rehydrateFromReload(const ReloadState& /*state*/, Document& /*doc*/) {
    // The sketch is gone from the saved project; nothing to restore on undo.
    return false;
}
