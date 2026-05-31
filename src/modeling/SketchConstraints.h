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
    Angle          // fixed angle (radians) between two lines
};

struct Constraint {
    int id;
    ConstraintType type;
    int entityA = -1;  // point or line id
    int entityB = -1;  // second entity (for Coincident, Parallel, Perpendicular)
    double value = 0.0;  // Distance / Radius / Angle. For Fixed, the X coord.
    double valueY = 0.0; // Y coord of the locked position (Fixed only).
    bool isSatisfied = false;
};

} // namespace materializr
