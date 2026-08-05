#include "ui/UiTheme.h"
#include "InteractiveOpController.h"
#include "touch_mode.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../core/Operation.h"
#include <imgui.h>
#include <cstdio>

namespace materializr {

bool InteractiveOpController::begin(const IopContext& ctx) {
    int body = onBegin(ctx);
    if (body == -1) return false;   // refused
    if (previewModel() == PreviewModel::LiveOp) {
        // Nothing to snapshot: the live instance's own undo() is the restore
        // path, and the target may not exist yet (a free-space extrude mints
        // its body). kNoTargetBody means "started, no body of my own".
        m_bodyId = (body == kNoTargetBody) ? -1 : body;
        m_active = true;
        m_commitRequested = false;
        m_liveOp.reset();
        m_liveApplied = false;
        update(ctx);
        return true;
    }
    if (body < 0) return false;
    try {
        m_snapshot = ctx.doc.getBody(body);
    } catch (...) { return false; }
    if (m_snapshot.IsNull()) return false;
    m_bodyId = body;
    m_active = true;
    m_commitRequested = false;
    update(ctx);
    return true;
}

void InteractiveOpController::update(const IopContext& ctx) {
    if (!m_active) return;
    if (previewModel() == PreviewModel::LiveOp) { updateLive(ctx); return; }
    if (m_bodyId < 0) return;
    if (!wantsLivePreview(ctx)) {
        // Live preview suppressed (recomputing it per change would freeze the
        // UI). Keep the snapshot shown and mark preview "ok" so Confirm still
        // computes + pushes the op once. pushOperation re-runs execute() and
        // refuses on failure, so a heavy commit that fails just does nothing.
        ctx.doc.updateBody(m_bodyId, m_snapshot);
        ctx.markMeshesDirty();
        m_previewOk = true;
        return;
    }
    // Reset to the snapshot, then run a fresh op against it so the live
    // preview tracks the current values exactly without compounding edits.
    ctx.doc.updateBody(m_bodyId, m_snapshot);
    ctx.markMeshesDirty();
    m_previewOk = false;
    try {
        std::unique_ptr<Operation> op = buildOp(ctx);
        if (op && op->execute(ctx.doc)) {
            ctx.markMeshesDirty();
            m_previewOk = true;
        } else {
            ctx.doc.updateBody(m_bodyId, m_snapshot);
        }
    } catch (...) {
        ctx.doc.updateBody(m_bodyId, m_snapshot);
    }
}

// LiveOp preview: ONE instance, toggled against the document. Undo whatever
// it currently has applied, push the new values in, run it again. History is
// not involved until commit — and because it is the same instance every
// frame, any body it creates keeps the same id (the whole point).
void InteractiveOpController::updateLive(const IopContext& ctx) {
    if (m_liveApplied && m_liveOp) {
        try { m_liveOp->undo(ctx.doc); } catch (...) {}
        m_liveApplied = false;
    }
    if (!m_liveOp) {
        try { m_liveOp = buildOp(ctx); } catch (...) { m_liveOp.reset(); }
    }
    m_previewOk = false;
    if (m_liveOp && wantsLivePreview(ctx)) {
        try {
            if (syncLiveOp(*m_liveOp) && m_liveOp->execute(ctx.doc)) {
                m_liveApplied = true;
                m_previewOk = true;
            }
        } catch (...) { m_liveApplied = false; }
    }
    ctx.markMeshesDirty();
}

void InteractiveOpController::commit(const IopContext& ctx) {
    if (!m_active) return;
    if (previewModel() == PreviewModel::LiveOp) {
        // A different op to record? Undo the preview and push it properly,
        // so History executes it against the un-previewed document.
        std::unique_ptr<Operation> alt;
        try { alt = buildCommitOp(ctx); } catch (...) {}
        if (alt) {
            if (m_liveApplied && m_liveOp) {
                try { m_liveOp->undo(ctx.doc); } catch (...) {}
                m_liveApplied = false;
            }
            m_liveOp.reset();
            ctx.history.pushOperation(std::move(alt), ctx.doc);
        } else if (m_liveApplied && m_liveOp) {
            // The preview IS the result — record it without re-running it.
            ctx.history.pushExecuted(std::move(m_liveOp));
        }
        // Anything else (nothing applied — a zero-distance gesture) records
        // nothing, which is right: the document is already untouched.
        ctx.selection.clear();
        ctx.markMeshesDirty();
        cleanup();
        return;
    }
    // Roll back the preview; History::pushOperation re-runs the op cleanly
    // against the snapshot.
    ctx.doc.updateBody(m_bodyId, m_snapshot);
    if (!m_previewOk) {
        cancel(ctx);
        return;
    }
    std::unique_ptr<Operation> op = buildOp(ctx);
    if (op) {
        if (wantsDeferredCommit(ctx) && ctx.progress && ctx.deferHeavy) {
            // Heavy op (live preview was off): defer it to run BETWEEN frames
            // with a progress reporter so the window stays alive and the user
            // can cancel. A cancel makes execute() fail → pushOperation refuses
            // → body stays at the snapshot (clean no-op).
            op->setProgressReporter(ctx.progress);
            History* hist = &ctx.history;
            Document* doc = &ctx.doc;
            auto markDirty = ctx.markMeshesDirty;
            Operation* raw = op.release();
            ctx.deferHeavy([hist, doc, raw, markDirty]() {
                std::unique_ptr<Operation> o(raw);
                hist->pushOperation(std::move(o), *doc);
                if (markDirty) markDirty();
            });
        } else {
            ctx.history.pushOperation(std::move(op), ctx.doc);
        }
    }
    ctx.selection.clear();
    ctx.markMeshesDirty();
    cleanup();
}

void InteractiveOpController::cancel(const IopContext& ctx) {
    if (previewModel() == PreviewModel::LiveOp) {
        if (m_liveApplied && m_liveOp) {
            try { m_liveOp->undo(ctx.doc); } catch (...) {}
        }
    } else if (m_bodyId >= 0 && !m_snapshot.IsNull()) {
        ctx.doc.updateBody(m_bodyId, m_snapshot);
    }
    ctx.markMeshesDirty();
    cleanup();
}

void InteractiveOpController::cleanup() {
    m_active = false;
    m_commitRequested = false;
    m_previewOk = false;
    // A latched handle must not survive the op — the viewport reads this to
    // suppress camera orbit and picking, so leaving it set would wedge both.
    m_draggingHandle = false;
    m_bodyId = -1;
    m_snapshot.Nullify();
    m_liveOp.reset();
    m_liveApplied = false;
    onCleanup();
}

void InteractiveOpController::renderPanel(const IopContext& ctx) {
    if (!m_active) return;

    // Seed the panel's RIGHT edge a fixed margin inside the viewport (pivot 1,0)
    // and let it grow leftward. The panel is AlwaysAutoResize and, on touch, its
    // padded content is wider than panelWidth() — anchoring the LEFT edge by a
    // fixed offset (winWidth - w - 20) let that extra width run off the right
    // edge on the tablet. Right-anchoring keeps it on-screen at any scale.
    //
    // ImGuiCond_Appearing + no NoMove flag: same as the Pattern / Edit-Diameter
    // popups — seed the position on first appearance, then let the user drag the
    // panel somewhere convenient (it otherwise landed over the top-left menu in
    // im-touch with no way to move it — issue #29). Dragging the panel body
    // (there's no title bar) moves it.
    const float w = panelWidth();
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x +
                                       ImGui::GetWindowWidth() - 20.0f,
                                   ImGui::GetWindowPos().y + 50.0f),
                            ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0), ImGuiCond_Appearing);
    char id[64];
    std::snprintf(id, sizeof(id), "##iop_%s", title());
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextColored(materializr::accentText(), "%s", title());
    ImGui::Separator();

    bool changed = false;
    panelBody(ctx, changed);
    if (changed) update(ctx);

    ImGui::Spacing();
    bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    bool esc   = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    bool doCommit = m_commitRequested || enter;
    bool doCancel = esc;
    if (!ctx.cornerCommitUi) {
        doCommit = doCommit ||
                   ImGui::Button(materializr::btnConfirm(), ImVec2(120, 0));
        if (!doCommit) {
            ImGui::SameLine();
            doCancel = doCancel ||
                       ImGui::Button(materializr::btnCancel(), ImVec2(120, 0));
        }
    }
    doCancel = doCancel && !doCommit;
    ImGui::End();

    if (doCommit) commit(ctx);
    else if (doCancel) cancel(ctx);
}

} // namespace materializr
