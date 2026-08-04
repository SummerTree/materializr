#pragma once
#include "IopViewport.h"
#include <functional>
#include <memory>
#include <TopoDS_Shape.hxx>

class Document;
class History;
class SelectionManager;
class Operation;

namespace materializr {

// Everything an interactive-op controller needs from the app, without
// seeing the Application god-class. Built on demand by Application.
struct IopContext {
    Document& doc;
    History& history;
    SelectionManager& selection;
    std::function<void()> markMeshesDirty;
    // Long-op progress: renders a progress frame, returns true if the user
    // cancelled. Passed to the op via setProgressReporter on commit.
    std::function<bool(float, const char*)> progress;
    // Defer a heavy task to run BETWEEN frames (so the progress reporter can
    // render its own frames without nesting ImGui frames).
    std::function<void(std::function<void()>)> deferHeavy;
    // The layout hosts Confirm/Cancel outside the panel (im-touch corner
    // FABs) — renderPanel skips its own buttons; Enter/Esc still work.
    bool cornerCommitUi = false;
};

// Base for "popup with live preview" modeling operations (Shell, Taper,
// Scale Face, …). Before this existed, every such feature deposited ~400
// lines across five Application files: a state block, four lifecycle
// methods, a panel function, a ToolAction case, an Esc-chain entry, and
// membership in TWO hand-maintained gizmo-suppression lists (forgetting a
// list entry was a recurring bug class). A controller subclass implements
// four small hooks; the lifecycle, the panel scaffold, and the
// suppression/Esc registration come from here.
//
// Lifecycle contract (identical to the pattern the hand-written ops used):
//   begin   — snapshot the target body, capture params from the selection
//             (onBegin), run the first preview.
//   update  — restore the snapshot, build a fresh Operation from current
//             params, execute it as the live preview; previewOk() records
//             whether it landed.
//   commit  — restore the snapshot, build the op once more, push it onto
//             History (which re-executes it cleanly), clear selection.
//   cancel  — restore the snapshot, drop all state.
class InteractiveOpController {
public:
    virtual ~InteractiveOpController() = default;

    bool begin(const IopContext& ctx);
    void update(const IopContext& ctx);
    void commit(const IopContext& ctx);
    void cancel(const IopContext& ctx);

    // Draws nothing when inactive. The scaffold handles window placement,
    // title, Confirm/Cancel buttons, and Enter/Esc keys.
    void renderPanel(const IopContext& ctx);

    bool active() const { return m_active; }
    bool previewOk() const { return m_previewOk; }

    // ─── Viewport handles (optional) ─────────────────────────────────────────
    // Ops with an on-screen handle own their hit-test and drag instead of
    // Application_Viewport doing it for them. Default: no handle, so the five
    // panel-only controllers are unaffected.
    virtual bool wantsViewportInput() const { return false; }
    // One frame of pointer input while this op is active and the camera isn't
    // being dragged. Call setDraggingHandle() when a handle latches so the
    // camera and the picker know to stand off.
    virtual void onViewportInput(const IopViewport& vp, const IopContext& ctx) {
        (void)vp; (void)ctx;
    }
    // Draw the handles. Called with the foreground draw list already selected.
    virtual void drawOverlay(const IopOverlay& ov) const { (void)ov; }

    // True while a handle is latched. The viewport suppresses camera orbit and
    // face picking on this — previously read off ScaleFace's dragAxis()
    // directly, which only worked because exactly one controller had a gizmo.
    bool draggingHandle() const { return m_draggingHandle; }

protected:
    virtual const char* title() const = 0;
    // Capture the selection into params. Return the target body id, or -1
    // to refuse to start (nothing usable selected).
    virtual int onBegin(const IopContext& ctx) = 0;
    // Build a fresh Operation from the current parameters (used for both
    // the live preview and the final commit).
    virtual std::unique_ptr<Operation> buildOp(const IopContext& ctx) = 0;
    // Parameter widgets (sliders, radios, status lines). Set `changed`
    // when a value moved so the scaffold re-runs the preview. Call
    // requestCommit() to commit from inside the body (e.g. Enter in a
    // text field).
    virtual void panelBody(const IopContext& ctx, bool& changed) = 0;
    virtual void onCleanup() {}
    virtual float panelWidth() const { return 260.0f; }
    // Override to suppress the per-change live preview when recomputing it would
    // freeze the UI (e.g. projecting a sketch with hundreds of regions). Commit
    // still builds + runs the op once. Default: always preview.
    virtual bool wantsLivePreview(const IopContext&) const { return true; }
    // Should commit() hand the op to deferHeavy (runs BETWEEN frames behind a
    // cancellable progress window) instead of pushing it inline?
    //
    // Defaults to "whenever the live preview is off", which is right for an op
    // that skipped previewing BECAUSE it is slow (Project Sketch). It is wrong
    // for one that skipped previewing for a different reason: Resize
    // Cylindrical turns the preview off on a THREADED body — the ring boolean
    // would re-run against the thread's helicoid faces on every keystroke —
    // but its commit is cheap and must stay inline, because History reflows it
    // beneath the Thread step and re-cuts the thread around that push.
    virtual bool wantsDeferredCommit(const IopContext& ctx) const {
        return !wantsLivePreview(ctx);
    }

    void requestCommit() { m_commitRequested = true; }
    void setDraggingHandle(bool d) { m_draggingHandle = d; }

    int bodyId() const { return m_bodyId; }
    const TopoDS_Shape& snapshot() const { return m_snapshot; }

private:
    void cleanup();

    bool m_active = false;
    bool m_previewOk = false;
    bool m_commitRequested = false;
    bool m_draggingHandle = false;
    int m_bodyId = -1;
    TopoDS_Shape m_snapshot;
};

} // namespace materializr
