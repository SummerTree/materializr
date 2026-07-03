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
    ImFont* font = ImGui::GetFont();
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
    dl->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                col, icon);
}

} // namespace

bool railButton(const char* id, const char* icon, const char* label, bool active,
                float width) {
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

bool pillButton(const char* id, const char* icon, const char* label, bool accent) {
    const float s = uiScale();
    const float h = std::max(ImGui::GetFrameHeight(), 44.0f * s);
    const float is = 17.0f * s;                       // icon size
    ImFont* font = ImGui::GetFont();

    float w = 20.0f * s; // horizontal padding total
    if (icon)  w += font->CalcTextSizeA(is, FLT_MAX, 0.0f, icon).x;
    if (label) w += ImGui::CalcTextSize(label).x + (icon ? 7.0f * s : 0.0f);
    w = std::max(w, h); // never narrower than tall

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
    const float seg = w / static_cast<float>(count);

    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int result = active;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(p.x + seg * i, p.y));
        if (ImGui::InvisibleButton("##seg", ImVec2(seg, h))) result = i;
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a(p.x + seg * i, p.y), b(p.x + seg * (i + 1), p.y + h);
        if (i == active) {
            // Active segment: outlined pill (mockup style).
            dl->AddRectFilled(a, b, ImGui::GetColorU32(rowBg()), 10.0f * s);
            dl->AddRect(a, b, ImGui::GetColorU32(accentDeep()), 10.0f * s, 0,
                        2.0f * s);
        } else if (hovered) {
            dl->AddRectFilled(a, b, ImGui::GetColorU32(rowBg()), 10.0f * s);
        }
        const ImVec2 ts = ImGui::CalcTextSize(items[i]);
        dl->AddText(ImVec2(a.x + (seg - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f),
                    ImGui::GetColorU32(i == active ? textPrimary() : textDim()),
                    items[i]);
        ImGui::PopID();
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

    // Row body (select) — leaves room for the trailing ⋯.
    act.clicked = ImGui::InvisibleButton("##row", ImVec2(w - ovW, h));
    const bool rowHov = ImGui::IsItemHovered();
    if (selected)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          ImGui::GetColorU32(rowBg()), 10.0f * s);
    else if (rowHov)
        dl->AddRectFilled(p, ImVec2(p.x + w - ovW, p.y + h),
                          ImGui::GetColorU32(ImVec4(0.09f, 0.10f, 0.13f, 1.0f)),
                          10.0f * s);

    // Checkbox (visibility) — its own hit area inside the row.
    if (checked) {
        const ImVec2 cb(p.x + pad, p.y + (h - box) * 0.5f);
        ImGui::SetCursorScreenPos(cb);
        if (ImGui::InvisibleButton("##chk", ImVec2(box, box))) {
            *checked = !*checked;
            act.toggled = true;
            act.clicked = false;
        }
        const bool chkHov = ImGui::IsItemHovered();
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
    const float lx = p.x + (checked ? pad + box + pad : pad);
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

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::PopID();
    return act;
}

} // namespace touchui
} // namespace materializr
