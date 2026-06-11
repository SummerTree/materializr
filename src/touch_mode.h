#pragma once

// Runtime "touch mode" flag. When ON, the UI scales up for fingers and input is
// interpreted as touch gestures (long-press context menus, press-drag-release
// drawing, on-screen Multi-Select / Move toggles, trackpad-style navigation,
// SVG-import without hover). When OFF, the desktop mouse/keyboard model applies.
//
// It defaults to the platform (on for Android, off elsewhere) but is a saved
// user setting, so a tablet with a mouse/keyboard/trackpad attached can run the
// full desktop interaction model — the touch adaptations are a mode, not baked
// into the build.
//
// The value is fixed for a run: Application sets it from the saved setting at
// startup (before fonts/scale are baked), and everything reads it live via
// touchMode(). Changing it in Settings persists the choice and takes full effect
// on the next launch. Header-only (inline function-local static) so it's a
// single shared instance across translation units with no extra .cpp / CMake
// entry — same pattern as ui_scale.h.
namespace materializr {

inline bool& touchModeRef() {
    static bool t =
#if defined(__ANDROID__)
        true;
#else
        false;
#endif
    return t;
}
inline bool touchMode() { return touchModeRef(); }
inline void setTouchMode(bool on) { touchModeRef() = on; }

} // namespace materializr
