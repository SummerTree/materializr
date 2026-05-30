#include "SketchSolver.h"
#include <cmath>
#include <algorithm>

namespace materializr {

SketchSolver::SketchSolver() = default;

// Thin wrappers around the active sketch's constraint storage. The Sketch owns
// the data; the solver only caches state/DOF from the last solve().
int SketchSolver::addConstraint(const Constraint& c) {
    if (!m_sketch) return -1;
    return m_sketch->addConstraint(c);
}

void SketchSolver::removeConstraint(int id) {
    if (!m_sketch) return;
    m_sketch->removeConstraint(id);
}

static const std::vector<Constraint> s_emptyConstraints;
const std::vector<Constraint>& SketchSolver::getConstraints() const {
    if (!m_sketch) return s_emptyConstraints;
    return m_sketch->getConstraints();
}

bool SketchSolver::solve(Sketch& sketch, int maxIterations, double tolerance) {
    m_sketch = &sketch;
    auto& constraints = sketch.getMutableConstraints();
    // Compute degrees of freedom
    int numPoints = sketch.pointCount();
    int numEquations = 0;
    for (const auto& c : constraints) {
        switch (c.type) {
            case ConstraintType::Coincident:
                numEquations += 2; // x and y must match
                break;
            case ConstraintType::Horizontal:
            case ConstraintType::Vertical:
                numEquations += 1;
                break;
            case ConstraintType::Distance:
                numEquations += 1;
                break;
            case ConstraintType::Radius:
                numEquations += 1;
                break;
            case ConstraintType::Parallel:
                numEquations += 1;
                break;
            case ConstraintType::Perpendicular:
                numEquations += 1;
                break;
            case ConstraintType::Fixed:
                numEquations += 2; // x and y fixed
                break;
            case ConstraintType::Tangent:
                numEquations += 1;
                break;
            case ConstraintType::Equal:
                numEquations += 1;
                break;
            case ConstraintType::Concentric:
                numEquations += 2; // x and y must match (same as coincident)
                break;
        }
    }

    m_dof = 2 * numPoints - numEquations;

    if (m_dof < 0) {
        m_state = SketchState::OverConstrained;
    } else if (m_dof == 0) {
        m_state = SketchState::FullyConstrained;
    } else {
        m_state = SketchState::UnderConstrained;
    }

    // Iterative relaxation
    for (int iter = 0; iter < maxIterations; ++iter) {
        double maxError = 0.0;

        for (auto& constraint : constraints) {
            double error = computeError(constraint, sketch);
            maxError = std::max(maxError, std::abs(error));

            if (std::abs(error) > tolerance) {
                applyCorrection(constraint, sketch, error);
                constraint.isSatisfied = false;
            } else {
                constraint.isSatisfied = true;
            }
        }

        if (maxError <= tolerance) {
            // Mark all as satisfied
            for (auto& c : constraints) {
                c.isSatisfied = true;
            }
            return true;
        }
    }

    return false;
}

SketchState SketchSolver::getState() const {
    return m_state;
}

int SketchSolver::degreesOfFreedom() const {
    return m_dof;
}

void SketchSolver::clear() {
    if (m_sketch) {
        m_sketch->getMutableConstraints().clear();
        m_sketch->setNextConstraintId(1);
    }
    m_state = SketchState::UnderConstrained;
    m_dof = 0;
}

double SketchSolver::computeError(const Constraint& c, const Sketch& sketch) const {
    switch (c.type) {
        case ConstraintType::Coincident: {
            const SketchPoint* pa = sketch.getPoint(c.entityA);
            const SketchPoint* pb = sketch.getPoint(c.entityB);
            if (!pa || !pb) return 0.0;
            glm::vec2 diff = pa->pos - pb->pos;
            return glm::length(diff);
        }

        case ConstraintType::Horizontal: {
            // entityA is a line id - find the line
            const auto& lines = sketch.getLines();
            for (const auto& line : lines) {
                if (line.id == c.entityA) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (!sp || !ep) return 0.0;
                    return ep->pos.y - sp->pos.y;
                }
            }
            return 0.0;
        }

        case ConstraintType::Vertical: {
            const auto& lines = sketch.getLines();
            for (const auto& line : lines) {
                if (line.id == c.entityA) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (!sp || !ep) return 0.0;
                    return ep->pos.x - sp->pos.x;
                }
            }
            return 0.0;
        }

        case ConstraintType::Distance: {
            const SketchPoint* pa = sketch.getPoint(c.entityA);
            const SketchPoint* pb = sketch.getPoint(c.entityB);
            if (!pa || !pb) return 0.0;
            double dist = glm::length(pa->pos - pb->pos);
            return dist - c.value;
        }

        case ConstraintType::Radius: {
            // entityA is circle id
            const auto& circles = sketch.getCircles();
            for (const auto& circle : circles) {
                if (circle.id == c.entityA) {
                    return circle.radius - c.value;
                }
            }
            return 0.0;
        }

        case ConstraintType::Fixed: {
            const SketchPoint* pa = sketch.getPoint(c.entityA);
            if (!pa) return 0.0;
            // Fixed = pin this point at the (value, valueY) it had when the
            // constraint was added.
            glm::vec2 target(static_cast<float>(c.value),
                             static_cast<float>(c.valueY));
            return glm::length(pa->pos - target);
        }

        case ConstraintType::Parallel: {
            // entityA and entityB are line ids
            const auto& lines = sketch.getLines();
            glm::vec2 dirA(0), dirB(0);
            for (const auto& line : lines) {
                if (line.id == c.entityA) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (sp && ep) dirA = ep->pos - sp->pos;
                }
                if (line.id == c.entityB) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (sp && ep) dirB = ep->pos - sp->pos;
                }
            }
            float lenA = glm::length(dirA);
            float lenB = glm::length(dirB);
            if (lenA < 1e-10f || lenB < 1e-10f) return 0.0;
            // Cross product (should be 0 for parallel)
            dirA /= lenA;
            dirB /= lenB;
            return static_cast<double>(dirA.x * dirB.y - dirA.y * dirB.x);
        }

        case ConstraintType::Perpendicular: {
            const auto& lines = sketch.getLines();
            glm::vec2 dirA(0), dirB(0);
            for (const auto& line : lines) {
                if (line.id == c.entityA) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (sp && ep) dirA = ep->pos - sp->pos;
                }
                if (line.id == c.entityB) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (sp && ep) dirB = ep->pos - sp->pos;
                }
            }
            float lenA = glm::length(dirA);
            float lenB = glm::length(dirB);
            if (lenA < 1e-10f || lenB < 1e-10f) return 0.0;
            // Dot product (should be 0 for perpendicular)
            dirA /= lenA;
            dirB /= lenB;
            return static_cast<double>(dirA.x * dirB.x + dirA.y * dirB.y);
        }

        case ConstraintType::Tangent: {
            // Tangent between arc (entityA) and line (entityB)
            // The arc's endpoint should lie on the line, and the line direction
            // should be perpendicular to the radius at that point
            const auto& arcs = sketch.getArcs();
            const auto& lines = sketch.getLines();

            const SketchArc* arc = nullptr;
            const SketchLine* line = nullptr;

            for (const auto& a : arcs) {
                if (a.id == c.entityA) { arc = &a; break; }
            }
            for (const auto& l : lines) {
                if (l.id == c.entityB) { line = &l; break; }
            }

            if (!arc || !line) return 0.0;

            const SketchPoint* arcCenter = sketch.getPoint(arc->centerPointId);
            const SketchPoint* lineStart = sketch.getPoint(line->startPointId);
            const SketchPoint* lineEnd = sketch.getPoint(line->endPointId);
            if (!arcCenter || !lineStart || !lineEnd) return 0.0;

            // Distance from arc center to line should equal arc radius
            glm::vec2 lineDir = lineEnd->pos - lineStart->pos;
            float lineLen = glm::length(lineDir);
            if (lineLen < 1e-10f) return 0.0;
            lineDir /= lineLen;

            // Signed distance from center to line (using perpendicular)
            glm::vec2 toCenter = arcCenter->pos - lineStart->pos;
            float dist = std::abs(toCenter.x * (-lineDir.y) + toCenter.y * lineDir.x);

            return static_cast<double>(dist - static_cast<float>(arc->radius));
        }

        case ConstraintType::Equal: {
            // entityA and entityB are line ids; their lengths should be equal
            const auto& lines = sketch.getLines();
            float lenA = 0.0f, lenB = 0.0f;

            for (const auto& line : lines) {
                if (line.id == c.entityA) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (sp && ep) lenA = glm::length(ep->pos - sp->pos);
                }
                if (line.id == c.entityB) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (sp && ep) lenB = glm::length(ep->pos - sp->pos);
                }
            }

            return static_cast<double>(lenA - lenB);
        }

        case ConstraintType::Concentric: {
            // entityA and entityB are circle or arc ids; their centers should coincide
            // Look up center points for both entities
            int centerA = -1, centerB = -1;

            const auto& circles = sketch.getCircles();
            const auto& arcs = sketch.getArcs();

            for (const auto& circ : circles) {
                if (circ.id == c.entityA) centerA = circ.centerPointId;
                if (circ.id == c.entityB) centerB = circ.centerPointId;
            }
            for (const auto& arc : arcs) {
                if (arc.id == c.entityA) centerA = arc.centerPointId;
                if (arc.id == c.entityB) centerB = arc.centerPointId;
            }

            if (centerA < 0 || centerB < 0) return 0.0;

            const SketchPoint* pa = sketch.getPoint(centerA);
            const SketchPoint* pb = sketch.getPoint(centerB);
            if (!pa || !pb) return 0.0;

            return static_cast<double>(glm::length(pa->pos - pb->pos));
        }
    }

    return 0.0;
}

void SketchSolver::applyCorrection(const Constraint& c, Sketch& sketch, double error) const {
    switch (c.type) {
        case ConstraintType::Coincident: {
            const SketchPoint* pa = sketch.getPoint(c.entityA);
            const SketchPoint* pb = sketch.getPoint(c.entityB);
            if (!pa || !pb) return;
            glm::vec2 mid = (pa->pos + pb->pos) * 0.5f;
            sketch.movePoint(c.entityA, mid);
            sketch.movePoint(c.entityB, mid);
            break;
        }

        case ConstraintType::Horizontal: {
            const auto& lines = sketch.getLines();
            for (const auto& line : lines) {
                if (line.id == c.entityA) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (!sp || !ep) return;
                    float avgY = (sp->pos.y + ep->pos.y) * 0.5f;
                    sketch.movePoint(line.startPointId, glm::vec2(sp->pos.x, avgY));
                    sketch.movePoint(line.endPointId, glm::vec2(ep->pos.x, avgY));
                    break;
                }
            }
            break;
        }

        case ConstraintType::Vertical: {
            const auto& lines = sketch.getLines();
            for (const auto& line : lines) {
                if (line.id == c.entityA) {
                    const SketchPoint* sp = sketch.getPoint(line.startPointId);
                    const SketchPoint* ep = sketch.getPoint(line.endPointId);
                    if (!sp || !ep) return;
                    float avgX = (sp->pos.x + ep->pos.x) * 0.5f;
                    sketch.movePoint(line.startPointId, glm::vec2(avgX, sp->pos.y));
                    sketch.movePoint(line.endPointId, glm::vec2(avgX, ep->pos.y));
                    break;
                }
            }
            break;
        }

        case ConstraintType::Distance: {
            const SketchPoint* pa = sketch.getPoint(c.entityA);
            const SketchPoint* pb = sketch.getPoint(c.entityB);
            if (!pa || !pb) return;
            glm::vec2 diff = pb->pos - pa->pos;
            float currentDist = glm::length(diff);
            if (currentDist < 1e-10f) {
                // Points are coincident, push apart along x
                diff = glm::vec2(1.0f, 0.0f);
                currentDist = 1.0f;
            }
            glm::vec2 dir = diff / currentDist;
            float targetDist = static_cast<float>(c.value);
            float correction = (targetDist - currentDist) * 0.5f;
            sketch.movePoint(c.entityA, pa->pos - dir * correction);
            sketch.movePoint(c.entityB, pb->pos + dir * correction);
            break;
        }

        case ConstraintType::Radius: {
            // Radius constraints modify the circle's radius directly
            // Since we store radius in the circle struct, we need direct access
            // For MVP, radius constraints are informational / checked only
            break;
        }

        case ConstraintType::Fixed: {
            glm::vec2 target(static_cast<float>(c.value),
                             static_cast<float>(c.valueY));
            sketch.movePoint(c.entityA, target);
            break;
        }

        case ConstraintType::Parallel: {
            const auto& lines = sketch.getLines();
            const SketchLine* lineA = nullptr;
            const SketchLine* lineB = nullptr;
            for (const auto& line : lines) {
                if (line.id == c.entityA) lineA = &line;
                if (line.id == c.entityB) lineB = &line;
            }
            if (!lineA || !lineB) return;

            const SketchPoint* spB = sketch.getPoint(lineB->startPointId);
            const SketchPoint* epB = sketch.getPoint(lineB->endPointId);
            const SketchPoint* spA = sketch.getPoint(lineA->startPointId);
            const SketchPoint* epA = sketch.getPoint(lineA->endPointId);
            if (!spA || !epA || !spB || !epB) return;

            // Rotate line B's endpoint to be parallel to line A
            glm::vec2 dirA = epA->pos - spA->pos;
            float lenA = glm::length(dirA);
            if (lenA < 1e-10f) return;
            dirA /= lenA;

            float lenB = glm::length(epB->pos - spB->pos);
            glm::vec2 newEndB = spB->pos + dirA * lenB;
            sketch.movePoint(lineB->endPointId, newEndB);
            break;
        }

        case ConstraintType::Perpendicular: {
            const auto& lines = sketch.getLines();
            const SketchLine* lineA = nullptr;
            const SketchLine* lineB = nullptr;
            for (const auto& line : lines) {
                if (line.id == c.entityA) lineA = &line;
                if (line.id == c.entityB) lineB = &line;
            }
            if (!lineA || !lineB) return;

            const SketchPoint* spB = sketch.getPoint(lineB->startPointId);
            const SketchPoint* epB = sketch.getPoint(lineB->endPointId);
            const SketchPoint* spA = sketch.getPoint(lineA->startPointId);
            const SketchPoint* epA = sketch.getPoint(lineA->endPointId);
            if (!spA || !epA || !spB || !epB) return;

            // Rotate line B's endpoint to be perpendicular to line A
            glm::vec2 dirA = epA->pos - spA->pos;
            float lenA = glm::length(dirA);
            if (lenA < 1e-10f) return;
            dirA /= lenA;

            // Perpendicular direction: rotate 90 degrees
            glm::vec2 perpDir(-dirA.y, dirA.x);
            float lenB = glm::length(epB->pos - spB->pos);
            glm::vec2 newEndB = spB->pos + perpDir * lenB;
            sketch.movePoint(lineB->endPointId, newEndB);
            break;
        }

        case ConstraintType::Tangent: {
            // Move the arc center so its distance to the line equals the radius
            const auto& arcs = sketch.getArcs();
            const auto& lines = sketch.getLines();

            const SketchArc* arc = nullptr;
            const SketchLine* line = nullptr;

            for (const auto& a : arcs) {
                if (a.id == c.entityA) { arc = &a; break; }
            }
            for (const auto& l : lines) {
                if (l.id == c.entityB) { line = &l; break; }
            }

            if (!arc || !line) return;

            const SketchPoint* arcCenter = sketch.getPoint(arc->centerPointId);
            const SketchPoint* lineStart = sketch.getPoint(line->startPointId);
            const SketchPoint* lineEnd = sketch.getPoint(line->endPointId);
            if (!arcCenter || !lineStart || !lineEnd) return;

            glm::vec2 lineDir = lineEnd->pos - lineStart->pos;
            float lineLen = glm::length(lineDir);
            if (lineLen < 1e-10f) return;
            lineDir /= lineLen;

            // Normal to the line (pointing toward center)
            glm::vec2 normal(-lineDir.y, lineDir.x);
            glm::vec2 toCenter = arcCenter->pos - lineStart->pos;
            float signedDist = toCenter.x * normal.x + toCenter.y * normal.y;

            // We want |signedDist| == radius. Preserve the sign.
            float targetDist = static_cast<float>(arc->radius);
            if (signedDist < 0.0f) targetDist = -targetDist;

            float correction = targetDist - signedDist;
            glm::vec2 newCenter = arcCenter->pos + normal * correction;
            sketch.movePoint(arc->centerPointId, newCenter);
            break;
        }

        case ConstraintType::Equal: {
            // Make both lines the same length by adjusting their endpoints
            const auto& lines = sketch.getLines();
            const SketchLine* lineA = nullptr;
            const SketchLine* lineB = nullptr;

            for (const auto& line : lines) {
                if (line.id == c.entityA) lineA = &line;
                if (line.id == c.entityB) lineB = &line;
            }
            if (!lineA || !lineB) return;

            const SketchPoint* spA = sketch.getPoint(lineA->startPointId);
            const SketchPoint* epA = sketch.getPoint(lineA->endPointId);
            const SketchPoint* spB = sketch.getPoint(lineB->startPointId);
            const SketchPoint* epB = sketch.getPoint(lineB->endPointId);
            if (!spA || !epA || !spB || !epB) return;

            float lenA = glm::length(epA->pos - spA->pos);
            float lenB = glm::length(epB->pos - spB->pos);

            if (lenA < 1e-10f && lenB < 1e-10f) return;

            float avgLen = (lenA + lenB) * 0.5f;

            // Scale line B's endpoint to match average length
            if (lenB > 1e-10f) {
                glm::vec2 dirB = (epB->pos - spB->pos) / lenB;
                glm::vec2 newEndB = spB->pos + dirB * avgLen;
                sketch.movePoint(lineB->endPointId, newEndB);
            }

            // Scale line A's endpoint to match average length
            if (lenA > 1e-10f) {
                glm::vec2 dirA = (epA->pos - spA->pos) / lenA;
                glm::vec2 newEndA = spA->pos + dirA * avgLen;
                sketch.movePoint(lineA->endPointId, newEndA);
            }
            break;
        }

        case ConstraintType::Concentric: {
            // Same as Coincident but for circle/arc centers
            int centerA = -1, centerB = -1;

            const auto& circles = sketch.getCircles();
            const auto& arcs = sketch.getArcs();

            for (const auto& circ : circles) {
                if (circ.id == c.entityA) centerA = circ.centerPointId;
                if (circ.id == c.entityB) centerB = circ.centerPointId;
            }
            for (const auto& arc : arcs) {
                if (arc.id == c.entityA) centerA = arc.centerPointId;
                if (arc.id == c.entityB) centerB = arc.centerPointId;
            }

            if (centerA < 0 || centerB < 0) return;

            const SketchPoint* pa = sketch.getPoint(centerA);
            const SketchPoint* pb = sketch.getPoint(centerB);
            if (!pa || !pb) return;

            glm::vec2 mid = (pa->pos + pb->pos) * 0.5f;
            sketch.movePoint(centerA, mid);
            sketch.movePoint(centerB, mid);
            break;
        }
    }
}

} // namespace materializr
