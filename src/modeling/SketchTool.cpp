#include "SketchTool.h"
#include "SketchConstraints.h"
#include <algorithm>
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace materializr {

SketchTool::SketchTool() = default;

void SketchTool::setSketch(Sketch* sketch) {
    m_sketch = sketch;
}

void SketchTool::setSolver(SketchSolver* solver) {
    m_solver = solver;
}

void SketchTool::setMode(SketchToolMode mode) {
    // Cancel any in-progress operation when switching modes
    if (m_isPlacing) {
        onCancel();
    }
    m_mode = mode;
    m_clickCount = 0;
    m_isPlacing = false;
    m_lastPointId = -1;
    m_chainStartPointId = -1;
    m_isDragging = false;
    m_dragPointId = -1;
    m_splinePoints.clear();
    m_hasPrevLineDir = false;
    m_activeInferences.clear();
    m_rectDimStage = 0;
    m_rectDimH = 0.0f;
}

SketchToolMode SketchTool::getMode() const {
    return m_mode;
}

void SketchTool::onMouseDown(glm::vec2 pos, bool addToSel) {
    if (!m_sketch) return;
    m_lastDownAddedToSel = addToSel;

    // Trim picks by proximity to existing geometry; snapping the cursor first
    // would pull the click toward unrelated nearby points/edges and pick wrong.
    glm::vec2 snapped = (m_mode == SketchToolMode::Trim) ? pos : snap(pos);

    // Rectangle stage-1 (width typed-in, cursor still drives height): the
    // click that commits the second corner should use the LOCKED width on the
    // X axis, not whatever the cursor happens to be at horizontally.
    if (m_mode == SketchToolMode::Rectangle && m_rectDimStage == 1 && m_isPlacing) {
        snapped.x = m_firstClick.x + m_rectDimH;
    }

    if (m_mode == SketchToolMode::None) return;

    switch (m_mode) {
        case SketchToolMode::Select:
            handleSelectTool(snapped);
            break;
        case SketchToolMode::Line:
            handleLineTool(snapped);
            break;
        case SketchToolMode::Circle:
            handleCircleTool(snapped);
            break;
        case SketchToolMode::Rectangle:
            handleRectangleTool(snapped);
            break;
        case SketchToolMode::Arc:
            handleArcTool(snapped);
            break;
        case SketchToolMode::Spline:
            handleSplineTool(snapped);
            break;
        case SketchToolMode::Polygon:
            handlePolygonTool(snapped);
            break;
        case SketchToolMode::Trim:
            handleTrimTool(snapped);
            break;
        default:
            break;
    }
}

void SketchTool::onMouseMove(glm::vec2 pos) {
    // Trim uses the raw cursor for picking; snapping would pull the click toward
    // unrelated nearby targets and pick the wrong element.
    glm::vec2 newPos = (m_mode == SketchToolMode::Trim) ? pos : snap(pos);
    glm::vec2 delta  = newPos - m_currentPos;
    m_currentPos = newPos;

    // Rectangle two-stage typed input: after the user has typed width + Enter
    // (stage 1), the X side is locked. Override the cursor-driven X so the
    // live preview keeps the typed width visible while the user adjusts /
    // types / clicks for the height.
    if (m_mode == SketchToolMode::Rectangle && m_rectDimStage == 1) {
        m_currentPos.x = m_firstClick.x + m_rectDimH;
    }

    if (m_mode == SketchToolMode::Trim) {
        computeTrimHover(pos);
    } else if (!m_trimHoverPoints.empty()) {
        m_trimHoverPoints.clear();
    }

    // Drag the active selection (or the single dragged point if no broader
    // selection exists) by the cursor delta each frame.
    if (m_isDragging && m_sketch) {
        std::set<int> pts = m_selectedPoints;
        for (int lid : m_selectedLines) {
            for (const auto& l : m_sketch->getLines()) {
                if (l.id == lid) {
                    pts.insert(l.startPointId);
                    pts.insert(l.endPointId);
                    break;
                }
            }
        }
        if (pts.empty() && m_dragPointId >= 0) pts.insert(m_dragPointId);

        // When a single point is being dragged, run inference snap on its
        // target position so endpoint / midpoint / on-line / axis-from-point
        // guides fire mid-drag (the same as during placement). The dragged
        // point itself is excluded so it doesn't snap to its starting spot.
        // For multi-point drags we skip inference snap — there's no single
        // "cursor follows this point" semantic, and snapping one would slide
        // the others off.
        if (pts.size() == 1 && m_dragPointId >= 0) {
            const SketchPoint* p = m_sketch->getPoint(m_dragPointId);
            if (p) {
                m_snapExcludePoints = pts;
                glm::vec2 target = p->pos + delta;
                glm::vec2 inferred = snap(target);
                m_snapExcludePoints.clear();
                m_sketch->movePoint(m_dragPointId, inferred);
            }
        } else {
            for (int pid : pts) {
                const SketchPoint* p = m_sketch->getPoint(pid);
                if (p) m_sketch->movePoint(pid, p->pos + delta);
            }
            // Multi-point drag doesn't fire inferences; clear any stale ones
            // so the overlay doesn't draw guides from the previous frame.
            m_activeInferences.clear();
        }
        if (m_solver) m_solver->solve(*m_sketch);
    }
}

void SketchTool::onMouseUp(glm::vec2 /*pos*/) {
    if (m_isDragging) {
        m_isDragging = false;
        m_dragPointId = -1;
        // No more drag — clear any drag-time inference guides so the overlay
        // doesn't linger after the user releases the mouse.
        m_activeInferences.clear();
    }
}

void SketchTool::onConfirm() {
    // Finalize spline if in spline mode with enough points
    if (m_mode == SketchToolMode::Spline && m_splinePoints.size() >= 2 && m_sketch) {
        m_sketch->addSpline(m_splinePoints);
        m_splinePoints.clear();
    }

    // Finish the current chain (e.g., stop line chaining)
    m_isPlacing = false;
    m_clickCount = 0;
    m_lastPointId = -1;
    m_chainStartPointId = -1;
    m_chainStartPointCreated = false;
    m_hasPrevLineDir = false;
    m_activeInferences.clear();
    m_rectDimStage = 0;
    m_rectDimH = 0.0f;
}

void SketchTool::onCancel() {
    // If only the first click of a line chain was made and we created its
    // anchor point fresh (no existing point was reused), drop that orphan so
    // a cancelled draw doesn't leave a stray yellow endpoint marker behind.
    if (m_sketch && m_mode == SketchToolMode::Line &&
        m_clickCount == 1 && m_chainStartPointCreated &&
        m_chainStartPointId >= 0) {
        m_sketch->removeElement(m_chainStartPointId);
    }
    m_isPlacing = false;
    m_clickCount = 0;
    m_lastPointId = -1;
    m_chainStartPointId = -1;
    m_chainStartPointCreated = false;
    m_splinePoints.clear();
    m_hasPrevLineDir = false;
    m_activeInferences.clear();
    m_rectDimStage = 0;
    m_rectDimH = 0.0f;
}

bool SketchTool::applyDimension(float value) {
    if (!m_sketch || !m_isPlacing || value <= 0.0f) return false;

    // Direction from anchor toward current cursor position. If the cursor is on top
    // of the anchor, default to +X so something happens.
    glm::vec2 delta = m_currentPos - m_firstClick;
    float curLen = glm::length(delta);
    glm::vec2 dir = (curLen > 1e-6f) ? (delta / curLen) : glm::vec2(1.0f, 0.0f);

    switch (m_mode) {
        case SketchToolMode::Line: {
            // Type-to-place a single segment, then end the chain. Without the
            // onConfirm() the chain stayed live and the next click would
            // either extend the chain or — worse — get pulled onto the
            // just-drawn line by the on-line inference and look like a point
            // was added partway along the segment. The user wanted typing to
            // commit the line and leave a fresh anchor for the next click.
            glm::vec2 endPos = m_firstClick + dir * value;
            handleLineTool(endPos);
            onConfirm();
            return true;
        }
        case SketchToolMode::Circle: {
            // Popup asks for DIAMETER (matching the on-canvas "X.X mm dia"
            // readout). Convert to radius for the underlying circle.
            float radius = value * 0.5f;
            glm::vec2 perimeter = m_firstClick + dir * radius;
            handleCircleTool(perimeter);
            return true;
        }
        case SketchToolMode::Polygon: {
            // For polygon the typed value is the SIDE COUNT (≥3), not the
            // radius. We DON'T commit here — just update m_polygonSides so
            // the live preview re-renders with the new count, and the user
            // can keep dragging to size + rotation. The actual placement
            // commits on the second click as for any other polygon.
            m_polygonSides = std::max(3, static_cast<int>(std::round(value)));
            return true;
        }
        case SketchToolMode::Rectangle: {
            // Two-stage typed input: first Enter sets the horizontal side
            // (cursor X locks, user can still move cursor for Y), second Enter
            // sets the vertical side AND commits. Quadrant comes from where
            // the cursor is relative to the first click so the rectangle
            // grows in the user's intended direction.
            float sx = (delta.x >= 0.0f) ? 1.0f : -1.0f;
            float sy = (delta.y >= 0.0f) ? 1.0f : -1.0f;
            if (m_rectDimStage == 0) {
                m_rectDimH = value * sx;
                m_rectDimStage = 1;
                // Lock the preview's X to the typed horizontal while the user
                // either drags for vertical or types the second value.
                m_currentPos.x = m_firstClick.x + m_rectDimH;
                return true;
            } else {
                glm::vec2 corner = m_firstClick + glm::vec2(m_rectDimH, value * sy);
                handleRectangleTool(corner);
                m_rectDimStage = 0;
                m_rectDimH = 0.0f;
                return true;
            }
        }
        case SketchToolMode::Arc: {
            // Arc needs three clicks; a single value can't fully specify it. No-op.
            return false;
        }
        default:
            return false;
    }
}

bool SketchTool::hasPreview() const {
    return m_isPlacing && m_mode != SketchToolMode::None;
}

glm::vec2 SketchTool::getPreviewStart() const {
    return m_firstClick;
}

glm::vec2 SketchTool::getPreviewEnd() const {
    return m_currentPos;
}

SketchToolMode SketchTool::getPreviewType() const {
    return m_mode;
}

bool SketchTool::isActive() const {
    return m_isPlacing;
}

glm::vec2 SketchTool::snap(glm::vec2 pos) const {
    // Fresh inference set every snap — the renderer treats this as "what's
    // active right now".
    m_activeInferences.clear();

    // Point snap radius scales with grid step so it remains useful at 10 mm grids
    // and isn't overaggressive at 0.1 mm grids.
    float pointSnapThreshold = std::max(0.15f, m_gridStep * 0.4f);
    float curveSnapThreshold = pointSnapThreshold; // same band for circle/arc perimeters

    // First, try snapping to existing sketch points (always-on).
    if (m_sketch) {
        const auto& points = m_sketch->getPoints();
        for (const auto& pt : points) {
            // During a drag, skip the points being moved — they'd snap to
            // their own starting position and lock the drag in place.
            if (m_snapExcludePoints.count(pt.id)) continue;
            if (glm::length(pos - pt.pos) < pointSnapThreshold) {
                m_activeInferences.push_back({InferenceGuide::Endpoint, pt.pos, pt.pos, pt.id});
                return pt.pos;
            }
        }
        // Face-reference points (vertices and curve samples from the host
        // face). Same endpoint inference, just sourced from the 3D geometry
        // the sketch was started on. refId of -1 since these aren't sketch
        // points and don't have ids the rest of the system cares about.
        for (const auto& fp : m_sketch->getFaceReferences().points) {
            if (glm::length(pos - fp) < pointSnapThreshold) {
                m_activeInferences.push_back({InferenceGuide::Endpoint, fp, fp, -1});
                return fp;
            }
        }

        // Then, snap to the nearest point on any circle's perimeter (and arc perimeter).
        // This lets the line tool anchor on a curved edge so a sketch made off a circle
        // ends up topologically closed instead of leaving a sliver gap.
        const auto& circles = m_sketch->getCircles();
        for (const auto& c : circles) {
            const SketchPoint* center = m_sketch->getPoint(c.centerPointId);
            if (!center) continue;
            glm::vec2 v = pos - center->pos;
            float dist = glm::length(v);
            if (dist < 1e-6f) continue;
            float r = static_cast<float>(c.radius);
            float perimeterDist = std::abs(dist - r);
            if (perimeterDist < curveSnapThreshold) {
                return center->pos + (v / dist) * r;
            }
        }

        const auto& arcs = m_sketch->getArcs();
        for (const auto& a : arcs) {
            const SketchPoint* center = m_sketch->getPoint(a.centerPointId);
            if (!center) continue;
            glm::vec2 v = pos - center->pos;
            float dist = glm::length(v);
            if (dist < 1e-6f) continue;
            float r = static_cast<float>(a.radius);
            float perimeterDist = std::abs(dist - r);
            if (perimeterDist < curveSnapThreshold) {
                return center->pos + (v / dist) * r;
            }
        }

        // Snap to line midpoints (matches the green dots drawn by the renderer).
        const auto& lines = m_sketch->getLines();
        for (const auto& ln : lines) {
            // Skip lines being dragged — their midpoint is moving with them.
            if (m_snapExcludePoints.count(ln.startPointId) ||
                m_snapExcludePoints.count(ln.endPointId)) continue;
            const SketchPoint* p1 = m_sketch->getPoint(ln.startPointId);
            const SketchPoint* p2 = m_sketch->getPoint(ln.endPointId);
            if (!p1 || !p2) continue;
            glm::vec2 mid = 0.5f * (p1->pos + p2->pos);
            if (glm::length(pos - mid) < pointSnapThreshold) {
                m_activeInferences.push_back({InferenceGuide::Midpoint, mid, mid, ln.id});
                return mid;
            }
        }
        // Face-reference line midpoints — same idea on the host face's edges.
        for (const auto& fl : m_sketch->getFaceReferences().lines) {
            glm::vec2 mid = 0.5f * (fl.first + fl.second);
            if (glm::length(pos - mid) < pointSnapThreshold) {
                m_activeInferences.push_back({InferenceGuide::Midpoint, mid, mid, -1});
                return mid;
            }
        }
        // Snap to arc midpoints.
        for (const auto& a : arcs) {
            const SketchPoint* center = m_sketch->getPoint(a.centerPointId);
            const SketchPoint* s = m_sketch->getPoint(a.startPointId);
            const SketchPoint* e = m_sketch->getPoint(a.endPointId);
            if (!center || !s || !e) continue;
            float startA = std::atan2(s->pos.y - center->pos.y, s->pos.x - center->pos.x);
            float endA = std::atan2(e->pos.y - center->pos.y, e->pos.x - center->pos.x);
            const float TWO_PI = 2.0f * static_cast<float>(M_PI);
            float sweep = endA - startA;
            while (sweep < 0.0f) sweep += TWO_PI;
            while (sweep >= TWO_PI) sweep -= TWO_PI;
            float midA = startA + sweep * 0.5f;
            glm::vec2 mid = center->pos + glm::vec2(std::cos(midA), std::sin(midA))
                                            * static_cast<float>(a.radius);
            if (glm::length(pos - mid) < pointSnapThreshold) return mid;
        }
        // Snap to host face centroid (if sketch was started on a face).
        glm::vec2 faceCenter;
        if (m_sketch->getSourceFaceCentroid(faceCenter) &&
            glm::length(pos - faceCenter) < pointSnapThreshold) {
            return faceCenter;
        }

        // On-line snap: cursor lands on the perpendicular projection onto any
        // existing line (excluding endpoints/midpoint, which are handled above).
        // Tighter threshold than endpoint snap so it doesn't drown out the
        // axis-from-point inferences below.
        {
            const float onLineThresh = pointSnapThreshold * 0.7f;
            const SketchLine* bestLn = nullptr;
            glm::vec2 bestProj{0.0f};
            float bestD = onLineThresh;
            for (const auto& ln : lines) {
                // Skip dragged lines.
                if (m_snapExcludePoints.count(ln.startPointId) ||
                    m_snapExcludePoints.count(ln.endPointId)) continue;
                const SketchPoint* p1 = m_sketch->getPoint(ln.startPointId);
                const SketchPoint* p2 = m_sketch->getPoint(ln.endPointId);
                if (!p1 || !p2) continue;
                glm::vec2 ab = p2->pos - p1->pos;
                float len2 = glm::dot(ab, ab);
                if (len2 < 1e-12f) continue;
                float t = glm::clamp(glm::dot(pos - p1->pos, ab) / len2, 0.0f, 1.0f);
                glm::vec2 proj = p1->pos + ab * t;
                float d = glm::distance(pos, proj);
                if (d < bestD) {
                    bestD = d;
                    bestProj = proj;
                    bestLn = &ln;
                }
            }
            if (bestLn) {
                m_activeInferences.push_back(
                    {InferenceGuide::OnLine, bestProj, bestProj, bestLn->id});
                return bestProj;
            }
            // Same on-line check against the face's straight edges.
            glm::vec2 bestFaceProj{0.0f};
            float bestFaceD = onLineThresh;
            bool faceHit = false;
            for (const auto& fl : m_sketch->getFaceReferences().lines) {
                glm::vec2 ab = fl.second - fl.first;
                float len2 = glm::dot(ab, ab);
                if (len2 < 1e-12f) continue;
                float t = glm::clamp(glm::dot(pos - fl.first, ab) / len2, 0.0f, 1.0f);
                glm::vec2 proj = fl.first + ab * t;
                float d = glm::distance(pos, proj);
                if (d < bestFaceD) {
                    bestFaceD = d;
                    bestFaceProj = proj;
                    faceHit = true;
                }
            }
            if (faceHit) {
                m_activeInferences.push_back(
                    {InferenceGuide::OnLine, bestFaceProj, bestFaceProj, -1});
                return bestFaceProj;
            }
        }

        // Extension-of-line: cursor lands on the infinite line through an
        // existing segment, but outside its endpoints. Useful for aligning
        // new geometry with the direction of a diagonal line that doesn't
        // happen to have a point exactly where you want to draw. Renders as
        // a dashed guide from the original segment through the snapped cursor.
        {
            const float extThresh = pointSnapThreshold * 0.6f;
            const SketchLine* bestLn = nullptr;
            glm::vec2 bestProj{0.0f};
            float bestD = extThresh;
            for (const auto& ln : lines) {
                if (m_snapExcludePoints.count(ln.startPointId) ||
                    m_snapExcludePoints.count(ln.endPointId)) continue;
                const SketchPoint* p1 = m_sketch->getPoint(ln.startPointId);
                const SketchPoint* p2 = m_sketch->getPoint(ln.endPointId);
                if (!p1 || !p2) continue;
                glm::vec2 ab = p2->pos - p1->pos;
                float len2 = glm::dot(ab, ab);
                if (len2 < 1e-12f) continue;
                float t = glm::dot(pos - p1->pos, ab) / len2; // UNCLAMPED
                // Only fire OUTSIDE the segment — inside it was handled by
                // the on-line snap above.
                if (t >= 0.0f && t <= 1.0f) continue;
                glm::vec2 proj = p1->pos + ab * t;
                float d = glm::distance(pos, proj);
                if (d < bestD) {
                    bestD = d;
                    bestProj = proj;
                    bestLn = &ln;
                }
            }
            if (bestLn) {
                const SketchPoint* p1 = m_sketch->getPoint(bestLn->startPointId);
                const SketchPoint* p2 = m_sketch->getPoint(bestLn->endPointId);
                // Source the guide line from the existing segment's midpoint
                // (or either endpoint) so the renderer's dashed-line "extend"
                // visual draws the whole infinite line cleanly through both
                // the existing segment and the snapped cursor.
                glm::vec2 from = p1 ? p1->pos : bestProj;
                if (p1 && p2) from = 0.5f * (p1->pos + p2->pos);
                m_activeInferences.push_back(
                    {InferenceGuide::OnLineExtension, from, bestProj, bestLn->id});
                return bestProj;
            }
        }

        // --- Inferences (draw-time alignment, no persistent constraint) ----
        // Axis-from-point: cursor's X / Y aligns with an existing point's X / Y.
        // The "purple line down from point 1" interaction. Width of the snap
        // band is the same as the grid snap so feel is consistent.
        const float axisThresh = std::max(0.2f, m_gridStep * 0.3f);
        // Axis references can come from either an existing sketch point OR a
        // face-reference point — the inference treats them the same way, so
        // store as a generic 2D position + an optional sketch point id.
        glm::vec2 axisVPos{0.0f}, axisHPos{0.0f};
        int axisVRefId = -1, axisHRefId = -1;
        bool axisVFires = false, axisHFires = false;
        float bestVDist = axisThresh;
        float bestHDist = axisThresh;
        // Includes the chain anchor itself — drawing a horizontal or vertical
        // line FROM the anchor is one of the most common cases, so the anchor's
        // axis must be a candidate, not a skipped one. Dragged points are
        // skipped (drawing a guide from a point to itself is meaningless).
        for (const auto& pt : m_sketch->getPoints()) {
            if (m_snapExcludePoints.count(pt.id)) continue;
            float dX = std::abs(pos.x - pt.pos.x);
            float dY = std::abs(pos.y - pt.pos.y);
            if (dX < bestVDist) {
                bestVDist = dX; axisVPos = pt.pos; axisVRefId = pt.id; axisVFires = true;
            }
            if (dY < bestHDist) {
                bestHDist = dY; axisHPos = pt.pos; axisHRefId = pt.id; axisHFires = true;
            }
        }
        for (const auto& fp : m_sketch->getFaceReferences().points) {
            float dX = std::abs(pos.x - fp.x);
            float dY = std::abs(pos.y - fp.y);
            if (dX < bestVDist) {
                bestVDist = dX; axisVPos = fp; axisVRefId = -1; axisVFires = true;
            }
            if (dY < bestHDist) {
                bestHDist = dY; axisHPos = fp; axisHRefId = -1; axisHFires = true;
            }
        }

        // Perpendicular / parallel to previous: when drawing a line chain and
        // we have a direction from the last committed segment, snap the
        // current direction to either perp or parallel when within ~5° of it.
        // Whichever is closer wins; they can't both fire simultaneously.
        bool perpFires = false, parFires = false;
        glm::vec2 perpDir(0.0f), parDir(0.0f);
        glm::vec2 perpProj = pos, parProj = pos;
        if (m_isPlacing && m_hasPrevLineDir && m_mode == SketchToolMode::Line) {
            perpDir = glm::vec2(-m_prevLineDir.y, m_prevLineDir.x); // 90° rotate
            parDir  = m_prevLineDir;
            glm::vec2 v = pos - m_firstClick;
            float len = glm::length(v);
            if (len > 1e-6f) {
                float alongPerp = glm::dot(v, perpDir);
                float alongPar  = glm::dot(v, parDir);
                perpProj = m_firstClick + perpDir * alongPerp;
                parProj  = m_firstClick + parDir  * alongPar;
                float perpOffset = glm::distance(pos, perpProj);
                float parOffset  = glm::distance(pos, parProj);
                // Within ~5° (sin5° ≈ 0.087) of perp / parallel, OR within
                // axisThresh in absolute world units — whichever is more
                // generous at this segment length.
                float angleTol = 0.087f * len;
                float tol = std::max(axisThresh, angleTol);
                bool perpClose = perpOffset < tol;
                bool parClose  = parOffset  < tol;
                if (perpClose && parClose) {
                    if (perpOffset <= parOffset) perpFires = true;
                    else                          parFires  = true;
                } else if (perpClose) {
                    perpFires = true;
                } else if (parClose) {
                    parFires = true;
                }
            }
        }

        // Tangent-to-circle detection: compute direction now so the combine
        // block below can treat it as another direction-lock candidate (so
        // tangent + axis-from-point intersect like perp + axis does).
        bool tangentFires = false;
        glm::vec2 tangentDir(0.0f), tangentProj = pos;
        int tangentRefId = -1;
        if (m_isPlacing && m_mode == SketchToolMode::Line) {
            glm::vec2 v = pos - m_firstClick;
            float len = glm::length(v);
            if (len > 0.5f) {
                float cursorAngle = std::atan2(v.y, v.x);
                const float angTol = 3.0f * static_cast<float>(M_PI) / 180.0f;
                auto angDiff = [](float a, float b) {
                    const float TWO_PI = 2.0f * static_cast<float>(M_PI);
                    float d = std::fmod(std::abs(a - b), TWO_PI);
                    if (d > static_cast<float>(M_PI)) d = TWO_PI - d;
                    return d;
                };
                auto checkTangent = [&](glm::vec2 cpos, double cradius, int refId) {
                    glm::vec2 toC = cpos - m_firstClick;
                    float D = glm::length(toC);
                    if (D <= static_cast<float>(cradius) + 1e-3f) return;
                    float baseAngle = std::atan2(toC.y, toC.x);
                    float tangentOffset = std::asin(static_cast<float>(cradius) / D);
                    float ang1 = baseAngle + tangentOffset;
                    float ang2 = baseAngle - tangentOffset;
                    float d1 = angDiff(cursorAngle, ang1);
                    float d2 = angDiff(cursorAngle, ang2);
                    float bestDelta = std::min(d1, d2);
                    if (bestDelta < angTol) {
                        float snapAng = (d1 < d2) ? ang1 : ang2;
                        tangentDir = glm::vec2(std::cos(snapAng), std::sin(snapAng));
                        tangentProj = m_firstClick + tangentDir * len;
                        tangentRefId = refId;
                        tangentFires = true;
                    }
                };
                for (const auto& circle : m_sketch->getCircles()) {
                    if (tangentFires) break;
                    const SketchPoint* center = m_sketch->getPoint(circle.centerPointId);
                    if (center) checkTangent(center->pos, circle.radius, circle.id);
                }
                for (const auto& arc : m_sketch->getArcs()) {
                    if (tangentFires) break;
                    const SketchPoint* center = m_sketch->getPoint(arc.centerPointId);
                    if (center) checkTangent(center->pos, arc.radius, arc.id);
                }
            }
        }

        // Combine: if a direction-lock (perp / parallel / tangent) AND an
        // axis-from-point both fire, snap to their intersection — e.g. "tangent
        // to that circle AND on its centre's vertical axis" lands on a single
        // unique point. Solved by parametric line / axis intersect.
        bool snapped = false;
        glm::vec2 result = pos;
        auto applyDirLock = [&](glm::vec2 dir, InferenceGuide::Kind dirKind,
                                glm::vec2 projFallback, int dirRefId = -1) -> bool {
            if (axisVFires && std::abs(dir.x) > 1e-3f) {
                float t = (axisVPos.x - m_firstClick.x) / dir.x;
                result = m_firstClick + dir * t;
                m_activeInferences.push_back(
                    {dirKind, m_firstClick, result, dirRefId});
                m_activeInferences.push_back(
                    {InferenceGuide::AxisVFromPoint, axisVPos, result, axisVRefId});
                return true;
            }
            if (axisHFires && std::abs(dir.y) > 1e-3f) {
                float t = (axisHPos.y - m_firstClick.y) / dir.y;
                result = m_firstClick + dir * t;
                m_activeInferences.push_back(
                    {dirKind, m_firstClick, result, dirRefId});
                m_activeInferences.push_back(
                    {InferenceGuide::AxisHFromPoint, axisHPos, result, axisHRefId});
                return true;
            }
            // No axis intersection — just snap onto the locked direction.
            result = projFallback;
            m_activeInferences.push_back({dirKind, m_firstClick, result, dirRefId});
            return true;
        };
        if (perpFires) {
            snapped = applyDirLock(perpDir, InferenceGuide::PerpToPrev, perpProj);
        } else if (parFires) {
            snapped = applyDirLock(parDir, InferenceGuide::ParallelToPrev, parProj);
        } else if (tangentFires) {
            snapped = applyDirLock(tangentDir, InferenceGuide::TangentToCircle,
                                   tangentProj, tangentRefId);
        }
        // Standalone axis-from-point (cursor aligned with any point's X or Y
        // but no perp/parallel/tangent lock) deliberately doesn't fire — with
        // sketch points + face vertices around, the cursor is almost always
        // incidentally aligned with someone's coordinate, so the standalone
        // guide became visual noise. Axis still acts as a co-pilot inside
        // applyDirLock above (perp/parallel/tangent + axis → intersection),
        // which is the actually-useful case. Angle snap and endpoint snap
        // cover everything else.
        if (snapped) return result;

        // Angle-snap fallback: while drawing a line, if nothing above fired,
        // check if the cursor direction from the anchor is within ~3° of a
        // common 15° increment (0, 15, 30, 45, 60, 75, 90, …). If so, snap
        // the angle and emit a grey guide so the user can see the lock.
        // Skipped when the perpendicular / parallel inferences already had a
        // chance — those are stronger semantic intents.
        if (m_isPlacing && m_mode == SketchToolMode::Line) {
            glm::vec2 v = pos - m_firstClick;
            float len = glm::length(v);
            if (len > 0.5f) {
                float a = std::atan2(v.y, v.x);
                const float step = 15.0f * static_cast<float>(M_PI) / 180.0f;
                float snappedA = std::round(a / step) * step;
                float angDelta = std::abs(a - snappedA);
                // Wrap so e.g. 179° vs −180° doesn't read as a large delta.
                const float TWO_PI = 2.0f * static_cast<float>(M_PI);
                if (angDelta > static_cast<float>(M_PI))
                    angDelta = TWO_PI - angDelta;
                // ~3° tolerance — generous enough to feel sticky but small
                // enough that off-axis lines still draw at the actual angle.
                const float angTol = 3.0f * static_cast<float>(M_PI) / 180.0f;
                if (angDelta < angTol) {
                    glm::vec2 dir(std::cos(snappedA), std::sin(snappedA));
                    glm::vec2 snappedPos = m_firstClick + dir * len;
                    m_activeInferences.push_back(
                        {InferenceGuide::AngleSnap, m_firstClick, snappedPos, -1});
                    return snappedPos;
                }
            }
        }
    }

    // Grid snap: ALWAYS round to the nearest grid increment when enabled.
    // A fractional-threshold band (e.g. 25 % of the step) sounds reasonable
    // but at any practical zoom level the band is sub-pixel cursor precision
    // on BOTH axes simultaneously, so the snap silently misses and the user
    // ends up with off-grid sketches when they explicitly asked for 1 mm
    // precision. Inferences (endpoint / midpoint / axis-from-point / etc.)
    // run earlier in this function and override grid snap when active, so
    // precise-but-off-grid placements still work via those. The toolbar
    // "Snap to grid" checkbox is the explicit opt-out for free-form drawing.
    if (!m_snapToGridEnabled || m_gridStep <= 0.0f) return pos;
    glm::vec2 result;
    result.x = std::round(pos.x / m_gridStep) * m_gridStep;
    result.y = std::round(pos.y / m_gridStep) * m_gridStep;
    return result;
}

int SketchTool::findCoincidentPoint(glm::vec2 pos, int excludeId) const {
    if (!m_sketch) return -1;

    const float threshold = 0.3f; // same as point snap threshold
    const auto& points = m_sketch->getPoints();
    for (const auto& pt : points) {
        if (pt.id == excludeId) continue;
        if (glm::length(pos - pt.pos) < threshold) {
            return pt.id;
        }
    }
    return -1;
}

void SketchTool::selectAll() {
    if (!m_sketch) return;
    m_selectedPoints.clear();
    m_selectedLines.clear();
    for (const auto& p : m_sketch->getPoints()) m_selectedPoints.insert(p.id);
    for (const auto& l : m_sketch->getLines())  m_selectedLines.insert(l.id);
}

void SketchTool::handleSelectTool(glm::vec2 pos) {
    if (!m_sketch) return;

    // Hit-test in order of priority: existing point first, then a nearby line.
    int nearPt = findCoincidentPoint(pos, -1);

    int nearLine = -1;
    if (nearPt < 0) {
        // Distance from `pos` to each line segment; pick the closest within tol.
        float bestD = 0.0f;
        const float tol = std::max(m_gridStep * 0.5f, 0.5f); // sketch units
        const auto& lines = m_sketch->getLines();
        for (const auto& l : lines) {
            const SketchPoint* a = m_sketch->getPoint(l.startPointId);
            const SketchPoint* b = m_sketch->getPoint(l.endPointId);
            if (!a || !b) continue;
            glm::vec2 ab = b->pos - a->pos;
            float len2 = glm::dot(ab, ab);
            if (len2 < 1e-12f) continue;
            float t = glm::clamp(glm::dot(pos - a->pos, ab) / len2, 0.0f, 1.0f);
            glm::vec2 proj = a->pos + ab * t;
            float d = glm::distance(proj, pos);
            if (d < tol && (nearLine < 0 || d < bestD)) {
                nearLine = l.id;
                bestD = d;
            }
        }
    }

    // If the user clicked on something that's ALREADY part of the selection,
    // start a drag of the whole selection in-place (don't replace the selection).
    bool reclickedSelected = (nearPt >= 0 && m_selectedPoints.count(nearPt)) ||
                             (nearLine >= 0 && m_selectedLines.count(nearLine));

    if (!m_lastDownAddedToSel && !reclickedSelected) {
        m_selectedPoints.clear();
        m_selectedLines.clear();
    }

    if (nearPt >= 0) {
        m_selectedPoints.insert(nearPt);
        m_dragPointId = nearPt;
        m_isDragging = true; // multi-point drag handled in onMouseMove
    } else if (nearLine >= 0) {
        if (m_lastDownAddedToSel) {
            // Ctrl+click on a line: toggle.
            if (m_selectedLines.count(nearLine)) m_selectedLines.erase(nearLine);
            else m_selectedLines.insert(nearLine);
        } else {
            m_selectedLines.insert(nearLine);
            m_isDragging = true; // dragging the line translates the whole selection
        }
    }
    // Empty space + no modifier: selection already cleared above.
}

void SketchTool::handleLineTool(glm::vec2 pos) {
    if (!m_isPlacing) {
        // First click: set start point
        m_firstClick = pos;
        m_isPlacing = true;
        m_clickCount = 1;

        // Check if we're snapping to an existing point
        m_chainStartPointCreated = false;
        if (m_lastPointId == -1) {
            const auto& points = m_sketch->getPoints();
            for (const auto& pt : points) {
                if (glm::length(pos - pt.pos) < 1e-4f) {
                    m_lastPointId = pt.id;
                    break;
                }
            }
            if (m_lastPointId == -1) {
                m_lastPointId = m_sketch->addPoint(pos);
                m_chainStartPointCreated = true; // brand-new, drop on Esc
            }
        }
        // Remember where the chain started — closing back onto this point
        // auto-completes the loop and ends placement (see below).
        m_chainStartPointId = m_lastPointId;
    } else {
        // Second click: create line and continue chain
        int endPointId = -1;

        // Check if snapping to existing point
        const auto& points = m_sketch->getPoints();
        for (const auto& pt : points) {
            if (pt.id != m_lastPointId && glm::length(pos - pt.pos) < 1e-4f) {
                endPointId = pt.id;
                break;
            }
        }

        if (endPointId == -1) {
            endPointId = m_sketch->addPoint(pos);
        }

        int newLineId = m_sketch->addLine(m_lastPointId, endPointId);
        // Constraints are entirely opt-in — no autoConstrain on creation. The
        // user applies Horizontal / Vertical / Coincident / etc. explicitly via
        // the toolbar when they want one.
        (void)newLineId;

        // Remember the direction of the segment we just committed — the
        // perpendicular- and parallel-to-previous inferences need it while the
        // user places the next vertex in the chain.
        const SketchPoint* spJustEnded = m_sketch->getPoint(m_lastPointId);
        const SketchPoint* epJustEnded = m_sketch->getPoint(endPointId);
        if (spJustEnded && epJustEnded) {
            glm::vec2 d = epJustEnded->pos - spJustEnded->pos;
            float dl = glm::length(d);
            if (dl > 1e-6f) {
                m_prevLineDir = d / dl;
                m_hasPrevLineDir = true;
            }
        }

        // Auto-close: if the chain has at least two committed segments and the
        // endpoint we just placed IS the chain's starting point, the loop is
        // closed — commit and end the chain so the user doesn't have to hit
        // Esc/Enter to escape line mode.
        if (m_clickCount >= 2 && endPointId == m_chainStartPointId) {
            m_isPlacing = false;
            m_clickCount = 0;
            m_lastPointId = -1;
            m_chainStartPointId = -1;
            m_hasPrevLineDir = false;
            m_activeInferences.clear();
            return;
        }

        // Continue chaining: the end becomes the new start
        m_lastPointId = endPointId;
        m_firstClick = pos;
        m_clickCount++;
    }
}

void SketchTool::handleCircleTool(glm::vec2 pos) {
    if (!m_isPlacing) {
        // First click: set center
        m_firstClick = pos;
        m_isPlacing = true;
        m_clickCount = 1;
    } else {
        // Second click: set radius point, create circle
        float radius = glm::length(pos - m_firstClick);
        if (radius > 1e-6f) {
            // Reuse the existing point at this position if there is one
            // (this is how concentric circles share a center).
            int existing = findCoincidentPoint(m_firstClick, -1);
            int centerId = (existing >= 0) ? existing : m_sketch->addPoint(m_firstClick);
            m_sketch->addCircle(centerId, static_cast<double>(radius));
        }

        m_isPlacing = false;
        m_clickCount = 0;
    }
}

void SketchTool::handleRectangleTool(glm::vec2 pos) {
    if (!m_isPlacing) {
        // First click: set first corner
        m_firstClick = pos;
        m_isPlacing = true;
        m_clickCount = 1;
    } else {
        // Second click: set opposite corner, create rectangle
        if (std::abs(pos.x - m_firstClick.x) > 1e-6f &&
            std::abs(pos.y - m_firstClick.y) > 1e-6f) {
            m_sketch->addRectangle(m_firstClick, pos);
        }

        m_isPlacing = false;
        m_clickCount = 0;
    }
}

void SketchTool::handleArcTool(glm::vec2 pos) {
    if (!m_isPlacing) {
        // First click: set start point
        m_firstClick = pos;
        m_isPlacing = true;
        m_clickCount = 1;
    } else if (m_clickCount == 1) {
        // Second click: set end point
        m_secondClick = pos;
        m_clickCount = 2;
    } else if (m_clickCount == 2) {
        // Third click: set midpoint on arc, compute center and radius
        glm::vec2 start = m_firstClick;
        glm::vec2 end = m_secondClick;
        glm::vec2 mid = pos;

        // Compute the circumcenter of the three points (start, mid, end)
        // This gives us the arc center
        float ax = start.x, ay = start.y;
        float bx = mid.x, by = mid.y;
        float cx = end.x, cy = end.y;

        float D = 2.0f * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));

        if (std::abs(D) > 1e-10f) {
            float ux = ((ax * ax + ay * ay) * (by - cy) +
                        (bx * bx + by * by) * (cy - ay) +
                        (cx * cx + cy * cy) * (ay - by)) / D;
            float uy = ((ax * ax + ay * ay) * (cx - bx) +
                        (bx * bx + by * by) * (ax - cx) +
                        (cx * cx + cy * cy) * (bx - ax)) / D;

            glm::vec2 center(ux, uy);
            float radius = glm::length(start - center);

            auto reuseOrAdd = [&](glm::vec2 p) {
                int existing = findCoincidentPoint(p, -1);
                return (existing >= 0) ? existing : m_sketch->addPoint(p);
            };
            int centerId = reuseOrAdd(center);
            int startId  = reuseOrAdd(start);
            int endId    = reuseOrAdd(end);

            m_sketch->addArc(centerId, startId, endId, static_cast<double>(radius));
        }

        m_isPlacing = false;
        m_clickCount = 0;
    }
}

void SketchTool::handleSplineTool(glm::vec2 pos) {
    if (!m_sketch) return;

    // Each click adds a control point
    int ptId = -1;

    // Check if snapping to an existing point
    const auto& points = m_sketch->getPoints();
    for (const auto& pt : points) {
        if (glm::length(pos - pt.pos) < 1e-4f) {
            ptId = pt.id;
            break;
        }
    }

    if (ptId == -1) {
        ptId = m_sketch->addPoint(pos);
    }

    m_splinePoints.push_back(ptId);

    if (!m_isPlacing) {
        m_firstClick = pos;
        m_isPlacing = true;
    }

    m_clickCount = static_cast<int>(m_splinePoints.size());
    // The spline is finalized via onConfirm() (Enter key)
}

// ─── Trim tool ──────────────────────────────────────────────────────────────
//
// The trim tool removes the segment of a sketch element between its two nearest
// intersections with any other element. If the element has no intersections,
// the whole element is removed. Splines and polygons are not split — clicking
// one removes it entirely.

namespace {

// Wrap angle into [0, 2π).
float wrap2Pi(float a) {
    const float TWO_PI = 2.0f * static_cast<float>(M_PI);
    while (a < 0.0f) a += TWO_PI;
    while (a >= TWO_PI) a -= TWO_PI;
    return a;
}

// Closest point on segment p1→p2 to q, returns squared distance.
float distSqPointSegment(glm::vec2 q, glm::vec2 p1, glm::vec2 p2, float* outT = nullptr) {
    glm::vec2 d = p2 - p1;
    float len2 = glm::dot(d, d);
    if (len2 < 1e-12f) {
        if (outT) *outT = 0.0f;
        glm::vec2 v = q - p1;
        return glm::dot(v, v);
    }
    float t = glm::dot(q - p1, d) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    if (outT) *outT = t;
    glm::vec2 closest = p1 + t * d;
    glm::vec2 v = q - closest;
    return glm::dot(v, v);
}

// True if angle θ falls within arc going CCW from startAngle to endAngle.
bool angleInArc(float theta, float startAngle, float endAngle) {
    float s = wrap2Pi(startAngle);
    float e = wrap2Pi(endAngle);
    float t = wrap2Pi(theta);
    if (s <= e) return t >= s - 1e-5f && t <= e + 1e-5f;
    // arc crosses 0
    return t >= s - 1e-5f || t <= e + 1e-5f;
}

struct Hit { glm::vec2 pos; float param; };

// Intersect line p1-p2 with line p3-p4. param is t along p1→p2.
void intersectLineLine(glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, glm::vec2 p4,
                       std::vector<Hit>& out) {
    glm::vec2 d1 = p2 - p1;
    glm::vec2 d2 = p4 - p3;
    float denom = d1.x * d2.y - d1.y * d2.x;
    if (std::abs(denom) < 1e-10f) return; // parallel
    float t = ((p3.x - p1.x) * d2.y - (p3.y - p1.y) * d2.x) / denom;
    float u = ((p3.x - p1.x) * d1.y - (p3.y - p1.y) * d1.x) / denom;
    const float eps = 1e-4f;
    if (t < -eps || t > 1.0f + eps) return;
    if (u < -eps || u > 1.0f + eps) return;
    out.push_back({p1 + t * d1, t});
}

// Intersect line p1-p2 with circle (center, radius). param = t along the line.
void intersectLineCircle(glm::vec2 p1, glm::vec2 p2, glm::vec2 center, float radius,
                         std::vector<Hit>& out) {
    glm::vec2 d = p2 - p1;
    glm::vec2 f = p1 - center;
    float a = glm::dot(d, d);
    float b = 2.0f * glm::dot(f, d);
    float c = glm::dot(f, f) - radius * radius;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return;
    disc = std::sqrt(disc);
    const float eps = 1e-4f;
    for (float t : { (-b - disc) / (2 * a), (-b + disc) / (2 * a) }) {
        if (t < -eps || t > 1.0f + eps) continue;
        out.push_back({p1 + t * d, t});
    }
}

// Intersect line with arc (filter line-circle by arc angle range).
void intersectLineArc(glm::vec2 p1, glm::vec2 p2, glm::vec2 center, float radius,
                      float startAngle, float endAngle, std::vector<Hit>& out) {
    std::vector<Hit> tmp;
    intersectLineCircle(p1, p2, center, radius, tmp);
    for (const auto& h : tmp) {
        glm::vec2 r = h.pos - center;
        float theta = std::atan2(r.y, r.x);
        if (angleInArc(theta, startAngle, endAngle)) out.push_back(h);
    }
}

// Intersect circle (c1,r1) with circle (c2,r2). Param = angle on circle 1.
void intersectCircleCircle(glm::vec2 c1, float r1, glm::vec2 c2, float r2,
                           std::vector<Hit>& out) {
    glm::vec2 d = c2 - c1;
    float dist = glm::length(d);
    if (dist < 1e-10f) return; // concentric
    if (dist > r1 + r2 + 1e-5f) return;
    if (dist < std::abs(r1 - r2) - 1e-5f) return;
    float a = (r1 * r1 - r2 * r2 + dist * dist) / (2.0f * dist);
    float h2 = r1 * r1 - a * a;
    float h = (h2 > 0.0f) ? std::sqrt(h2) : 0.0f;
    glm::vec2 mid = c1 + d * (a / dist);
    glm::vec2 perp(-d.y / dist, d.x / dist);
    for (int sign : { 1, -1 }) {
        glm::vec2 p = mid + perp * (h * static_cast<float>(sign));
        float theta = std::atan2(p.y - c1.y, p.x - c1.x);
        out.push_back({p, theta});
        if (h < 1e-6f) break; // tangent
    }
}

} // anonymous

// Returns elementId of the picked element, or -1 if none within threshold.
// Sets pickType to one of "line","circle","arc","spline","polygon".
static int pickSketchElement(const Sketch& sketch, glm::vec2 pos, float threshold,
                             std::string& pickType) {
    float bestDistSq = threshold * threshold;
    int bestId = -1;
    pickType.clear();

    // Lines
    for (const auto& ln : sketch.getLines()) {
        const SketchPoint* a = sketch.getPoint(ln.startPointId);
        const SketchPoint* b = sketch.getPoint(ln.endPointId);
        if (!a || !b) continue;
        float dsq = distSqPointSegment(pos, a->pos, b->pos);
        if (dsq < bestDistSq) { bestDistSq = dsq; bestId = ln.id; pickType = "line"; }
    }
    // Circles
    for (const auto& ci : sketch.getCircles()) {
        const SketchPoint* c = sketch.getPoint(ci.centerPointId);
        if (!c) continue;
        float d = glm::length(pos - c->pos);
        float dsq = (d - static_cast<float>(ci.radius)) * (d - static_cast<float>(ci.radius));
        if (dsq < bestDistSq) { bestDistSq = dsq; bestId = ci.id; pickType = "circle"; }
    }
    // Arcs
    for (const auto& ar : sketch.getArcs()) {
        const SketchPoint* c = sketch.getPoint(ar.centerPointId);
        const SketchPoint* s = sketch.getPoint(ar.startPointId);
        const SketchPoint* e = sketch.getPoint(ar.endPointId);
        if (!c || !s || !e) continue;
        glm::vec2 r = pos - c->pos;
        float distC = glm::length(r);
        float perimeterDist = std::abs(distC - static_cast<float>(ar.radius));
        float theta = std::atan2(r.y, r.x);
        float startA = std::atan2(s->pos.y - c->pos.y, s->pos.x - c->pos.x);
        float endA = std::atan2(e->pos.y - c->pos.y, e->pos.x - c->pos.x);
        if (angleInArc(theta, startA, endA)) {
            float dsq = perimeterDist * perimeterDist;
            if (dsq < bestDistSq) { bestDistSq = dsq; bestId = ar.id; pickType = "arc"; }
        }
    }
    // Splines / polygons: bounding-box hit only (no precise picking) — fall back to
    // simple proximity to control points or vertices.
    for (const auto& sp : sketch.getSplines()) {
        for (int pid : sp.controlPointIds) {
            const SketchPoint* p = sketch.getPoint(pid);
            if (!p) continue;
            glm::vec2 v = pos - p->pos;
            float dsq = glm::dot(v, v);
            if (dsq < bestDistSq) { bestDistSq = dsq; bestId = sp.id; pickType = "spline"; }
        }
    }
    for (const auto& po : sketch.getPolygons()) {
        for (int pid : po.vertexPointIds) {
            const SketchPoint* p = sketch.getPoint(pid);
            if (!p) continue;
            glm::vec2 v = pos - p->pos;
            float dsq = glm::dot(v, v);
            if (dsq < bestDistSq) { bestDistSq = dsq; bestId = po.id; pickType = "polygon"; }
        }
    }
    return bestId;
}

// Collect intersection points + parameters along a given line element.
static void collectLineIntersections(const Sketch& sketch, const SketchLine& target,
                                     std::vector<Hit>& out) {
    const SketchPoint* a = sketch.getPoint(target.startPointId);
    const SketchPoint* b = sketch.getPoint(target.endPointId);
    if (!a || !b) return;
    glm::vec2 p1 = a->pos, p2 = b->pos;

    for (const auto& ln : sketch.getLines()) {
        if (ln.id == target.id) continue;
        const SketchPoint* c = sketch.getPoint(ln.startPointId);
        const SketchPoint* d = sketch.getPoint(ln.endPointId);
        if (!c || !d) continue;
        intersectLineLine(p1, p2, c->pos, d->pos, out);
    }
    for (const auto& ci : sketch.getCircles()) {
        const SketchPoint* c = sketch.getPoint(ci.centerPointId);
        if (!c) continue;
        intersectLineCircle(p1, p2, c->pos, static_cast<float>(ci.radius), out);
    }
    for (const auto& ar : sketch.getArcs()) {
        const SketchPoint* c = sketch.getPoint(ar.centerPointId);
        const SketchPoint* s = sketch.getPoint(ar.startPointId);
        const SketchPoint* e = sketch.getPoint(ar.endPointId);
        if (!c || !s || !e) continue;
        float startA = std::atan2(s->pos.y - c->pos.y, s->pos.x - c->pos.x);
        float endA = std::atan2(e->pos.y - c->pos.y, e->pos.x - c->pos.x);
        intersectLineArc(p1, p2, c->pos, static_cast<float>(ar.radius), startA, endA, out);
    }
}

// Collect intersection angles around a circle element (parameters are angles in [0,2π)).
static void collectCircleIntersections(const Sketch& sketch, glm::vec2 center, float radius,
                                       int skipId, std::vector<Hit>& out) {
    for (const auto& ln : sketch.getLines()) {
        if (ln.id == skipId) continue;
        const SketchPoint* a = sketch.getPoint(ln.startPointId);
        const SketchPoint* b = sketch.getPoint(ln.endPointId);
        if (!a || !b) continue;
        std::vector<Hit> tmp;
        intersectLineCircle(a->pos, b->pos, center, radius, tmp);
        for (auto& h : tmp) {
            float theta = std::atan2(h.pos.y - center.y, h.pos.x - center.x);
            out.push_back({h.pos, wrap2Pi(theta)});
        }
    }
    for (const auto& ci : sketch.getCircles()) {
        if (ci.id == skipId) continue;
        const SketchPoint* c = sketch.getPoint(ci.centerPointId);
        if (!c) continue;
        std::vector<Hit> tmp;
        intersectCircleCircle(center, radius, c->pos, static_cast<float>(ci.radius), tmp);
        for (auto& h : tmp) out.push_back({h.pos, wrap2Pi(h.param)});
    }
    for (const auto& ar : sketch.getArcs()) {
        if (ar.id == skipId) continue;
        const SketchPoint* c = sketch.getPoint(ar.centerPointId);
        const SketchPoint* s = sketch.getPoint(ar.startPointId);
        const SketchPoint* e = sketch.getPoint(ar.endPointId);
        if (!c || !s || !e) continue;
        std::vector<Hit> tmp;
        intersectCircleCircle(center, radius, c->pos, static_cast<float>(ar.radius), tmp);
        float startA = std::atan2(s->pos.y - c->pos.y, s->pos.x - c->pos.x);
        float endA = std::atan2(e->pos.y - c->pos.y, e->pos.x - c->pos.x);
        for (auto& h : tmp) {
            glm::vec2 r = h.pos - c->pos;
            float thetaOnOther = std::atan2(r.y, r.x);
            if (!angleInArc(thetaOnOther, startA, endA)) continue;
            float theta = std::atan2(h.pos.y - center.y, h.pos.x - center.x);
            out.push_back({h.pos, wrap2Pi(theta)});
        }
    }
}

namespace {

// Plan for a single trim click: what to delete, plus the data needed to
// regenerate the surviving sub-segments. Computed by planTrim() without any
// mutation so that both the hover preview and the actual click use the same
// geometric decision.
struct TrimAction {
    enum class Kind { None, FullDelete, Line, Circle, Arc };

    Kind kind = Kind::None;
    int targetId = -1;

    // Line
    glm::vec2 lineP1{0}, lineP2{0};
    int lineKillIndex = -1;
    std::vector<float> lineBounds;

    // Circle (whole circle being cut into arcs)
    glm::vec2 circCenter{0};
    float circRadius = 0.0f;
    int circCenterPointId = -1;
    int circKillIndex = -1;
    std::vector<float> circAngles; // CCW-sorted intersection angles in [0, 2π)

    // Arc (existing arc being cut into sub-arcs)
    glm::vec2 arcCenter{0};
    float arcRadius = 0.0f;
    float arcStartA = 0.0f;
    int arcCenterPointId = -1;
    int arcKillIndex = -1;
    std::vector<float> arcBounds; // CCW distances from arcStartA: [0, d1, ..., totalArc]

    bool valid() const { return kind != Kind::None; }
};

TrimAction planTrim(const Sketch& sketch, glm::vec2 pos, float threshold) {
    TrimAction action;
    std::string pickType;
    int id = pickSketchElement(sketch, pos, threshold, pickType);
    if (id < 0) return action;
    action.targetId = id;

    if (pickType == "spline" || pickType == "polygon") {
        action.kind = TrimAction::Kind::FullDelete;
        return action;
    }

    if (pickType == "line") {
        const SketchLine* line = nullptr;
        for (const auto& l : sketch.getLines()) if (l.id == id) { line = &l; break; }
        if (!line) return action;
        const SketchPoint* a = sketch.getPoint(line->startPointId);
        const SketchPoint* b = sketch.getPoint(line->endPointId);
        if (!a || !b) return action;
        action.lineP1 = a->pos;
        action.lineP2 = b->pos;

        std::vector<Hit> hits;
        collectLineIntersections(sketch, *line, hits);
        if (hits.empty()) { action.kind = TrimAction::Kind::FullDelete; return action; }

        std::sort(hits.begin(), hits.end(),
                  [](const Hit& x, const Hit& y) { return x.param < y.param; });
        std::vector<Hit> uniq;
        for (const auto& h : hits) {
            if (uniq.empty() || std::abs(h.param - uniq.back().param) > 1e-3f) uniq.push_back(h);
        }

        std::vector<float> bounds;
        bounds.push_back(0.0f);
        for (const auto& h : uniq) {
            if (h.param > 1e-3f && h.param < 1.0f - 1e-3f) bounds.push_back(h.param);
        }
        bounds.push_back(1.0f);

        float tClick;
        distSqPointSegment(pos, action.lineP1, action.lineP2, &tClick);

        int killIndex = -1;
        for (int i = 0; i + 1 < static_cast<int>(bounds.size()); ++i) {
            if (tClick >= bounds[i] - 1e-3f && tClick <= bounds[i + 1] + 1e-3f) {
                killIndex = i; break;
            }
        }
        if (killIndex < 0) { action.kind = TrimAction::Kind::FullDelete; return action; }

        action.kind = TrimAction::Kind::Line;
        action.lineBounds = std::move(bounds);
        action.lineKillIndex = killIndex;
        return action;
    }

    if (pickType == "circle") {
        const SketchCircle* circ = nullptr;
        for (const auto& c : sketch.getCircles()) if (c.id == id) { circ = &c; break; }
        if (!circ) return action;
        const SketchPoint* cp = sketch.getPoint(circ->centerPointId);
        if (!cp) return action;
        action.circCenter = cp->pos;
        action.circRadius = static_cast<float>(circ->radius);
        action.circCenterPointId = circ->centerPointId;

        std::vector<Hit> hits;
        collectCircleIntersections(sketch, action.circCenter, action.circRadius, id, hits);
        if (hits.empty()) { action.kind = TrimAction::Kind::FullDelete; return action; }

        std::sort(hits.begin(), hits.end(),
                  [](const Hit& x, const Hit& y) { return x.param < y.param; });
        std::vector<float> angles;
        for (const auto& h : hits) {
            if (angles.empty() || std::abs(h.param - angles.back()) > 1e-3f) angles.push_back(h.param);
        }

        glm::vec2 r = pos - action.circCenter;
        float thetaClick = wrap2Pi(std::atan2(r.y, r.x));
        int n = static_cast<int>(angles.size());
        int killIndex = -1;
        for (int i = 0; i < n; ++i) {
            float a = angles[i];
            float b = angles[(i + 1) % n];
            if (angleInArc(thetaClick, a, b)) { killIndex = i; break; }
        }
        if (killIndex < 0) { action.kind = TrimAction::Kind::FullDelete; return action; }

        action.kind = TrimAction::Kind::Circle;
        action.circAngles = std::move(angles);
        action.circKillIndex = killIndex;
        return action;
    }

    if (pickType == "arc") {
        const SketchArc* arc = nullptr;
        for (const auto& a : sketch.getArcs()) if (a.id == id) { arc = &a; break; }
        if (!arc) return action;
        const SketchPoint* cp = sketch.getPoint(arc->centerPointId);
        const SketchPoint* sp = sketch.getPoint(arc->startPointId);
        const SketchPoint* ep = sketch.getPoint(arc->endPointId);
        if (!cp || !sp || !ep) return action;
        action.arcCenter = cp->pos;
        action.arcRadius = static_cast<float>(arc->radius);
        action.arcStartA = std::atan2(sp->pos.y - cp->pos.y, sp->pos.x - cp->pos.x);
        action.arcCenterPointId = arc->centerPointId;
        float endA = std::atan2(ep->pos.y - cp->pos.y, ep->pos.x - cp->pos.x);
        float totalCCW = wrap2Pi(endA - action.arcStartA);

        std::vector<Hit> hits;
        collectCircleIntersections(sketch, action.arcCenter, action.arcRadius, id, hits);
        std::vector<float> inRangeD; // CCW distances of in-range intersections
        for (const auto& h : hits) {
            float d = wrap2Pi(h.param - action.arcStartA);
            if (d > 1e-3f && d < totalCCW - 1e-3f) inRangeD.push_back(d);
        }
        if (inRangeD.empty()) { action.kind = TrimAction::Kind::FullDelete; return action; }

        std::sort(inRangeD.begin(), inRangeD.end());
        std::vector<float> bounds;
        bounds.push_back(0.0f);
        for (float d : inRangeD) {
            if (bounds.empty() || std::abs(d - bounds.back()) > 1e-3f) bounds.push_back(d);
        }
        bounds.push_back(totalCCW);

        glm::vec2 r = pos - action.arcCenter;
        float dClick = wrap2Pi(std::atan2(r.y, r.x) - action.arcStartA);
        int killIndex = -1;
        for (int i = 0; i + 1 < static_cast<int>(bounds.size()); ++i) {
            if (dClick >= bounds[i] - 1e-3f && dClick <= bounds[i + 1] + 1e-3f) {
                killIndex = i; break;
            }
        }
        if (killIndex < 0) { action.kind = TrimAction::Kind::FullDelete; return action; }

        action.kind = TrimAction::Kind::Arc;
        action.arcBounds = std::move(bounds);
        action.arcKillIndex = killIndex;
        return action;
    }

    return action;
}

// Densify the segment that would be removed into 2D sketch-space points.
// For FullDelete the entire element is densified (so the renderer can highlight it).
void densifyTrimPreview(const Sketch& sketch, const TrimAction& a, std::vector<glm::vec2>& out) {
    out.clear();
    if (a.kind == TrimAction::Kind::FullDelete) {
        for (const auto& l : sketch.getLines()) if (l.id == a.targetId) {
            const SketchPoint* p1 = sketch.getPoint(l.startPointId);
            const SketchPoint* p2 = sketch.getPoint(l.endPointId);
            if (p1 && p2) { out.push_back(p1->pos); out.push_back(p2->pos); }
            return;
        }
        for (const auto& c : sketch.getCircles()) if (c.id == a.targetId) {
            const SketchPoint* cp = sketch.getPoint(c.centerPointId);
            if (!cp) return;
            float r = static_cast<float>(c.radius);
            const int samples = 64;
            for (int i = 0; i <= samples; ++i) {
                float t = 2.0f * static_cast<float>(M_PI) * i / samples;
                out.push_back(cp->pos + glm::vec2(std::cos(t), std::sin(t)) * r);
            }
            return;
        }
        for (const auto& ar : sketch.getArcs()) if (ar.id == a.targetId) {
            const SketchPoint* cp = sketch.getPoint(ar.centerPointId);
            const SketchPoint* sp = sketch.getPoint(ar.startPointId);
            const SketchPoint* ep = sketch.getPoint(ar.endPointId);
            if (!cp || !sp || !ep) return;
            float r = static_cast<float>(ar.radius);
            float startA = std::atan2(sp->pos.y - cp->pos.y, sp->pos.x - cp->pos.x);
            float endA = std::atan2(ep->pos.y - cp->pos.y, ep->pos.x - cp->pos.x);
            float total = wrap2Pi(endA - startA);
            const int samples = 32;
            for (int i = 0; i <= samples; ++i) {
                float t = total * i / samples;
                float th = startA + t;
                out.push_back(cp->pos + glm::vec2(std::cos(th), std::sin(th)) * r);
            }
            return;
        }
        for (const auto& spl : sketch.getSplines()) if (spl.id == a.targetId) {
            for (int pid : spl.controlPointIds) {
                const SketchPoint* p = sketch.getPoint(pid);
                if (p) out.push_back(p->pos);
            }
            return;
        }
        for (const auto& po : sketch.getPolygons()) if (po.id == a.targetId) {
            for (int pid : po.vertexPointIds) {
                const SketchPoint* p = sketch.getPoint(pid);
                if (p) out.push_back(p->pos);
            }
            if (!out.empty()) out.push_back(out.front());
            return;
        }
        return;
    }
    switch (a.kind) {
        case TrimAction::Kind::None: return;
        case TrimAction::Kind::FullDelete: return; // handled above
        case TrimAction::Kind::Line: {
            float t0 = a.lineBounds[a.lineKillIndex];
            float t1 = a.lineBounds[a.lineKillIndex + 1];
            out.push_back(a.lineP1 + t0 * (a.lineP2 - a.lineP1));
            out.push_back(a.lineP1 + t1 * (a.lineP2 - a.lineP1));
            return;
        }
        case TrimAction::Kind::Circle: {
            int n = static_cast<int>(a.circAngles.size());
            float a0 = a.circAngles[a.circKillIndex];
            float a1 = a.circAngles[(a.circKillIndex + 1) % n];
            float sweep = wrap2Pi(a1 - a0);
            const int samples = 32;
            for (int i = 0; i <= samples; ++i) {
                float t = static_cast<float>(i) / samples;
                float th = a0 + sweep * t;
                out.push_back(a.circCenter +
                              glm::vec2(std::cos(th), std::sin(th)) * a.circRadius);
            }
            return;
        }
        case TrimAction::Kind::Arc: {
            float d0 = a.arcBounds[a.arcKillIndex];
            float d1 = a.arcBounds[a.arcKillIndex + 1];
            const int samples = 32;
            for (int i = 0; i <= samples; ++i) {
                float t = static_cast<float>(i) / samples;
                float th = a.arcStartA + d0 + (d1 - d0) * t;
                out.push_back(a.arcCenter +
                              glm::vec2(std::cos(th), std::sin(th)) * a.arcRadius);
            }
            return;
        }
    }
}

// Reuse an existing sketch point if one already sits within `tol` of `pos`,
// otherwise add a new one. Critical for trim: new sub-segments must share their
// endpoint IDs with the adjacent geometry (the other element that defined the
// intersection point), otherwise buildWires() can't stitch them into a closed
// loop and buildRegions() never sees a manifold region for push/pull.
int findOrAddPoint(Sketch& sketch, glm::vec2 pos, float tol = 1e-2f) {
    for (const auto& p : sketch.getPoints()) {
        if (glm::length(pos - p.pos) < tol) return p.id;
    }
    return sketch.addPoint(pos);
}

void applyTrim(Sketch& sketch, const TrimAction& a) {
    if (a.kind == TrimAction::Kind::None) return;

    if (a.kind == TrimAction::Kind::FullDelete) {
        sketch.removeElement(a.targetId);
        return;
    }

    if (a.kind == TrimAction::Kind::Line) {
        sketch.removeElement(a.targetId);
        for (int i = 0; i + 1 < static_cast<int>(a.lineBounds.size()); ++i) {
            if (i == a.lineKillIndex) continue;
            glm::vec2 sp = a.lineP1 + a.lineBounds[i] * (a.lineP2 - a.lineP1);
            glm::vec2 ep = a.lineP1 + a.lineBounds[i + 1] * (a.lineP2 - a.lineP1);
            int sid = findOrAddPoint(sketch, sp);
            int eid = findOrAddPoint(sketch, ep);
            sketch.addLine(sid, eid);
        }
        return;
    }

    if (a.kind == TrimAction::Kind::Circle) {
        sketch.removeElement(a.targetId);
        int centerId = sketch.getPoint(a.circCenterPointId) ? a.circCenterPointId
                                                            : findOrAddPoint(sketch, a.circCenter);
        int n = static_cast<int>(a.circAngles.size());
        for (int i = 0; i < n; ++i) {
            if (i == a.circKillIndex) continue;
            float a0 = a.circAngles[i];
            float a1 = a.circAngles[(i + 1) % n];
            glm::vec2 sp = a.circCenter + glm::vec2(std::cos(a0), std::sin(a0)) * a.circRadius;
            glm::vec2 ep = a.circCenter + glm::vec2(std::cos(a1), std::sin(a1)) * a.circRadius;
            int sid = findOrAddPoint(sketch, sp);
            int eid = findOrAddPoint(sketch, ep);
            sketch.addArc(centerId, sid, eid, static_cast<double>(a.circRadius));
        }
        return;
    }

    if (a.kind == TrimAction::Kind::Arc) {
        sketch.removeElement(a.targetId);
        int centerId = sketch.getPoint(a.arcCenterPointId) ? a.arcCenterPointId
                                                           : findOrAddPoint(sketch, a.arcCenter);
        for (int i = 0; i + 1 < static_cast<int>(a.arcBounds.size()); ++i) {
            if (i == a.arcKillIndex) continue;
            float angA = a.arcStartA + a.arcBounds[i];
            float angB = a.arcStartA + a.arcBounds[i + 1];
            glm::vec2 sp = a.arcCenter + glm::vec2(std::cos(angA), std::sin(angA)) * a.arcRadius;
            glm::vec2 ep = a.arcCenter + glm::vec2(std::cos(angB), std::sin(angB)) * a.arcRadius;
            int sid = findOrAddPoint(sketch, sp);
            int eid = findOrAddPoint(sketch, ep);
            sketch.addArc(centerId, sid, eid, static_cast<double>(a.arcRadius));
        }
        return;
    }
}

} // anonymous

void SketchTool::handleTrimTool(glm::vec2 pos) {
    if (!m_sketch) return;
    float threshold = std::max(0.3f, m_gridStep * 0.5f);
    TrimAction a = planTrim(*m_sketch, pos, threshold);
    if (!a.valid()) return;
    applyTrim(*m_sketch, a);
    m_trimHoverPoints.clear(); // stale after mutation; recomputed on next move
}

void SketchTool::computeTrimHover(glm::vec2 pos) {
    m_trimHoverPoints.clear();
    if (!m_sketch) return;
    float threshold = std::max(0.3f, m_gridStep * 0.5f);
    TrimAction a = planTrim(*m_sketch, pos, threshold);
    if (a.valid()) densifyTrimPreview(*m_sketch, a, m_trimHoverPoints);
}

void SketchTool::handlePolygonTool(glm::vec2 pos) {
    if (!m_isPlacing) {
        // First click: set center
        m_firstClick = pos;
        m_isPlacing = true;
        m_clickCount = 1;
    } else {
        // Second click: set radius + rotation and create polygon. Cursor
        // direction from center defines vertex 0's angle, so the first
        // vertex lands exactly under the (grid-snapped) cursor — same
        // "corner snaps to grid" behaviour the user gets with circles.
        glm::vec2 delta = pos - m_firstClick;
        float radius = glm::length(delta);
        if (radius > 1e-6f) {
            int existing = findCoincidentPoint(m_firstClick, -1);
            int centerId = (existing >= 0) ? existing : m_sketch->addPoint(m_firstClick);
            double rotation = std::atan2(delta.y, delta.x);
            m_sketch->addPolygon(centerId, static_cast<double>(radius),
                                 m_polygonSides, rotation);
        }

        m_isPlacing = false;
        m_clickCount = 0;
    }
}

} // namespace materializr
