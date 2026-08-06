#pragma once
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <glm/glm.hpp>
#include <memory>
#include <utility>
#include <vector>

class PushPullOp;

namespace materializr {

// Everything the interactive Push/Pull gesture holds while it is running.
//
// Was 16 loose members on Application. Grouped here first (the same first step
// Move Face took) so the state has one owner before the lifecycle moves onto
// the InteractiveOpController base — where the live-preview engine below is
// already modelled as PreviewModel::LiveOp, since Push/Pull is the op that
// engine was originally hand-written for.
struct PushPullState {
    // One entry per region/face the gesture will operate on.
    struct Target {
        int sketchId;
        int regionIndex;
        int sourceBodyId;   // -1 for floating (NewBody)
        TopoDS_Face profile;
    };

    bool active = false;

    // Live preview op (snapshot/restore engine): undone + re-executed
    // directly against the document each preview frame, appended to history
    // exactly once via pushExecuted() at commit. History is untouched
    // during the preview — see updatePushPull.
    std::unique_ptr<PushPullOp> liveOp;
    bool previewApplied = false;

    bool symmetric = false;   // panel checkbox (plane-sketch targets)
    float distance = 5.0f;
    // Unsnapped drag accumulator. The grid snap in updatePushPull mutates
    // `distance` itself (so the readouts show the snapped value), which would
    // erase sub-step drag motion every frame — a slow drag accumulated
    // nothing, then a fast flick jumped a whole step. The drag adds into THIS
    // instead, and `distance` is derived + snapped from it. Typing/sliding a
    // value re-bases the accumulator.
    float distanceRaw = 0.0f;
    char inputBuf[32] = "5.0";
    bool inputFocus = true;

    // Face arrow: drag along this normal to drive the distance (set from the
    // first face target). `hasArrow` is false for sketch-region-only push/pull.
    glm::vec3 origin{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    bool hasArrow = false;

    // Trackpad-mode sticky drag (orbitButton == panButton == LMB): a single
    // click in the viewport while the arrow is up enters this state, mouse
    // moves then drive the distance frame-by-frame without a button held,
    // and a second click exits. Same shape as the Sketch Circle tool's
    // click-move-click pattern — gives users a way to "drag" the arrow
    // when their primary click is already bound to orbit. While true,
    // gizmoOwnsDrag suppresses orbit so the cursor isn't fighting the
    // camera. (Steve: "let click then click act like click and hold".)
    bool sticky = false;

    // Dense-body drag protection: when any target body has >250 faces (a
    // threaded rod), the per-frame preview shows a tinted GHOST of the tool
    // volume instead of running the real boolean (which would also trigger
    // the thread reflow) every frame. The real op runs once, on commit.
    bool heavyPreview = false;

    std::vector<Target> targets;
    std::vector<int> previewBodyIds;
    // For undoing previews
    std::vector<std::pair<int, TopoDS_Shape>> previousBodies;
};

} // namespace materializr
