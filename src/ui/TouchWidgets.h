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
// by the lite shell's horizontal tool bar). `solid` gives the inactive state
// a panel-coloured fill too, so the button reads as solid standing alone on
// the viewport (not just when hovered/active) without a separate backing
// window. Returns true on press.
bool railButton(const char* id, const char* icon, const char* label, bool active,
                float width = 0.0f, bool solid = false);

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
// A non-empty `label` widens the box into a pill (icon + name to its right)
// so a strip of steps reads as words, not a wall of identical glyphs.
bool timelineBox(const char* id, const char* icon, bool current, bool editing,
                 bool dim, ImU32 iconCol = 0, float side = 0.0f,
                 const char* label = nullptr);

// Calculator-style value readout for the number pad: large right-aligned
// text in a framed well. `dim` styles placeholder/default values. `width`
// should match the pad below it (numberPadWidth) so they read as one unit.
void valueReadout(const char* id, const char* text, bool dim, float width);
// Total width of a numberPad with the given key side (3 keys + 2 gaps).
float numberPadWidth(float keySide = 0.0f);

// In-app numeric keypad (7 8 9 / 4 5 6 / 1 2 3 / . 0 ⌫) editing `buf` in
// place. Exists because the NATIVE mobile keyboard is a dead end for the
// im-touch dimension fields — raising iOS's keyboard from the SDL loop
// starved/froze the app, and a CAD dimension only ever needs digits and a
// dot anyway. Pure ImGui buttons: no SDL_StartTextInput, no IME, no focus.
// `allowSign` adds a full-width ± key (push/pull's negative = cut).
// Returns true when a key changed the buffer this frame.
bool numberPad(const char* id, char* buf, size_t bufSize, float keySide = 0.0f,
               bool allowSign = false);

// One-line numeric AMOUNT field for the im-touch layout: label + the value
// in a tappable well; tapping opens a number-pad popup (big readout, keys,
// ✗/✓). ✓ (with a valid entry) writes back into *v and returns true.
// Callers keep their InputText path for the other layouts and call this
// only when im-touch hosts the panel. minV<maxV clamps the committed value.
// The pad popup is PINNED: below the field's own well by default, or at
// `padPos` (screen coords) when given — multi-field dialogs (rectangle
// W/H) pass one shared anchor so the pad never jumps between fields.
bool amountField(const char* id, const char* label, double* v,
                 const char* suffix = "mm", int decimals = 1,
                 bool allowSign = false, double minV = 0.0, double maxV = 0.0,
                 const ImVec2* padPos = nullptr);
bool amountField(const char* id, const char* label, float* v,
                 const char* suffix = "mm", int decimals = 1,
                 bool allowSign = false, float minV = 0.0f, float maxV = 0.0f,
                 const ImVec2* padPos = nullptr);

// Fusion-style browser tree rows (the im-touch transparent Items overlay).
// Group header: disclosure triangle + label + count. Returns true on tap —
// the caller flips its open flag.
bool treeGroup(const char* id, const char* label, int count, bool open);
// Leaf under a group: eye visibility toggle (own hit area) + type icon +
// name, indented. Row tap = select; selected rows get a soft accent fill;
// hidden items render dimmed.
struct TreeLeafAction {
    bool eyeToggled = false;  // *visible already flipped
    bool clicked    = false;  // row body tapped (select)
};
TreeLeafAction treeLeaf(const char* id, const char* icon, const char* label,
                        bool* visible, bool selected);

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
