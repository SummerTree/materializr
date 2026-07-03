#pragma once
// im-touch widget kit (docs/im-touch-ui-plan.md, Phase 1) — the five
// primitives the mockup is built from, so shell screens stay declarative.
// All sizes scale with uiScale(); every hit target is >= 44pt. Render inside
// a touchui::Scope (TouchTheme.h) for the intended look.

#include <imgui.h>

namespace materializr {
namespace touchui {

// Vertical rail entry: icon over a small label, accent-filled rounded rect
// when active. Fills the current content width. Returns true on press.
bool railButton(const char* id, const char* icon, const char* label, bool active);

// Rounded pill with an icon and optional label (top-bar actions). Returns
// true on press. `accent` fills it with the accent color (primary action).
bool pillButton(const char* id, const char* icon, const char* label = nullptr,
                bool accent = false);

// Square icon-only button (undo/redo/⋯). Side defaults to frame height.
bool iconButton(const char* id, const char* icon, float side = 0.0f);

// Segmented control (the Items | History switcher). Returns the active index
// (== `active` when untouched).
int segmented(const char* id, const char* const items[], int count, int active);

// Small-caps grey group header ("BODIES") with breathing room above.
void sectionHeader(const char* text);

// 44pt list row: leading visibility checkbox, label, trailing ⋯ button.
// Returns which part was pressed this frame.
struct ListRowAction {
    bool toggled  = false;  // checkbox changed (*checked already updated)
    bool clicked  = false;  // row body tapped (select)
    bool overflow = false;  // ⋯ tapped (caller opens its popup)
};
ListRowAction listRow(const char* id, bool* checked, const char* label,
                      bool selected = false, bool withOverflow = true);

} // namespace touchui
} // namespace materializr
