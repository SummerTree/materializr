#pragma once
#include "Sketch.h"
#include "SketchSolver.h"
#include "SvgImport.h"
#include <glm/glm.hpp>
#include <functional>
#include <set>
#include <string>

namespace materializr {

enum class SketchToolMode { None, Select, Line, Circle, Rectangle, Arc, Spline, Polygon, Trim, Text, Svg };

// One drawing-time alignment hint. Inferences are transient — they describe
// what the cursor IS aligned to right now, get drawn as coloured ghost lines /
// markers, and disappear after the click is placed. No constraint metadata is
// stored on the resulting geometry; the placed point is just a point.
struct InferenceGuide {
    enum Kind {
        Endpoint,       // cursor snapped onto an existing sketch point
        Midpoint,       // cursor snapped onto the midpoint of a line / arc
        OnLine,         // cursor projected onto an existing line (not at an endpoint/midpoint)
        AxisHFromPoint, // cursor's Y aligns with an existing point's Y → red horizontal guide
        AxisVFromPoint, // cursor's X aligns with an existing point's X → green vertical guide
        PerpToPrev,        // cursor is on the perpendicular ray from the chain's previous segment → orange guide
        ParallelToPrev,    // cursor is on the parallel-to-previous ray → magenta guide
        AngleSnap,         // cursor is on a 15° / 30° / 45° / etc. ray from the chain anchor → grey guide
        OnLineExtension,   // cursor is on the infinite extension of an existing line → lavender dashed guide
        TangentToCircle,   // cursor lies on the tangent line touching a circle/arc → orange dashed guide
        PerpToRef,         // cursor is on the perpendicular ray through a hover-charged point → cyan guide
    };
    Kind kind;
    glm::vec2 from;    // ghost guide line start (sketch-space)
    glm::vec2 to;      // ghost guide line end (typically the snapped cursor)
    int refId = -1;    // id of the referenced point / line, or -1 if not applicable
};

class SketchTool {
public:
    SketchTool();

    void setSketch(Sketch* sketch);
    void setSolver(SketchSolver* solver);

    void setMode(SketchToolMode mode);
    SketchToolMode getMode() const;

    // Input events (in sketch 2D coordinates). `addToSel` is the modifier state
    // for Select-mode multi-pick (Ctrl held); ignored by other tools.
    void onMouseDown(glm::vec2 pos, bool addToSel = false);
    void onMouseMove(glm::vec2 pos);
    void onMouseUp(glm::vec2 pos);

    // --- Sketch-element selection (Select mode) ---
    const std::set<int>& getSelectedPoints()  const { return m_selectedPoints; }
    const std::set<int>& getSelectedLines()   const { return m_selectedLines; }
    const std::set<int>& getSelectedCircles() const { return m_selectedCircles; }
    const std::set<int>& getSelectedArcs()    const { return m_selectedArcs; }
    void clearElementSelection() {
        m_selectedPoints.clear();
        m_selectedLines.clear();
        m_selectedCircles.clear();
        m_selectedArcs.clear();
    }
    bool hasElementSelection() const {
        return !m_selectedPoints.empty() || !m_selectedLines.empty() ||
               !m_selectedCircles.empty() || !m_selectedArcs.empty();
    }
    // Select every element in the active sketch (used by Ctrl+A / double-click).
    void selectAll();
    // Replace the current selection with the given ids.
    void setSelection(const std::set<int>& pointIds, const std::set<int>& lineIds) {
        m_selectedPoints = pointIds;
        m_selectedLines = lineIds;
        m_selectedCircles.clear();
        m_selectedArcs.clear();
    }
    void onConfirm(); // Enter/double-click to finish
    void onCancel();  // Escape to cancel

    // Type a dimension during placement: completes the current shape using `value` as the
    // primary dimension (line length, circle radius, polygon radius, rectangle half-side)
    // anchored at the first click, in the direction of the current cursor.
    // Returns true if the shape was created.
    bool applyDimension(float value);

    // Anchor point of the current placement (the first click). Used by the dimension input UI.
    glm::vec2 getFirstClick() const { return m_firstClick; }
    glm::vec2 getCurrentPos() const { return m_currentPos; }
    glm::vec2 getSecondClick() const { return m_secondClick; }
    int getClickCount() const { return m_clickCount; }
    // Current side count for the polygon tool. Modifiable from the dimension
    // popup without committing the placement, so the user can preview a new
    // count before clicking.
    int getPolygonSides() const { return m_polygonSides; }
    void setPolygonSides(int n) { m_polygonSides = (n < 3) ? 3 : n; }
    // Text tool settings — the popup edits these; the click consumes them.
    const std::string& getTextString() const { return m_textString; }
    void setTextString(const std::string& s) { m_textString = s; }
    const std::string& getTextFontPath() const { return m_textFontPath; }
    void setTextFontPath(const std::string& p) { m_textFontPath = p; }
    float getTextHeight() const { return m_textHeight; }
    void setTextHeight(float h) { m_textHeight = (h < 0.5f) ? 0.5f : h; }
    // CCW rotation about the click anchor, 90° steps. The app seeds this
    // from the camera when the tool activates so text reads upright in the
    // current view; the popup's rotate buttons adjust from there.
    int getTextAngle() const { return m_textAngle; }
    void setTextAngle(int deg) { m_textAngle = ((deg % 360) + 360) % 360; }
    // Unrotated text extents relative to the anchor (mm), pushed by the app
    // whenever string/font/height change; the viewport draws the placement
    // rectangle from these. Invalid until setTextPreviewBox is called.
    bool hasTextPreviewBox() const { return m_textPrevValid; }
    glm::vec2 getTextPreviewMin() const { return m_textPrevMin; }
    glm::vec2 getTextPreviewMax() const { return m_textPrevMax; }
    void setTextPreviewBox(glm::vec2 mn, glm::vec2 mx) {
        m_textPrevMin = mn; m_textPrevMax = mx; m_textPrevValid = true;
    }
    void clearTextPreviewBox() { m_textPrevValid = false; }
    // SVG placement (shares the Text tool's placement frame: same angle,
    // same cursor preview box, same fromText suppression on the result).
    void setSvgPaths(SvgPaths svg) { m_svgPaths = std::move(svg); }
    const SvgPaths& getSvgPaths() const { return m_svgPaths; }
    float getSvgWidth() const { return m_svgWidth; }
    void setSvgWidth(float w) { m_svgWidth = (w < 0.1f) ? 0.1f : w; }
    // Backspace while the Text/SVG tool is active yanks the whole last
    // stamp — a misplaced 550-line logo is not undoable element-by-element.
    bool hasLastStamp() const { return !m_lastStampIds.empty(); }
    void undoLastStamp();
    // Rectangle's typed-value placement is two-stage: first Enter sets the
    // horizontal side, second Enter the vertical (and commits). Stage 0 =
    // expecting H, 1 = expecting V. Read by the UI to swap the popup label.
    int getRectDimStage() const { return m_rectDimStage; }

    // True while the tool has an in-progress placement (first click made,
    // second pending) — used by the host to give Escape two-step semantics:
    // first Esc cancels just the in-progress shape, second Esc exits the
    // sketch mode entirely.
    bool isPlacing() const { return m_isPlacing; }

    // Grid step (in sketch-plane mm). Used for both visual grid and snap-to-line.
    // 0 disables grid snap entirely.
    void setGridStep(float step) { m_gridStep = step; }
    float getGridStep() const { return m_gridStep; }
    // Mirrors the toolbar "Snap to grid" checkbox. When on (default), placed
    // points always round to the nearest grid increment; when off, only
    // inferences snap and the cursor lands at sub-grid precision.
    void setSnapToGridEnabled(bool b) { m_snapToGridEnabled = b; }

    // How much drawing-time inference assistance to apply. Cycled live from the
    // sketch toolbar (no longer a persisted setting). Full = every guide incl.
    // hover-charged references; Reduced = lock-onto-existing-geometry snaps only
    // (endpoint / midpoint / on-line / axis-from-point), no directional ray
    // guides or dwell-charging; Off = grid + endpoint only.
    enum class InferenceLevel { Full, Reduced, Off };
    void setInferenceLevel(InferenceLevel lvl) { m_inferenceLevel = lvl; }
    InferenceLevel getInferenceLevel() const { return m_inferenceLevel; }

    // Hover-to-charge references (Full level only). Call once per frame during
    // sketch placement with the current time and the sketch-space cursor. After
    // the cursor dwells ~0.3 s on an existing reference (sketch point, sketch
    // line midpoint, host-face vertex, or host-face edge midpoint) it becomes
    // "charged" and projects axis + perpendicular guides anchored AT that
    // position — until a different one charges or the placement ends. The
    // renderer reads hasChargedRef() / getChargedPos() to draw the cyan ring.
    struct ChargedRef {
        // None = nothing charged; the other kinds carry an anchor position the
        // snap engine projects V / H / perpendicular guides from. SketchLineMid
        // and FaceLineMid each have exactly one PerpToRef direction (perp to
        // the segment); the Point kinds scan touching lines / face edges.
        enum class Kind { None, SketchPoint, SketchLineMid, FacePoint, FaceLineMid };
        Kind kind = Kind::None;
        glm::vec2 pos{0.0f};
        // Sketch point or line id for the Sketch* kinds; -1 for Face*.
        int sourceId = -1;
    };
    void updateHoverCharge(double tNow, glm::vec2 cursorSketchPos);
    bool hasChargedRef() const { return m_charged.kind != ChargedRef::Kind::None; }
    glm::vec2 getChargedPos() const { return m_charged.pos; }
    // Legacy accessor used elsewhere — returns the sketch-point id only when
    // the charged ref happens to BE a sketch point; -1 for the new kinds.
    int  getChargedRefPoint() const {
        return m_charged.kind == ChargedRef::Kind::SketchPoint ? m_charged.sourceId : -1;
    }

    // Current state for rendering preview
    bool hasPreview() const;
    glm::vec2 getPreviewStart() const;
    glm::vec2 getPreviewEnd() const;
    SketchToolMode getPreviewType() const;

    // Trim hover: densified 2D points outlining the segment that would be
    // removed on the next click. Empty when nothing is hovered in Trim mode.
    const std::vector<glm::vec2>& getTrimHoverPoints() const { return m_trimHoverPoints; }

    // The set of inferences active at the most recent snap. The renderer reads
    // this each frame to draw ghost guide lines. Cleared whenever the cursor
    // doesn't align with anything.
    const std::vector<InferenceGuide>& getActiveInferences() const { return m_activeInferences; }

    // Is the tool actively placing something?
    bool isActive() const;

private:
    SketchToolMode m_mode = SketchToolMode::None;
    Sketch* m_sketch = nullptr;
    SketchSolver* m_solver = nullptr;
    InferenceLevel m_inferenceLevel = InferenceLevel::Full;
    // Hover-charge state (see updateHoverCharge). m_charged is the active
    // reference (Kind::None when nothing's charged); the m_hover* fields
    // track the in-progress dwell on a candidate before it commits.
    ChargedRef m_charged;
    ChargedRef m_hoverCandidate;
    double     m_hoverProbeStart = 0.0;
    glm::vec2  m_hoverProbePos{0.0f};

    // State for multi-click tools
    bool m_isPlacing = false;
    int m_clickCount = 0;
    glm::vec2 m_firstClick{0};
    glm::vec2 m_secondClick{0};
    glm::vec2 m_currentPos{0};

    // For line chaining. m_lastPointId is the running tail of the chain that
    // each new segment extends from. m_chainStartPointId remembers where the
    // chain *began*, so when the user clicks back onto it we can auto-close
    // the loop (commit the final segment and end placement).
    int m_lastPointId = -1;
    int m_chainStartPointId = -1;
    // Did the first click of the current line chain add a brand-new point
    // (vs reusing an existing one)? If yes and the chain is cancelled before
    // any segment is committed, we delete the orphan to avoid leaving a
    // stray vertex with no lines attached.
    bool m_chainStartPointCreated = false;

    // Snap to grid/points
    glm::vec2 snap(glm::vec2 pos) const;

    // Find an existing point near the given position (returns -1 if none)
    int findCoincidentPoint(glm::vec2 pos, int excludeId = -1) const;

    void handleLineTool(glm::vec2 pos);
    void handleCircleTool(glm::vec2 pos);
    void handleRectangleTool(glm::vec2 pos);
    void handleArcTool(glm::vec2 pos);
    void handleSelectTool(glm::vec2 pos);
    void handleSplineTool(glm::vec2 pos);
    void handlePolygonTool(glm::vec2 pos);
    void handleTextTool(glm::vec2 pos);
    void handleSvgTool(glm::vec2 pos);
    // Collapse a directional-inference result onto the axis (+ grid) when
    // it's within a few degrees of horizontal/vertical — crooked-line guard.
    glm::vec2 rectifyNearAxis(glm::vec2 target) const;
    void handleTrimTool(glm::vec2 pos);
    void computeTrimHover(glm::vec2 pos); // updates m_trimHoverPoints (no mutation)

    // Select/drag state
    int m_dragPointId = -1;
    bool m_isDragging = false;
    bool m_lastDownAddedToSel = false; // Ctrl state for the current click
    std::set<int> m_selectedPoints;
    std::set<int> m_selectedLines;
    std::set<int> m_selectedCircles;
    std::set<int> m_selectedArcs;

    std::vector<int> m_splinePoints; // temp storage during spline creation

public:
    // Control points of the spline currently being placed (live preview).
    const std::vector<int>& splinePointsInProgress() const {
        return m_splinePoints;
    }

    // Backspace during spline placement: drop the last control point
    // (removing it from the sketch too unless something else references
    // it — e.g. the user snapped onto an existing vertex).
    void removeLastSplinePoint();

private:
    int m_polygonSides = 6; // default hexagon

    // Text tool settings (see TextSketchOp.h for the generator)
    std::string m_textString = "TEXT";
    std::string m_textFontPath; // resolved by the app at tool activation
    float m_textHeight = 8.0f;  // capital height, mm
    int   m_textAngle = 0;      // CCW degrees, 90° steps
    bool  m_textPrevValid = false;
    glm::vec2 m_textPrevMin{0.0f};
    glm::vec2 m_textPrevMax{0.0f};

    // SVG placement state (see SvgImport.h)
    SvgPaths m_svgPaths;
    float m_svgWidth = 50.0f; // target artwork width, mm

    // Element ids of the most recent Text/SVG stamp (lines first, then
    // points, so removal order never orphans references).
    std::vector<int> m_lastStampIds;
    void recordStamp(size_t pointsBefore, size_t linesBefore);

    // Rectangle's typed-value placement is two-stage: first Enter sets the
    // horizontal side, second Enter sets the vertical side and commits.
    // m_rectDimStage tracks where we are (0 = expecting H, 1 = expecting V);
    // m_rectDimH stores the locked-in horizontal value between stages.
    int   m_rectDimStage = 0;
    float m_rectDimH = 0.0f;

    float m_gridStep = 1.0f; // default 1 mm grid
    bool  m_snapToGridEnabled = true; // toolbar checkbox, see setSnapToGridEnabled

    // Updated each frame in Trim mode so the renderer can outline the segment
    // that would be deleted on click.
    std::vector<glm::vec2> m_trimHoverPoints;

    // Direction (unit vector) of the previous segment in the current chain.
    // Used so "perpendicular to previous" and "parallel to previous" inferences
    // have something to anchor to while the user draws the next vertex.
    // Reset on chain-break (onConfirm/onCancel/mode-switch).
    glm::vec2 m_prevLineDir{0.0f, 0.0f};
    bool m_hasPrevLineDir = false;

    // Populated as a side-effect of snap(); read by the viewport overlay to
    // draw ghost guide lines. Mutable so snap() can stay const.
    mutable std::vector<InferenceGuide> m_activeInferences;

    // Point ids the next snap() call should skip when looking for endpoint /
    // axis-from-point / on-line candidates. Populated during a drag to
    // exclude the dragged points themselves (otherwise the cursor would snap
    // to its own starting position and the drag would feel sticky-broken).
    // Cleared in onMouseUp.
    std::set<int> m_snapExcludePoints;
};

} // namespace materializr
