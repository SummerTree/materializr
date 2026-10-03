#include "MeshGuard.h"

#include "Document.h"
#include "SelectionManager.h"
#include "../i18n.h"

#include <cstdio>

namespace materializr {

std::vector<int> meshBodiesAmong(const Document& doc,
                                 const std::vector<int>& bodies) {
    std::vector<int> meshes;
    for (int id : bodies)
        if (id >= 0 && doc.isBodyMesh(id)) meshes.push_back(id);
    return meshes;
}

std::vector<int> selectedBodyIds(const SelectionManager& sel) {
    std::vector<int> bodies;
    for (const auto& e : sel.getSelection()) {
        if (e.bodyId < 0) continue;
        bool seen = false;
        for (int b : bodies) if (b == e.bodyId) { seen = true; break; }
        if (!seen) bodies.push_back(e.bodyId);
    }
    return bodies;
}

std::string meshRefusalMessage(const char* opName, size_t meshCount,
                               size_t total) {
    const char* op = tr(opName ? opName : "That operation");
    // An import is a reference, so say what it IS good for in the same breath -
    // otherwise the refusal reads as a missing feature rather than a boundary.
    const char* fmt = meshCount >= total
        ? tr("%s needs solid geometry, and this is an imported mesh - "
             "a reference body. Sketch on it and snap to it all you like, "
             "then model the part alongside it.")
        : tr("%s needs solid geometry, and one of the selected bodies is an "
             "imported mesh - a reference body. Leave the import "
             "out of the selection.");
    const int n = std::snprintf(nullptr, 0, fmt, op);
    if (n < 0) return fmt;
    std::string out(static_cast<size_t>(n) + 1, '\0');
    std::snprintf(out.data(), out.size(), fmt, op);
    out.resize(static_cast<size_t>(n));
    return out;
}

} // namespace materializr
