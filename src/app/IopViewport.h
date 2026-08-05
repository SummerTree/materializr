#pragma once
#include <functional>
#include <glm/glm.hpp>

namespace materializr {

// What an interactive-op controller needs from the VIEWPORT, in the same
// spirit as IopContext (which covers the document side).
//
// Why this exists: InteractiveOpController models a popup with a live preview
// and knows nothing about dragging. ScaleFaceController is the one extracted op
// with a gizmo, and it coped by exposing its whole frame — center, both axes,
// both extents, the latched drag axis — as public accessors that
// Application_Viewport reached into to run the hit-test and the drag itself.
// That works for one op. Copying it for Move Face (152 viewport references),
// the edge ops (103) and Push/Pull (71) would move the STATE out of Application
// while leaving the COUPLING exactly where it was.
//
// So the drag lives with the op that owns it, and this struct is the whole of
// what the viewport hands over. Deliberately no ImGui types: screen positions
// are glm::vec2 in window pixels and colours are packed 0xAABBGGRR, so a
// controller header never includes imgui.h. (These types live in src/app —
// the adapter layer — not core; the point is to keep the boundary narrow and
// explicit, not to pretend the app is UI-free.)
struct IopViewport {
    // World → screen (window pixels). False when the point is behind the eye.
    std::function<bool(const glm::vec3& world, glm::vec2& screenOut)> toScreen;
    // A screen-space mouse delta projected onto a world axis, in world units —
    // the same helper the extrude/push-pull arrows drag with.
    std::function<float(const glm::vec3& origin, const glm::vec3& axis,
                        const glm::vec2& screenDelta)> dragAlongAxis;

    glm::vec2 mouse{0.0f};        // cursor, window pixels
    glm::vec2 mouseDelta{0.0f};   // since last frame

    bool clicked = false;   // left button went down this frame
    bool dragging = false;  // left button held and moving
    bool released = false;  // left button came up this frame
    bool down = false;      // left button currently held

    // The cursor's world-space pick ray — for gizmos that intersect it with
    // their own planes/rings rather than projecting a screen delta.
    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDir{0.0f, 0.0f, -1.0f};

    // Camera frame, for distance-scaled handle sizing (ring radius, arm
    // length track the zoom the same way the main gizmo does).
    glm::vec3 camPos{0.0f};
    glm::vec3 camTarget{0.0f};
};

// Immediate-mode draw surface for a controller's on-screen handles. Same rule:
// no ImGui types cross this line. Colours are packed 0xAABBGGRR (IM_COL32
// order), so call sites read the same as the code they replace.
struct IopOverlay {
    std::function<bool(const glm::vec3& world, glm::vec2& screenOut)> toScreen;
    std::function<void(glm::vec2 a, glm::vec2 b, unsigned rgba,
                       float thickness)> line;
    std::function<void(glm::vec2 centre, float radius, unsigned rgba)> disc;
    // Text with its own backing plate, offset clear of the anchor — the
    // percentage readouts every handle draws.
    std::function<void(glm::vec2 at, const char* text, unsigned rgba)> label;
};

// The REAL 3D gizmo meshes — depth-tested cones, torus rings and cubes drawn
// by the Gizmo renderer, camera-scaled. Separate from IopOverlay because the
// two run in different halves of the frame: drawGizmos3D() is invoked during
// the 3D pass, while the offscreen viewport FBO is still bound, so the meshes
// land in the scene and depth-test against it. drawOverlay() runs later,
// after unbind(), while the ImGui draw list is being built — a raw GL call
// there hits the WINDOW framebuffer and the UI paints straight over it
// (these three calls used to live on IopOverlay, and drew exactly nothing).
// Same boundary rule: world-space glm vectors in, colour packed 0xAABBGGRR.
struct IopGizmo3D {
    std::function<void(const glm::vec3& at, const glm::vec3& dir,
                       unsigned rgba)> arrow;
    std::function<void(const glm::vec3& at, const glm::vec3& axis,
                       unsigned rgba)> ring;
    std::function<void(const glm::vec3& at, const glm::vec3& dir,
                       unsigned rgba)> cube;
};

} // namespace materializr
