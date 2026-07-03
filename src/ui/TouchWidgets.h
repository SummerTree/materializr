#pragma once
// im-touch widget kit (docs/im-touch-ui-plan.md, Phase 1) — the five
// primitives the mockup is built from, so shell screens stay declarative.
// All sizes scale with uiScale(); every hit target is >= 44pt. Render inside
// a touchui::Scope (TouchTheme.h) for the intended look.

#include <imgui.h>

namespace materializr {
namespace touchui {

// Vertical rail entry: icon over a small label, accent-filled rounded rect
// when active. Fills the current content width (or `width` when > 0 — used
// by the lite shell's horizontal tool bar). Returns true on press.
bool railButton(const char* id, const char* icon, const char* label, bool active,
                float width = 0.0f);

// Floating action button: filled accent circle with a centered icon
// (im-touch-lite's "+ create"). Returns true on press.
bool fab(const char* id, const char* icon, float diameter = 0.0f);

// Rounded pill with an icon and optional label (top-bar actions). Returns
// true on press. `accent` fills it with the accent color (primary action).
bool pillButton(const char* id, const char* icon, const char* label = nullptr,
                bool accent = false);
// Exact width pillButton(icon, label) will occupy — for right-aligned layout
// math (the top bar). Shares the sizing code so the two can't drift.
float pillButtonWidth(const char* icon, const char* label = nullptr);

// Square icon-only button (undo/redo/⋯). Side defaults to frame height.
bool iconButton(const char* id, const char* icon, float side = 0.0f);

// Segmented control (the Items | History switcher). Returns the active index
// (== `active` when untouched).
int segmented(const char* id, const char* const items[], int count, int active);

// Small-caps grey group header ("BODIES") with breathing room above.
void sectionHeader(const char* text);

// Fusion-style history timeline box (im-touch-lite bottom strip): a rounded
// square holding the step's op icon. `current` fills it with the accent (the
// history marker sits on this step); `editing` outlines it (its properties
// popup is open); `dim` greys the icon (undone / disabled steps); `iconCol`
// overrides the icon colour when non-zero (frozen amber, failed red).
// Returns true on press.
bool timelineBox(const char* id, const char* icon, bool current, bool editing,
                 bool dim, ImU32 iconCol = 0, float side = 0.0f);

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
