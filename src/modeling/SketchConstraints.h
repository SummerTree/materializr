#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace materializr {

enum class ConstraintType {
    Coincident,    // two points same position
    Horizontal,    // line is horizontal
    Vertical,      // line is vertical
    Distance,      // fixed distance between two points
    Radius,        // fixed radius of circle/arc
    Parallel,      // two lines are parallel
    Perpendicular, // two lines are perpendicular
    Fixed,         // point locked in place
    Tangent,       // arc/circle tangent to line
    Equal,         // two lines have equal length
    Concentric,    // two circles/arcs share same center
    Angle,         // fixed angle (radians) between two lines
    DistancePointLine, // fixed perpendicular distance from a point to a line's infinite carrier
    CircleGap          // fixed rim-to-rim gap between two circles/arcs (centre dist - rA - rB)
};

struct Constraint {
    int id;
    ConstraintType type;
    int entityA = -1;  // point or line id
    int entityB = -1;  // second entity (for Coincident, Parallel, Perpendicular)
    double value = 0.0;  // Distance / Radius / Angle. For Fixed, the X coord.
    double valueY = 0.0; // Y coord of the locked position (Fixed only).
    bool isSatisfied = false;
    // Sketch-space offset of the dimension label from its auto-computed
    // anchor. (0,0) = legacy auto placement (pre-offset files and constraints
    // created without explicit label placement).
    double labelOffX = 0.0;
    double labelOffY = 0.0;

    // Driving (default) = the solver enforces this constraint and it consumes
    // a degree of freedom. Reference/driven = the solver ignores it entirely;
    // it only annotates, re-measuring itself as the geometry moves.
    //
    // Defaults to TRUE so every pre-existing constraint — in memory, in old
    // project files, and every geometric type (Horizontal, Coincident,
    // Tangent, …) for which "reference" is meaningless — keeps driving
    // exactly as before. Only the Dimension tool clears it, so a placed
    // dimension measures without moving anything until the user promotes it
    // in the label's edit popup. See Application::applyPendingDimension.
    bool isDriving = true;
};

// Reference/driven mode is only meaningful for the dimension-bearing types —
// the ones carrying a numeric value the user reads off the drawing. A
// geometric relationship (Horizontal, Parallel, Coincident, …) has no
// measurement to annotate, so it is always driving and the edit popup offers
// no toggle for it.
inline bool constraintSupportsReference(ConstraintType t) {
    return t == ConstraintType::Distance || t == ConstraintType::Radius ||
           t == ConstraintType::Angle || t == ConstraintType::DistancePointLine ||
           t == ConstraintType::CircleGap;
}

} // namespace materializr
