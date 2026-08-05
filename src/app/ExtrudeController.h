#pragma once
#include "InteractiveOpController.h"
#include "../modeling/ExtrudeOp.h"
#include <TopoDS_Shape.hxx>
#include <glm/glm.hpp>

namespace materializr {

// Interactive Extrude: sweep a sketch profile (or a flat body face) along its
// normal, with a draggable arrow and a distance well.
//
// FIRST user of the LiveOp preview model, and the reason that model got
// built. The hand-written version pushed a real ExtrudeOp onto History and
// undid it again on EVERY preview frame — the same churn Push/Pull was
// rescued from earlier, still carrying its symptoms: the created body's id
// changed each frame, and an outside history touch mid-gesture left the
// bookkeeping desynced (the old code had an explicit "preview op no longer on
// top of history — resyncing without undo" bail-out for exactly that). Keeping
// ONE instance and toggling it undo/execute fixes both: ExtrudeOp re-uses
// m_createdBodyId through addOrPutBody, so the preview body keeps its id, and
// History sees nothing at all until commit.
//
// The preview is ALWAYS a NewBody tool volume, even in Subtract mode, so the
// user watches the shape being swept; the real boolean runs once at commit
// through buildCommitOp().
class ExtrudeController : public InteractiveOpController {
public:
    // Entry point (this op is handed its profile rather than reading the
    // selection, so it doesn't fit onBegin's capture-from-selection shape).
    // Returns false when it refused — a curved face has no single normal.
    bool beginExtrude(const IopContext& ctx, const TopoDS_Shape& profile,
                      ExtrudeMode mode, int targetBody, int sourceSketchId);

    // The arrow's frame, for the viewport gizmo + drag (still in Application).
    const glm::vec3& origin() const { return m_origin; }
    const glm::vec3& normal() const { return m_normal; }
    float distance() const { return m_distance; }
    // Transitional: the panel's ImGui widgets and the arrow drag bind
    // straight to this float. Goes away when the panel + drag move in.
    float& distanceRef() { return m_distance; }
    char* inputBuf() { return m_inputBuf; }
    int inputBufSize() const { return static_cast<int>(sizeof(m_inputBuf)); }
    bool takeInputFocus() { bool f = m_inputFocus; m_inputFocus = false; return f; }
    ExtrudeMode mode() const { return m_mode; }
    int previewBodyId() const;

    // Re-run the preview at the current distance. applySnap=false keeps a
    // typed value exact (the grid step would round it under the user).
    void updateExtrude(const IopContext& ctx, bool applySnap = true);

protected:
    const char* title() const override { return "Extrude"; }
    PreviewModel previewModel() const override { return PreviewModel::LiveOp; }
    int onBegin(const IopContext& ctx) override;
    std::unique_ptr<Operation> buildOp(const IopContext& ctx) override;
    bool syncLiveOp(Operation& op) override;
    std::unique_ptr<Operation> buildCommitOp(const IopContext& ctx) override;
    void panelBody(const IopContext& ctx, bool& changed) override;
    // The panel lives in Application_Viewport (its distance well anchors to
    // the viewport window, like Move Face's). Scaffold stays silent.
    void renderPanel(const IopContext&) override {}
    void onCleanup() override;

private:
    // Signed distance for the op: the profile normal points OUT of the body,
    // so a Subtract's tool has to travel the other way.
    double opDistance() const;

    TopoDS_Shape m_profile;
    ExtrudeMode m_mode = ExtrudeMode::NewBody;
    int m_targetBody = -1;
    int m_sketchId = -1;
    glm::vec3 m_normal{0, 0, 1};
    glm::vec3 m_origin{0};
    float m_distance = 5.0f;
    char m_inputBuf[32] = "5.0";
    bool m_inputFocus = true;
};

} // namespace materializr
