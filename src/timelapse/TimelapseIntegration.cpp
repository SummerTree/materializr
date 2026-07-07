// Timelapse ↔ Application glue: the per-frame capture gate, the offscreen
// canonical-render capture, and the export kickoff. Lives with the rest of
// the timelapse code (src/timelapse/) rather than in the Application core;
// these are Application member functions in a separate TU, the same split
// as Application_Dialogs / Application_Viewport.
#include "gl_common.h"

#include "app/Application.h"
#include "core/Document.h"
#include "core/History.h"
#include "io/FileDialogs.h"
#include "modeling/Sketch.h"
#include "timelapse/Timelapse.h"
#include "timelapse/VideoEncoder.h"
#include "viewport/BackgroundRenderer.h"
#include "viewport/EdgeRenderer.h"
#include "viewport/ShapeRenderer.h"
#include "viewport/SketchRenderer.h"
#include "viewport/Viewport.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>

namespace materializr {

void Application::updateTimelapse() {
    if (!m_timelapse) return;

    // Follow the current document. A revision jump alongside the path change
    // means a project load (fresh history) — don't carry frames; a plain path
    // change with an unchanged history is Save As adopting the unsaved
    // session's recording.
    const unsigned rev = m_history ? m_history->revision() : 0;
    if (!m_tlBound || m_currentProjectPath != m_tlBoundRef) {
        const bool carry = m_tlBound && rev == m_tlLastRevision;
        m_timelapse->bindProject(m_currentProjectPath, carry);
        m_tlBoundRef = m_currentProjectPath;
        m_tlBound = true;
        m_tlLastRevision = rev;
    } else if (m_timelapse->enabled() && m_timelapse->videoMode() &&
               m_viewport) {
        // Collect any finished async readback first (no-op when idle) so a
        // burst's last frame reaches the encoder promptly.
        m_timelapse->pumpVideoReads();
        // Video mode: ~10 Hz ACTION sampling. A frame is worth storing when
        // the camera moved, the history changed, or a drag preview is live —
        // idle time appends nothing, so the recording is pure action. One
        // trailing frame lands after action settles so every burst ends on
        // its final state.
        const Camera& c = m_viewport->getCamera();
        const glm::vec3 pos = c.getPosition(), tgt = c.getTarget();
        const float viewDist = std::max(glm::length(pos - tgt), 1e-3f);
        const bool camMoved = (glm::length(pos - m_tlLastCamPos) +
                               glm::length(tgt - m_tlLastCamTgt)) /
                                  viewDist > 0.002f;
        const bool dragging = anyInteractivePreviewActive();
        const bool commitPending = rev != m_tlLastRevision && !m_meshesDirty;
        const bool action = camMoved || dragging || commitPending;
        const double now = ImGui::GetTime();
        const double interval = 1.0 / double(std::max(1, m_timelapseCaptureHz));
        if ((action || m_tlTrailing) && now - m_tlLastCapture >= interval) {
            m_tlLastRevision = rev;
            m_tlLastCapture = now;
            m_tlLastCamPos = pos;
            m_tlLastCamTgt = tgt;
            m_tlTrailing = action; // capture once more after the burst ends
            renderTimelapseFrame();
        }
    } else if (m_timelapse->enabled()) {
        // Pixel-store fallback (Windows/Android): a settled history commit or
        // an in-progress drag, paced to ~4 fps; camera moves get synthetic
        // glide fillers inside renderTimelapseFrame.
        const bool commitPending = rev != m_tlLastRevision && !m_meshesDirty;
        const bool dragging = anyInteractivePreviewActive();
        const double now = ImGui::GetTime();
        if ((commitPending || dragging) && now - m_tlLastCapture >= 0.25) {
            if (commitPending) m_tlLastRevision = rev;
            m_tlLastCapture = now;
            renderTimelapseFrame();
        }
    }

    // Finished background encode → hand the ready GIF to the export dialog
    // (which copies it to the chosen destination / share target).
    if (m_tlExportFuture.valid() &&
        m_tlExportFuture.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        const std::string err = m_tlExportFuture.get();
        if (!err.empty()) {
            showToast("Timelapse export failed: " + err);
        } else {
            const std::string tmp = m_tlExportTmp;
            const bool mp4 = tmp.size() > 4 &&
                             tmp.compare(tmp.size() - 4, 4, ".mp4") == 0;
            FileDialogs::exportFile(
                "Save Timelapse", mp4 ? "timelapse.mp4" : "timelapse.gif",
                mp4 ? "video/mp4" : "image/gif",
                {{mp4 ? "MP4 video" : "GIF animation", mp4 ? "*.mp4" : "*.gif"}},
                [this, tmp](const std::string& dest) -> bool {
                    std::error_code ec;
                    std::filesystem::copy_file(
                        tmp, dest,
                        std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) showToast("Timelapse saved.");
                    return !ec;
                });
        }
    }
}

void Application::exportTimelapse(int condenseSeconds, bool asMp4) {
    if (!m_timelapse) return;
    if (m_timelapse->frameCount() < 2) {
        showToast("No timelapse yet — it records as you model.");
        return;
    }
    if (m_tlExportFuture.valid()) {
        showToast("A timelapse export is already running.");
        return;
    }
    std::error_code ec;

    if (m_timelapse->videoMode()) {
        // Chunked recording: flush the readback ring, finalize the open
        // segment, then assemble on a worker — a lossless concat for full
        // length, a retimed re-encode for the condensed cut.
        m_timelapse->pumpVideoReads();
        m_timelapse->closeSegment();
        const auto segs = m_timelapse->segmentPaths();
        if (segs.empty()) {
            showToast("No timelapse yet — it records as you model.");
            return;
        }
        const int total = m_timelapse->frameCount();
        m_tlExportTmp = (std::filesystem::temp_directory_path(ec) /
                         "materializr-timelapse.mp4").string();
        showToast("Assembling timelapse\xE2\x80\xA6");
        m_tlExportFuture = std::async(
            std::launch::async,
            [segs, out = m_tlExportTmp, total, condenseSeconds]() -> std::string {
                std::string err;
                if (VideoEncoder::concatSegments(segs, out, total,
                                                 condenseSeconds, &err))
                    return std::string();
                return err.empty() ? std::string("unknown error") : err;
            });
        return;
    }

    // Pixel-store fallback: encode GIF (or ffmpeg MP4) from the .mzf frames.
    m_tlExportTmp = (std::filesystem::temp_directory_path(ec) /
                     (asMp4 ? "materializr-timelapse.mp4"
                            : "materializr-timelapse.gif")).string();
    showToast("Encoding timelapse\xE2\x80\xA6");
    // Snapshot on this thread (captures mutate the list), encode on a worker.
    m_tlExportFuture = std::async(
        std::launch::async,
        [dir = m_timelapse->frameDirPath(), names = m_timelapse->frameSnapshot(),
         out = m_tlExportTmp, condenseSeconds, asMp4]() -> std::string {
            std::string err;
            const bool ok =
                asMp4 ? TimelapseRecorder::encodeMp4(dir, names, out,
                                                     condenseSeconds, &err)
                      : TimelapseRecorder::encodeGif(dir, names, out,
                                                     condenseSeconds, &err);
            if (ok) return std::string();
            return err.empty() ? std::string("unknown error") : err;
        });
}

// ── Timelapse capture ────────────────────────────────────────────────────────
// Renders the document from the USER'S live camera into a private 1080p MSAA
// FBO and stores the result as a timelapse frame. Runs AFTER the main
// viewport pass (updateTimelapse), so meshes are tessellated and any
// in-progress drag preview/ghost lives in m_shapeRenderer — drags capture as
// motion. Draws bodies + edges + sketches only: no grid, gizmos, or selection
// chrome — the replay shows what the user framed, minus the UI.
void Application::renderTimelapseFrame() {
    if (!m_timelapse || !m_shapeRenderer || !m_document || !m_viewport) return;
    constexpr int kW = 1920, kH = 1080;
    const int kSamples = m_timelapseMsaa; // Settings > Timelapse (0 = off)

    // Empty documents aren't worth a frame.
    bool haveContent = m_inSketchMode && m_activeSketch;
    if (!haveContent)
        for (int id : m_document->getAllBodyIds())
            if (m_document->isBodyVisible(id)) { haveContent = true; break; }
    if (!haveContent)
        for (int sid : m_document->getAllSketchIds())
            if (m_document->isSketchVisible(sid)) { haveContent = true; break; }
    if (!haveContent) return;

    // Lazily build the capture targets: a multisampled render FBO (colour +
    // depth renderbuffers) resolved into a plain texture FBO — the same
    // pattern as Viewport's own framebuffer. Rebuilt when the MSAA setting
    // changes (0 samples = ordinary single-sample storage, same code path).
    if (m_tlFbo != 0 && m_tlFboSamples != kSamples) {
        glDeleteFramebuffers(1, &m_tlFbo);
        glDeleteFramebuffers(1, &m_tlFboMs);
        glDeleteTextures(1, &m_tlColor);
        glDeleteRenderbuffers(1, &m_tlColorMs);
        glDeleteRenderbuffers(1, &m_tlDepth);
        m_tlFbo = m_tlColor = m_tlFboMs = m_tlColorMs = m_tlDepth = 0;
    }
    if (m_tlFbo == 0) {
        GLint prevFbo0 = 0, prevTex = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

        glGenFramebuffers(1, &m_tlFbo);
        glGenTextures(1, &m_tlColor);
        glBindTexture(GL_TEXTURE_2D, m_tlColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
        glBindFramebuffer(GL_FRAMEBUFFER, m_tlFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_tlColor, 0);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                  GL_FRAMEBUFFER_COMPLETE;

        GLint maxSamples = 1;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        const int samples = std::min(kSamples, int(maxSamples));
        m_tlFboSamples = kSamples;
        glGenFramebuffers(1, &m_tlFboMs);
        glBindFramebuffer(GL_FRAMEBUFFER, m_tlFboMs);
        glGenRenderbuffers(1, &m_tlColorMs);
        glBindRenderbuffer(GL_RENDERBUFFER, m_tlColorMs);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8,
                                         kW, kH);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, m_tlColorMs);
        glGenRenderbuffers(1, &m_tlDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, m_tlDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                         GL_DEPTH24_STENCIL8, kW, kH);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, m_tlDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo0));
        if (!ok) {
            glDeleteFramebuffers(1, &m_tlFbo);
            glDeleteFramebuffers(1, &m_tlFboMs);
            glDeleteTextures(1, &m_tlColor);
            glDeleteRenderbuffers(1, &m_tlColorMs);
            glDeleteRenderbuffers(1, &m_tlDepth);
            m_tlFbo = m_tlColor = m_tlFboMs = m_tlColorMs = m_tlDepth = 0;
            return;
        }
    }

    // The user's live camera — the timelapse replays exactly the views they
    // worked in. Copied so retargeting the aspect to 16:9 never disturbs the
    // on-screen view (a wider/narrower field, not a distortion).
    Camera cam = m_viewport->getCamera();
    cam.setAspect(float(kW) / float(kH));

    GLint prevFbo = 0, prevVp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevVp);

    // One clean scene pass from `c` into the MSAA target, resolved and stored.
    auto renderOnce = [&](Camera& c, bool moveFrame) {
        const glm::mat4 view = c.getViewMatrix();
        const glm::mat4 proj = c.getProjectionMatrix();
        glBindFramebuffer(GL_FRAMEBUFFER, m_tlFboMs);
#if !defined(MZ_GLES)
        glEnable(GL_MULTISAMPLE); // GLES: implied by the multisampled target
#endif
        glViewport(0, 0, kW, kH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        m_backgroundRenderer->render(); // gradient themed by the main pass
        glEnable(GL_DEPTH_TEST);
        m_shapeRenderer->render(view, proj, c.getPosition());
        if (m_edgeRenderer) m_edgeRenderer->render(view, proj);
        if (m_sketchRenderer) {
            for (int sid : m_document->getAllSketchIds()) {
                if (!m_document->isSketchVisible(sid)) continue;
                if (m_inSketchMode && sid == m_activeSketchId) continue;
                if (auto sk = m_document->getSketch(sid))
                    m_sketchRenderer->render(sk.get(), nullptr, view, proj,
                                             nullptr);
            }
            if (m_inSketchMode && m_activeSketch)
                m_sketchRenderer->render(m_activeSketch.get(), nullptr, view,
                                         proj, m_sketchSolver.get());
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_tlFboMs);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_tlFbo);
        glBlitFramebuffer(0, 0, kW, kH, 0, 0, kW, kH, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
        m_timelapse->captureFromTexture(m_tlColor, kW, kH, moveFrame);
    };

    // Camera-move fillers: when the view jumped since the last capture, glide
    // through a few eased in-between poses first (stored with the 'm' marker
    // so exports play them fast) — the replay pans instead of snapping.
    // Perspective↔ortho flips snap on purpose: sketch entry/exit reads better
    // as a cut. Function-local state; reset whenever the store is fresh.
    static bool haveLast = false;
    static glm::vec3 lastPos(0.0f), lastTarget(0.0f), lastUp(0.0f, 1.0f, 0.0f);
    static bool lastOrtho = false;
    if (m_timelapse->frameCount() == 0) haveLast = false;

    const glm::vec3 pos = cam.getPosition(), tgt = cam.getTarget(),
                    up = cam.getUp();
    // Video mode records real camera motion at ~10 Hz — synthetic glide
    // fillers are only for the sparse pixel-store fallback.
    if (!m_timelapse->videoMode() && haveLast &&
        lastOrtho == cam.isOrthographic()) {
        const float scale = std::max(glm::length(pos - tgt), 1e-3f);
        const float moved = (glm::length(pos - lastPos) +
                             glm::length(tgt - lastTarget)) / scale;
        if (moved > 0.04f) {
            const int k = std::clamp(int(moved * 8.0f), 1, 5);
            for (int i = 1; i <= k; ++i) {
                float t = float(i) / float(k + 1);
                t = t * t * (3.0f - 2.0f * t); // ease in–out
                Camera mid = cam;
                mid.setPosition(glm::mix(lastPos, pos, t));
                mid.setTarget(glm::mix(lastTarget, tgt, t));
                glm::vec3 u = glm::mix(lastUp, up, t);
                mid.setUp(glm::length(u) > 1e-4f ? glm::normalize(u) : up);
                renderOnce(mid, /*moveFrame=*/true);
            }
        }
    }
    renderOnce(cam, /*moveFrame=*/false);
    haveLast = true;
    lastPos = pos;
    lastTarget = tgt;
    lastUp = up;
    lastOrtho = cam.isOrthographic();

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}

} // namespace materializr
