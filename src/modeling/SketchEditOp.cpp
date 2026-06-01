#include "SketchEditOp.h"
#include "SketchSolver.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace materializr {

SketchEditOp::SketchEditOp(std::shared_ptr<Sketch> liveSketch,
                           std::shared_ptr<Sketch> beforeSnapshot,
                           std::shared_ptr<Sketch> afterSnapshot)
    : m_target(std::move(liveSketch)),
      m_before(std::move(beforeSnapshot)),
      m_after(std::move(afterSnapshot)) {}

bool SketchEditOp::execute(Document& /*doc*/) {
    if (!m_target || !m_after) return false;
    *m_target = *m_after;
    return true;
}

bool SketchEditOp::undo(Document& /*doc*/) {
    if (!m_target || !m_before) return false;
    *m_target = *m_before;
    return true;
}

// Helper: human-friendly name for a ConstraintType. Used in descriptions.
static const char* constraintName(ConstraintType t) {
    switch (t) {
        case ConstraintType::Coincident:    return "Coincident";
        case ConstraintType::Horizontal:    return "Horizontal";
        case ConstraintType::Vertical:      return "Vertical";
        case ConstraintType::Distance:      return "Distance";
        case ConstraintType::Radius:        return "Ø";
        case ConstraintType::Parallel:      return "Parallel";
        case ConstraintType::Perpendicular: return "Perpendicular";
        case ConstraintType::Fixed:         return "Fix Position";
        case ConstraintType::Tangent:       return "Tangent";
        case ConstraintType::Equal:         return "Equal";
        case ConstraintType::Concentric:    return "Concentric";
        case ConstraintType::Angle:         return "Angle";
    }
    return "Constraint";
}

std::string SketchEditOp::description() const {
    if (!m_before || !m_after) return "Sketch edit";

    // Constraint diff first — these read more specifically than the generic
    // geometry-count descriptions below.
    const auto& cBefore = m_before->getConstraints();
    const auto& cAfter  = m_after->getConstraints();
    if (cBefore.size() != cAfter.size()) {
        // Added or removed. Look at the difference set (by id).
        char buf[80];
        if (cAfter.size() > cBefore.size()) {
            // Find the first id present in after but not before.
            for (const auto& c : cAfter) {
                bool wasThere = false;
                for (const auto& b : cBefore) if (b.id == c.id) { wasThere = true; break; }
                if (wasThere) continue;
                const char* name = constraintName(c.type);
                if (c.type == ConstraintType::Distance) {
                    std::snprintf(buf, sizeof(buf), "Add Distance %.2f mm", c.value);
                } else if (c.type == ConstraintType::Radius) {
                    std::snprintf(buf, sizeof(buf), "Add \xC3\x98 %.2f mm", c.value * 2.0);
                } else if (c.type == ConstraintType::Angle) {
                    std::snprintf(buf, sizeof(buf), "Add Angle %.1f\xC2\xB0",
                                  c.value * 180.0 / M_PI);
                } else {
                    std::snprintf(buf, sizeof(buf), "Add %s", name);
                }
                return buf;
            }
            return "Add constraint";
        } else {
            // Removed — name the removed type if we can identify it.
            for (const auto& b : cBefore) {
                bool stillThere = false;
                for (const auto& a : cAfter) if (a.id == b.id) { stillThere = true; break; }
                if (stillThere) continue;
                std::snprintf(buf, sizeof(buf), "Remove %s", constraintName(b.type));
                return buf;
            }
            return "Remove constraint";
        }
    } else {
        // Same count — check for a value edit on the same id.
        for (size_t i = 0; i < cAfter.size(); ++i) {
            // Find matching id in before.
            const Constraint* bMatch = nullptr;
            for (const auto& b : cBefore) if (b.id == cAfter[i].id) { bMatch = &b; break; }
            if (!bMatch) continue;
            if (std::abs(bMatch->value - cAfter[i].value) > 1e-9 ||
                std::abs(bMatch->valueY - cAfter[i].valueY) > 1e-9) {
                char buf[100];
                if (cAfter[i].type == ConstraintType::Angle) {
                    std::snprintf(buf, sizeof(buf), "Edit Angle %.1f\xC2\xB0 \xE2\x86\x92 %.1f\xC2\xB0",
                                  bMatch->value * 180.0 / M_PI,
                                  cAfter[i].value * 180.0 / M_PI);
                } else if (cAfter[i].type == ConstraintType::Radius) {
                    std::snprintf(buf, sizeof(buf), "Edit \xC3\x98 %.2f \xE2\x86\x92 %.2f mm",
                                  bMatch->value * 2.0, cAfter[i].value * 2.0);
                } else if (cAfter[i].type == ConstraintType::Distance) {
                    std::snprintf(buf, sizeof(buf), "Edit Distance %.2f \xE2\x86\x92 %.2f mm",
                                  bMatch->value, cAfter[i].value);
                } else {
                    std::snprintf(buf, sizeof(buf), "Edit %s",
                                  constraintName(cAfter[i].type));
                }
                return buf;
            }
        }
    }

    // No constraint diff — fall back to geometry-count diff (existing behaviour).
    int delta = m_after->elementCount() - m_before->elementCount();
    if (delta > 0) return "Add sketch element";
    if (delta < 0) return "Remove sketch element";
    return "Modify sketch";
}

// Writes one Sketch's contents in the project file's SKETCH_START / SKETCH_END
// format. Matches the schema that ProjectIO::parseSketchBody reads, so the
// snapshots inside a SketchEditOp can be rehydrated on load by the same
// parser the top-level sketches use.
static void writeSketchBody(std::ostream& os, const Sketch& sk, int sketchId,
                            const std::string& name, bool visible, int sourceBody) {
    os << "SKETCH_START " << sketchId << " \"" << name << "\" "
       << (visible ? 1 : 0) << " " << sourceBody << "\n";

    // Plane.
    const auto& ax = sk.getPlane().Position();
    auto o = ax.Location();
    auto n = ax.Direction();
    auto x = ax.XDirection();
    auto y = ax.YDirection();
    os << "PLANE "
       << o.X() << " " << o.Y() << " " << o.Z() << " "
       << n.X() << " " << n.Y() << " " << n.Z() << " "
       << x.X() << " " << x.Y() << " " << x.Z() << " "
       << y.X() << " " << y.Y() << " " << y.Z() << "\n";

    // Points.
    const auto& pts = sk.getPoints();
    os << "POINT_COUNT " << pts.size() << "\n";
    for (const auto& p : pts) {
        os << "POINT " << p.id << " " << p.pos.x << " " << p.pos.y
           << " " << (p.isConstruction ? 1 : 0) << "\n";
    }

    // Lines.
    const auto& lines = sk.getLines();
    os << "LINE_COUNT " << lines.size() << "\n";
    for (const auto& l : lines) {
        os << "LINE " << l.id << " " << l.startPointId << " " << l.endPointId
           << " " << (l.isConstruction ? 1 : 0) << "\n";
    }

    // Circles.
    const auto& circs = sk.getCircles();
    os << "CIRCLE_COUNT " << circs.size() << "\n";
    for (const auto& c : circs) {
        os << "CIRCLE " << c.id << " " << c.centerPointId << " " << c.radius
           << " " << (c.isConstruction ? 1 : 0) << "\n";
    }

    // Arcs.
    const auto& arcs = sk.getArcs();
    os << "ARC_COUNT " << arcs.size() << "\n";
    for (const auto& a : arcs) {
        os << "ARC " << a.id << " " << a.centerPointId << " " << a.startPointId
           << " " << a.endPointId << " " << a.radius
           << " " << (a.isConstruction ? 1 : 0) << "\n";
    }

    // Splines.
    const auto& splines = sk.getSplines();
    os << "SPLINE_COUNT " << splines.size() << "\n";
    for (const auto& sp : splines) {
        os << "SPLINE " << sp.id << " " << (sp.isConstruction ? 1 : 0)
           << " " << sp.controlPointIds.size();
        for (int id : sp.controlPointIds) os << " " << id;
        os << "\n";
    }

    // Polygons.
    const auto& polys = sk.getPolygons();
    os << "POLYGON_COUNT " << polys.size() << "\n";
    for (const auto& g : polys) {
        os << "POLYGON " << g.id << " " << g.centerPointId << " " << g.radius
           << " " << g.sides << " " << (g.isConstruction ? 1 : 0)
           << " " << g.vertexPointIds.size();
        for (int id : g.vertexPointIds) os << " " << id;
        os << " " << g.lineIds.size();
        for (int id : g.lineIds) os << " " << id;
        os << "\n";
    }

    // Constraints.
    const auto& cs = sk.getConstraints();
    os << "CONSTRAINT_COUNT " << cs.size() << "\n";
    for (const auto& c : cs) {
        os << "CONSTRAINT " << c.id << " " << static_cast<int>(c.type)
           << " " << c.entityA << " " << c.entityB
           << " " << c.value << " " << c.valueY << "\n";
    }

    os << "SKETCH_END\n";
}

std::string SketchEditOp::serializeWithDocument(const Document& doc) const {
    if (!m_target || !m_before || !m_after) return "";

    // The sketch this op edits — used as the rebind anchor at load time.
    int sketchId = doc.findSketchId(m_target.get());
    if (sketchId < 0) return ""; // not in the document; can't bind on load

    std::string name = doc.getSketchName(sketchId);
    bool visible = doc.isSketchVisible(sketchId);
    // SourceBody travels with the snapshots themselves (each Sketch carries it
    // via getSourceBody()), but we also stash it on the SKETCH_START line so
    // the loader doesn't need a second hop through the live sketch.
    int sourceBody = m_target->getSourceBody();

    std::ostringstream os;
    writeSketchBody(os, *m_before, sketchId, name, visible, sourceBody);
    writeSketchBody(os, *m_after,  sketchId, name, visible, sourceBody);
    return os.str();
}

void SketchEditOp::renderProperties() {
    if (!m_after) {
        ImGui::TextDisabled("No snapshot");
        return;
    }
    auto& cs = m_after->getMutableConstraints();
    if (cs.empty()) {
        ImGui::TextDisabled("No constraints in this step");
        return;
    }

    // Edit dimensional values inline. For each change we re-solve `m_after`
    // so dependent geometry catches up — Apply Changes then copies the
    // solved snapshot onto the live sketch via editStep / execute().
    auto resolveAfter = [&]() {
        SketchSolver solver;
        solver.solve(*m_after);
    };

    bool anyDim = false;
    for (size_t i = 0; i < cs.size(); ++i) {
        Constraint& c = cs[i];
        ImGui::PushID(static_cast<int>(i));
        switch (c.type) {
            case ConstraintType::Distance: {
                anyDim = true;
                double v = c.value;
                if (ImGui::InputDouble("Distance (mm)", &v, 0.0, 0.0, "%.3f",
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
                    c.value = v;
                    resolveAfter();
                }
                break;
            }
            case ConstraintType::Radius: {
                anyDim = true;
                // Stored as radius; show as diameter to match the in-sketch
                // popup ("Ø ..." in descriptions and dimensions).
                double dia = c.value * 2.0;
                if (ImGui::InputDouble("\xC3\x98 (mm)", &dia, 0.0, 0.0, "%.3f",
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
                    c.value = std::max(dia, 1e-6) * 0.5;
                    resolveAfter();
                }
                break;
            }
            case ConstraintType::Angle: {
                anyDim = true;
                double deg = c.value * 180.0 / M_PI;
                if (ImGui::InputDouble("Angle (\xC2\xB0)", &deg, 0.0, 0.0, "%.2f",
                                       ImGuiInputTextFlags_EnterReturnsTrue)) {
                    c.value = deg * M_PI / 180.0;
                    resolveAfter();
                }
                break;
            }
            default: {
                // Non-dimensional constraints have nothing to tune. Show the
                // name as a read-only row so the user can confirm what's in
                // the step, then move on.
                const char* name = constraintName(c.type);
                ImGui::TextDisabled("• %s", name);
                break;
            }
        }
        ImGui::PopID();
    }

    if (!anyDim) {
        ImGui::TextWrapped("This step contains only non-dimensional "
                           "constraints — there are no values to edit.");
    } else {
        ImGui::TextDisabled("Press Enter to commit a value, then Apply Changes.");
    }
}

} // namespace materializr
