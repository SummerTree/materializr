#include "TouchWidgets.h"
#include "TouchTheme.h"
#include "TouchIcons.h"
#include "../ui_scale.h"

#include <imgui_internal.h> // ImGuiItemFlags_Disabled for iconButton dimming

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace materializr {
namespace touchui {

namespace {

// Icon drawn centered in a rect at an arbitrary size (the atlas glyph is
// bitmap-scaled; fine at the small deltas we use — revisit if soft).
void drawIconCentered(ImDrawList* dl, const ImVec2& center, float size,
                      const char* icon, ImU32 col) {
    // MZ_ICON_PRIMITIVE sentinel (U+E001): a square overlapping a larger
    // circle (the CAD-sketch look — square top-left, circle through its
    // bottom-right corner). No Iconoir glyph reads as "basic solids".
    if (std::strcmp(icon, "\xee\x80\x81") == 0) {
        const float th = std::max(1.5f, size * 0.075f);
        const float hs = size * 0.28f;                       // square half-side
        const ImVec2 sc(center.x - size * 0.17f, center.y - size * 0.17f);
        const float r = size * 0.30f;                        // circle radius
        const ImVec2 cc(center.x + size * 0.15f, center.y + size * 0.15f);
        dl->AddRect(ImVec2(sc.x - hs, sc.y - hs), ImVec2(sc.x + hs, sc.y + hs),
                    col, 0.0f, 0, th);
        dl->AddCircle(cc, r, col, 0, th);
        return;
    }
    // MZ_ICON_CHAMFER sentinel (U+E000): Iconoir has no straight-corner-cut
    // glyph, so draw one — a square outline with its top-right corner
    // chamfered off. Matches Iconoir's 1.5px-at-24px stroke look.
    if (std::strcmp(icon, "\xee\x80\x80") == 0) {
        const float h = size * 0.40f;          // half side
        const float c = h * 0.95f;             // chamfer leg length
        const ImVec2 pts[5] = {
            ImVec2(center.x - h,     center.y - h),      // TL
            ImVec2(center.x + h - c, center.y - h),      // top edge, cut start
            ImVec2(center.x + h,     center.y - h + c),  // right edge, cut end
            ImVec2(center.x + h,     center.y + h),      // BR
            ImVec2(center.x - h,     center.y + h),      // BL
        };
        dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed,
                        std::max(1.5f, size * 0.075f));
        return;
    }
    // MZ_ICON_UNFOLD sentinel (U+E002): a cube-unfold "Latin cross" of unit
    // squares — the flat-pattern look. (Iconoir's ruler-combine glyph didn't
    // read as "unfold".)
    if (std::strcmp(icon, "\xee\x80\x82") == 0) {
        const float cell = size * 0.22f;
        const float th   = std::max(1.2f, size * 0.05f);
        const ImVec2 o(center.x - cell * 2.0f, center.y - cell * 1.5f); // grid TL
        // (col,row) cells: a horizontal strip of 4 with two arms on the 3rd
        // column (the cross laid on its side, so it doesn't read as a crucifix).
        static const int cells[6][2] =
            {{2,0},{0,1},{1,1},{2,1},{3,1},{2,2}};
        for (const auto& c : cells)
            dl->AddRect(ImVec2(o.x + c[0] * cell,       o.y + c[1] * cell),
                        ImVec2(o.x + (c[0]+1) * cell,   o.y + (c[1]+1) * cell),
                        col, 0.0f, 0, th);
        return;
    }
    // MZ_ICON_PATTERN_LINEAR sentinel (U+E003): three squares in a row.
    if (std::strcmp(icon, "\xee\x80\x83") == 0) {
        const float cell = size * 0.26f;
        const float th   = std::max(1.5f, size * 0.06f);
        const ImVec2 o(center.x - cell * 1.5f, center.y - cell * 0.5f);
        for (int i = 0; i < 3; ++i)
            dl->AddRect(ImVec2(o.x + i * cell,     o.y),
                        ImVec2(o.x + (i+1) * cell, o.y + cell), col, 0.0f, 0, th);
        return;
    }
    // MZ_ICON_PATTERN_CIRCULAR sentinel (U+E004): three squares spaced around a
    // centre (120° apart, one at the bottom) — the radial-pattern look.
    if (std::strcmp(icon, "\xee\x80\x84") == 0) {
        const float hs = size * 0.12f;                 // square half-side
        const float r  = size * 0.30f;                 // ring radius
        const float th = std::max(1.5f, size * 0.06f);
        const ImVec2 ctr[3] = {
            ImVec2(center.x,              center.y + r),         // bottom
            ImVec2(center.x - 0.866f * r, center.y - 0.5f * r),  // top-left
            ImVec2(center.x + 0.866f * r, center.y - 0.5f * r),  // top-right
        };
        for (const auto& c : ctr)
            dl->AddRect(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs),
                        col, 0.0f, 0, th);
        return;
    }
    ImFont* font = ImGui::GetFont();
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
    dl->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                col, icon);
}

} // namespace

bool railButton(const char* id, const char* icon, const char* label, bool active,
                float width, bool solid) {
    const float s = uiScale();
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    // 52 (was 62): shorter so the whole tool set fits with less scrolling,
    // still comfortably above the 44pt touch floor. If this changes, update
    // the lite shell's bottom-bar pill alignment (hardcodes the same height).
    const float h = 52.0f * s;

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##rail", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (active) {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          ImGui::GetColorU32(accentFill()), 12.0f * s);
    } else if (hovered || ImGui::IsItemActive()) {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          ImGui::GetColorU32(rowBg()), 12.0f * s);
    } else if (solid) {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          ImGui::GetColorU32(panelBg()), 12.0f * s);
    }

    const ImU32 fg = ImGui::GetColorU32(active ? onAccent() : textPrimary());
    const ImU32 fgDim = ImGui::GetColorU32(active ? onAccent() : textDim());
    drawIconCentered(dl, ImVec2(p.x + w * 0.5f, p.y + h * 0.38f), 22.0f * s, icon, fg);

    ImFont* font = ImGui::GetFont();
    const float ls = 11.0f * s;
    const ImVec2 lsz = font->CalcTextSizeA(ls, FLT_MAX, 0.0f, label);
    dl->AddText(font, ls,
                ImVec2(p.x + (w - lsz.x) * 0.5f, p.y + h * 0.62f), fgDim, label);
    ImGui::PopID();
    return pressed;
}

float pillButtonWidth(const char* icon, const char* label) {
    const float s = uiScale();
    const float h = std::max(ImGui::GetFrameHeight(), 44.0f * s);
    const float is = 17.0f * s;                       // icon size
    ImFont* font = ImGui::GetFont();
    float w = 20.0f * s; // horizontal padding total
    if (icon)  w += font->CalcTextSizeA(is, FLT_MAX, 0.0f, icon).x;
    if (label) w += ImGui::CalcTextSize(label).x + (icon ? 7.0f * s : 0.0f);
    return std::max(w, h); // never narrower than tall
}

bool pillButton(const char* id, const char* icon, const char* label, bool accent) {
    const float s = uiScale();
    const float h = std::max(ImGui::GetFrameHeight(), 44.0f * s);
    const float is = 17.0f * s;                       // icon size
    ImFont* font = ImGui::GetFont();
    const float w = pillButtonWidth(icon, label);

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##pill", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 bg = accent ? accentFill() : rowBg();
    if (hovered && !accent) bg = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
    if (ImGui::IsItemActive()) bg = accent ? accentDeep() : ImVec4(0.20f, 0.24f, 0.31f, 1.0f);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(bg), h * 0.32f);

    const ImU32 fg = ImGui::GetColorU32(accent ? onAccent() : textPrimary());
    float x = p.x + 10.0f * s;
    if (icon) {
        const ImVec2 ts = font->CalcTextSizeA(is, FLT_MAX, 0.0f, icon);
        dl->AddText(font, is, ImVec2(x, p.y + (h - ts.y) * 0.5f), fg, icon);
        x += ts.x + (label ? 7.0f * s : 0.0f);
    }
    if (label) {
        const ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(x, p.y + (h - ts.y) * 0.5f), fg, label);
    }
    ImGui::PopID();
    return pressed;
}

bool iconButton(const char* id, const char* icon, float side) {
    const float s = uiScale();
    if (side <= 0.0f) side = std::max(ImGui::GetFrameHeight(), 44.0f * s);

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##ib", ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool enabled =
        !(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled);
    ImVec4 bg = rowBg();
    if (hovered) bg = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
    if (ImGui::IsItemActive()) bg = ImVec4(0.20f, 0.24f, 0.31f, 1.0f);
    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side),
                      ImGui::GetColorU32(bg), 10.0f * s);
    drawIconCentered(dl, ImVec2(p.x + side * 0.5f, p.y + side * 0.5f), 17.0f * s,
                     icon,
                     ImGui::GetColorU32(enabled ? textPrimary() : textDim()));
    ImGui::PopID();
    return pressed;
}

bool fab(const char* id, const char* icon, float diameter) {
    const float s = uiScale();
    if (diameter <= 0.0f) diameter = 56.0f * s;

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##fab", ImVec2(diameter, diameter));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 bg = accentFill();
    if (hovered) bg = ImVec4(0.62f, 0.75f, 0.96f, 1.0f);
    if (ImGui::IsItemActive()) bg = accentDeep();
    const ImVec2 c(p.x + diameter * 0.5f, p.y + diameter * 0.5f);
    dl->AddCircleFilled(c, diameter * 0.5f, ImGui::GetColorU32(bg));
    drawIconCentered(dl, c, 24.0f * s, icon, ImGui::GetColorU32(onAccent()));
    ImGui::PopID();
    return pressed;
}

int segmented(const char* id, const char* const items[], int count, int active) {
    const float s = uiScale();
    const float h = 44.0f * s;
    const float w = ImGui::GetContentRegionAvail().x;

    // Segments are sized proportionally to their labels (a short "Items" cedes
    // room to "History & Properties") instead of equal halves; text is clipped
    // to its segment so a too-narrow panel can't bleed the label off-panel.
    float need[16];
    float total = 0.0f;
    const int n = count > 16 ? 16 : count;
    for (int i = 0; i < n; ++i) {
        need[i] = ImGui::CalcTextSize(items[i]).x + 24.0f * s;
        total += need[i];
    }

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int result = active;
    float x = p.x;
    for (int i = 0; i < n; ++i) {
        const float seg = w * (need[i] / total);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(x, p.y));
        if (ImGui::InvisibleButton("##seg", ImVec2(seg, h))) result = i;
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a(x, p.y), b(x + seg, p.y + h);
        if (i == active) {
            // Active segment: outlined pill (mockup style).
            dl->AddRectFilled(a, b, ImGui::GetColorU32(rowBg()), 10.0f * s);
            dl->AddRect(a, b, ImGui::GetColorU32(accentDeep()), 10.0f * s, 0,
                        2.0f * s);
        } else if (hovered) {
            dl->AddRectFilled(a, b, ImGui::GetColorU32(rowBg()), 10.0f * s);
        }
        const ImVec2 ts = ImGui::CalcTextSize(items[i]);
        dl->PushClipRect(a, b, true);
        dl->AddText(ImVec2(a.x + std::max(6.0f * s, (seg - ts.x) * 0.5f),
                           a.y + (h - ts.y) * 0.5f),
                    ImGui::GetColorU32(i == active ? textPrimary() : textDim()),
                    items[i]);
        dl->PopClipRect();
        ImGui::PopID();
        x += seg;
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Spacing();
    ImGui::PopID();
    return result;
}

void sectionHeader(const char* text) {
    const float s = uiScale();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    // Small caps: uppercase at a smaller size, tracked out by the font.
    char buf[64];
    int n = 0;
    for (const char* c = text; *c && n < 63; ++c)
        buf[n++] = (*c >= 'a' && *c <= 'z') ? static_cast<char>(*c - 32) : *c;
    buf[n] = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(ImGui::GetFont(), 12.0f * s, p, ImGui::GetColorU32(textDim()), buf);
    ImGui::Dummy(ImVec2(0.0f, 16.0f * s));
}

bool timelineBox(const char* id, const char* icon, bool current, bool editing,
                 bool dim, ImU32 iconCol, float side, const char* label) {
    const float s = uiScale();
    if (side <= 0.0f) side = 48.0f * s;

    const bool hasLabel = label && label[0];
    // History steps stack the label UNDER a slightly smaller icon, in a slightly
    // smaller font, with tight side padding — so a long run of named steps stays
    // compact. These metrics are private to this widget (the timeline is its
    // only caller), so nothing else is affected.
    const float iconSz   = 18.0f * s;                     // ~2px smaller
    const float fscale   = 0.82f;                         // label a little smaller
    const float fontSize = ImGui::GetFontSize() * fscale;
    const float padX     = 8.0f * s;                      // side dead-space
    const float padY     = 6.0f * s;
    const float vgap     = 3.0f * s;                      // icon → label gap
    ImVec2 ts(0.0f, 0.0f);
    if (hasLabel) {
        ts = ImGui::CalcTextSize(label);
        ts.x *= fscale;   // width scales with the font (glyphs scale uniformly)
        ts.y = fontSize;
    }
    const float boxW = hasLabel ? (std::max(iconSz, ts.x) + 2.0f * padX) : side;
    const float boxH = hasLabel ? (padY + iconSz + vgap + ts.y + padY) : side;

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##tl", ImVec2(boxW, boxH));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 bg = current ? accentFill() : rowBg();
    if (hovered && !current) bg = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
    if (ImGui::IsItemActive())
        bg = current ? accentDeep() : ImVec4(0.20f, 0.24f, 0.31f, 1.0f);
    dl->AddRectFilled(p, ImVec2(p.x + boxW, p.y + boxH),
                      ImGui::GetColorU32(bg), 10.0f * s);
    if (editing)
        dl->AddRect(p, ImVec2(p.x + boxW, p.y + boxH),
                    ImGui::GetColorU32(accentDeep()), 10.0f * s, 0, 2.0f * s);

    ImU32 fg = iconCol;
    if (fg == 0)
        fg = ImGui::GetColorU32(current ? onAccent()
                                        : (dim ? textDim() : textPrimary()));
    const float iconCy = hasLabel ? (p.y + padY + iconSz * 0.5f)
                                   : (p.y + boxH * 0.5f);
    drawIconCentered(dl, ImVec2(p.x + boxW * 0.5f, iconCy), iconSz, icon, fg);
    if (hasLabel)
        dl->AddText(ImGui::GetFont(), fontSize,
                    ImVec2(p.x + (boxW - ts.x) * 0.5f, p.y + padY + iconSz + vgap),
                    fg, label);
    ImGui::PopID();
    return pressed;
}

ListRowAction listRow(const char* id, bool* checked, const char* label,
                      bool selected, bool withOverflow) {
    ListRowAction act;
    const float s = uiScale();
    const float h = 44.0f * s;
    const float w = ImGui::GetContentRegionAvail().x;
    const float box = 22.0f * s;
    const float pad = 10.0f * s;
    const float ovW = withOverflow ? h : 0.0f;

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lead = checked ? pad + box + pad : pad;

    // Claim the WHOLE row rect first. The internals below are placed with
    // SetCursorScreenPos, and ImGui flags any such jump past the window's
    // current content max at the next item ("uses SetCursorPos to extend
    // window boundaries — submit an item e.g. Dummy()"). With the row rect
    // claimed up front, every internal placement stays within bounds. The
    // max() guards the FIRST frame of an auto-resize host, where the content
    // region reports ~0 wide — claim at least the internals' extent or the
    // checkbox placement still lands out of bounds (and the warning banner
    // it trips sticks for the whole session).
    ImGui::Dummy(ImVec2(std::max(w, lead + ovW + 1.0f), h));
    // ImGui TRUNCATES item advances to whole pixels, so the claimed row
    // bottom is trunc-based — recompute it exactly the way ItemSize() does
    // (post-Dummy cursor minus spacing) instead of the raw p.y + h, which at
    // a fractional uiScale overshoots the claim by the fraction and trips
    // the boundary warning on EVERY row (found via instrumented logcat:
    // cur.y 673.80 vs max.y 673.00 at s = 1.7).
    const float rowBottom =
        ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;

    // Checkbox (visibility) FIRST, with its own exclusive hit area — a row
    // button submitted before it would claim its clicks (ImGui gives the
    // press to the first hovered item), leaving the checkbox untappable.
    bool chkHov = false;
    if (checked) {
        const ImVec2 cb(p.x + pad, p.y + (h - box) * 0.5f);
        ImGui::SetCursorScreenPos(cb);
        if (ImGui::InvisibleButton("##chk", ImVec2(box, box))) {
            *checked = !*checked;
            act.toggled = true;
        }
        chkHov = ImGui::IsItemHovered();
    }

    // Row body (select) — from after the checkbox to before the trailing ⋯,
    // so the three hit areas never overlap.
    ImGui::SetCursorScreenPos(ImVec2(p.x + lead, p.y));
    act.clicked = ImGui::InvisibleButton(
        "##row", ImVec2(std::max(1.0f, w - lead - ovW), h));
    const bool rowHov = ImGui::IsItemHovered();

    if (selected)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          ImGui::GetColorU32(rowBg()), 10.0f * s);
    else if (rowHov)
        dl->AddRectFilled(p, ImVec2(p.x + w - ovW, p.y + h),
                          ImGui::GetColorU32(ImVec4(0.09f, 0.10f, 0.13f, 1.0f)),
                          10.0f * s);

    if (checked) {
        const ImVec2 cb(p.x + pad, p.y + (h - box) * 0.5f);
        if (*checked) {
            dl->AddRectFilled(cb, ImVec2(cb.x + box, cb.y + box),
                              ImGui::GetColorU32(accentFill()), 6.0f * s);
            ImFont* font = ImGui::GetFont();
            const float cs = 14.0f * s;
            const ImVec2 ts = font->CalcTextSizeA(cs, FLT_MAX, 0.0f, MZ_ICON_CHECK);
            dl->AddText(font, cs,
                        ImVec2(cb.x + (box - ts.x) * 0.5f, cb.y + (box - ts.y) * 0.5f),
                        ImGui::GetColorU32(onAccent()), MZ_ICON_CHECK);
        } else {
            dl->AddRect(cb, ImVec2(cb.x + box, cb.y + box),
                        ImGui::GetColorU32(chkHov ? textDim() : hairline()),
                        6.0f * s, 0, 2.0f * s);
        }
    }

    // Label.
    const float lx = p.x + lead;
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(lx, p.y + (h - ts.y) * 0.5f),
                ImGui::GetColorU32(textPrimary()), label);

    // Trailing ⋯.
    if (withOverflow) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - ovW, p.y));
        if (ImGui::InvisibleButton("##ovf", ImVec2(ovW, h))) act.overflow = true;
        const bool ovHov = ImGui::IsItemHovered();
        drawIconCentered(dl, ImVec2(p.x + w - ovW * 0.5f, p.y + h * 0.5f),
                         16.0f * s, MZ_ICON_MORE,
                         ImGui::GetColorU32(ovHov ? textPrimary() : textDim()));
    }

    // Next row starts right below (tight stacking). rowBottom is bit-exact
    // with the Dummy's claimed max, so this never extends boundaries.
    ImGui::SetCursorScreenPos(ImVec2(p.x, rowBottom));
    ImGui::PopID();
    return act;
}

} // namespace touchui
} // namespace materializr
