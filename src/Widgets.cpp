#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "Widgets.h"
#include "Visuals.h"         // drawJet, for the jet icon
#include "imgui_internal.h"  // ShadeVertsLinearColorGradientKeepAlpha, for gradients
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>

namespace ui {

FontSet fonts;

namespace {

constexpr float kPi = 3.14159265f;

ImU32 u32(ImVec4 c, float alphaMul = 1.0f) {
    c.w *= alphaMul;
    return ImGui::ColorConvertFloat4ToU32(c);
}

// The visible part of a label: "Quit##quiz" shows as "Quit".
const char* labelEnd(const char* label) {
    const char* hash = std::strstr(label, "##");
    return hash ? hash : label + std::strlen(label);
}

void textAt(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 color, const char* text, const char* end = nullptr,
            float wrap = 0.0f) {
    dl->AddText(f, fontPx(f), ImVec2(std::floor(pos.x), std::floor(pos.y)), color, text, end, wrap);
}

ImVec2 measure(ImFont* f, const char* text, const char* end = nullptr) {
    return f->CalcTextSizeA(fontPx(f), FLT_MAX, 0.0f, text, end);
}

void textCentered(ImDrawList* dl, ImFont* f, ImVec2 center, ImU32 color, const char* text,
                  const char* end = nullptr) {
    ImVec2 s = measure(f, text, end);
    textAt(dl, f, ImVec2(center.x - s.x * 0.5f, center.y - s.y * 0.5f), color, text, end);
}

void handCursorIfHovered() {
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

const char* firstExisting(std::initializer_list<const char*> paths) {
    for (const char* p : paths)
        if (std::ifstream(p).good()) return p;
    return nullptr;
}

float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

}  // namespace

// ================================================================ setup

float fontPx(ImFont* f) { return f->FontSize * ImGui::GetIO().FontGlobalScale; }

ImVec2 textSize(ImFont* font, const char* text, float wrap) {
    return font->CalcTextSizeA(fontPx(font), FLT_MAX, wrap > 0 ? wrap : 0.0f, text);
}

Tint tint(int i) {
    static const Tint t[] = {
        {{0.910f, 0.922f, 0.996f, 1}, {0.286f, 0.345f, 0.792f, 1}},  // indigo
        {{0.871f, 0.957f, 0.937f, 1}, {0.055f, 0.522f, 0.459f, 1}},  // teal
        {{0.996f, 0.933f, 0.878f, 1}, {0.851f, 0.451f, 0.094f, 1}},  // orange
        {{0.945f, 0.910f, 0.992f, 1}, {0.502f, 0.302f, 0.808f, 1}},  // purple
        {{0.894f, 0.965f, 0.906f, 1}, {0.180f, 0.565f, 0.282f, 1}},  // green
        {{0.992f, 0.906f, 0.933f, 1}, {0.812f, 0.239f, 0.420f, 1}},  // rose
        {{0.894f, 0.937f, 0.996f, 1}, {0.157f, 0.427f, 0.808f, 1}},  // blue
        {{0.996f, 0.957f, 0.851f, 1}, {0.733f, 0.502f, 0.035f, 1}},  // amber
        {{0.871f, 0.957f, 0.980f, 1}, {0.035f, 0.494f, 0.620f, 1}},  // cyan
        {{0.918f, 0.929f, 0.957f, 1}, {0.278f, 0.333f, 0.459f, 1}},  // slate
    };
    return t[((i % 10) + 10) % 10];
}

void loadFonts(ImGuiIO& io, float scale) {
    // Latin, Greek (pi), punctuation, superscripts and subscripts, arrows, math symbols
    static const ImWchar fullRanges[] = {0x0020, 0x00FF, 0x0370, 0x03FF, 0x2000, 0x206F, 0x2070, 0x209F,
                                         0x2190, 0x21FF, 0x2200, 0x22FF, 0};
    static const ImWchar basicRanges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2022, 0x2022, 0x2190, 0x2193, 0x2212, 0x2212, 0};

    const char* regular = firstExisting({
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",  // macOS
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",                          // Windows
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",  // Linux
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    });
    const char* bold = firstExisting({
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    });
    // Fills in symbols a font lacks (on macOS, Arial Unicode has them all).
    const char* symbols = firstExisting({
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "C:\\Windows\\Fonts\\seguisym.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    });

    if (!regular) {  // no system fonts: ImGui's built-in font everywhere
        ImFont* f = io.Fonts->AddFontDefault();
        fonts = {f, f, f, f, f, f, f, f, f};
        io.FontGlobalScale = 1.3f;
        return;
    }
    if (!bold) bold = regular;

    auto add = [&](const char* path, float size, const ImWchar* ranges) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, size * scale, nullptr, ranges);
        if (symbols && std::strcmp(symbols, path) != 0) {
            ImFontConfig merge;
            merge.MergeMode = true;  // only adds characters the main font is missing
            io.Fonts->AddFontFromFileTTF(symbols, size * scale, &merge, ranges);
        }
        return f;
    };

    fonts.body = add(regular, 16.0f, fullRanges);
    fonts.small = add(regular, 13.5f, fullRanges);
    fonts.strong = add(bold, 16.0f, fullRanges);
    fonts.label = add(bold, 12.5f, basicRanges);
    fonts.prompt = add(regular, 20.0f, fullRanges);
    fonts.heading = add(bold, 21.0f, fullRanges);
    fonts.title = add(bold, 30.0f, basicRanges);
    fonts.big = add(bold, 32.0f, basicRanges);
    fonts.hero = add(bold, 46.0f, basicRanges);

    io.FontDefault = fonts.body;
    io.FontGlobalScale = 1.0f / scale;  // fonts are rendered at full Retina resolution
}

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsLight(&s);
    s.WindowPadding = ImVec2(0, 0);
    s.ItemSpacing = ImVec2(12, 12);
    s.FramePadding = ImVec2(12, 8);
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.PopupRounding = 12.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameRounding = 8.0f;
    s.ScrollbarSize = 10.0f;
    s.ScrollbarRounding = 8.0f;
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = col::Bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = col::Card;
    c[ImGuiCol_Text] = col::Ink;
    c[ImGuiCol_TextDisabled] = col::Muted;
    c[ImGuiCol_Border] = col::CardBorder;
    c[ImGuiCol_Separator] = col::CardBorder;
    c[ImGuiCol_Header] = col::AccentSoft;  // selected menu row
    c[ImGuiCol_HeaderHovered] = ImVec4(0.855f, 0.886f, 0.984f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.800f, 0.843f, 0.976f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.70f, 0.74f, 0.82f, 0.7f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.65f, 0.75f, 0.9f);
    c[ImGuiCol_ScrollbarGrabActive] = col::Accent;
}

// ================================================================ icons

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 color) {
    const float h = size * 0.5f;
    const float t = std::max(1.6f, size * 0.085f);  // line thickness
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };

    switch (icon) {
        case Icon::Home:
            dl->AddLine(P(-0.95f, -0.05f), P(0.0f, -0.9f), color, t);
            dl->AddLine(P(0.0f, -0.9f), P(0.95f, -0.05f), color, t);
            dl->AddRect(P(-0.62f, -0.2f), P(0.62f, 0.85f), color, 2.0f, 0, t);
            dl->AddRectFilled(P(-0.16f, 0.3f), P(0.16f, 0.85f), color);
            break;
        case Icon::Target:
            dl->AddCircle(c, h * 0.9f, color, 0, t);
            dl->AddCircle(c, h * 0.48f, color, 0, t);
            dl->AddCircleFilled(c, h * 0.15f, color);
            break;
        case Icon::Clock:
            dl->AddCircle(c, h * 0.9f, color, 0, t);
            dl->AddLine(c, P(0.0f, -0.55f), color, t);
            dl->AddLine(c, P(0.38f, 0.18f), color, t);
            break;
        case Icon::Review: {
            const float r = h * 0.78f, a0 = -2.6f, a1 = 2.0f;
            dl->PathArcTo(c, r, a0, a1, 24);
            dl->PathStroke(color, 0, t);
            ImVec2 tip(c.x + std::cos(a0) * r, c.y + std::sin(a0) * r);
            dl->AddTriangleFilled(ImVec2(tip.x - h * 0.42f, tip.y - h * 0.02f), ImVec2(tip.x + h * 0.18f, tip.y - h * 0.48f),
                                  ImVec2(tip.x + h * 0.22f, tip.y + h * 0.18f), color);
            break;
        }
        case Icon::Cards:
            dl->AddRect(P(-0.85f, -0.45f), P(0.5f, 0.9f), color, size * 0.08f, 0, t);
            dl->AddLine(P(-0.5f, -0.85f), P(0.85f, -0.85f), color, t);
            dl->AddLine(P(0.85f, -0.85f), P(0.85f, 0.55f), color, t);
            break;
        case Icon::Chart:
            dl->AddRect(P(-0.85f, 0.15f), P(-0.4f, 0.85f), color, 2.0f, 0, t);
            dl->AddRect(P(-0.22f, -0.35f), P(0.22f, 0.85f), color, 2.0f, 0, t);
            dl->AddRect(P(0.4f, -0.85f), P(0.85f, 0.85f), color, 2.0f, 0, t);
            break;
        case Icon::List:
            for (int i = 0; i < 3; ++i) {
                float y = -0.6f + 0.6f * static_cast<float>(i);
                dl->AddCircleFilled(P(-0.7f, y), h * 0.12f, color);
                dl->AddLine(P(-0.35f, y), P(0.85f, y), color, t);
            }
            break;
        case Icon::Check:
            dl->AddLine(P(-0.6f, 0.0f), P(-0.15f, 0.48f), color, t * 1.3f);
            dl->AddLine(P(-0.15f, 0.48f), P(0.65f, -0.5f), color, t * 1.3f);
            break;
        case Icon::Cross:
            dl->AddLine(P(-0.5f, -0.5f), P(0.5f, 0.5f), color, t * 1.3f);
            dl->AddLine(P(0.5f, -0.5f), P(-0.5f, 0.5f), color, t * 1.3f);
            break;
        case Icon::Sparkle: {  // four-pointed star
            ImVec2 pts[8];
            for (int i = 0; i < 8; ++i) {
                float a = kPi / 4.0f * static_cast<float>(i) - kPi / 2.0f;
                float r = (i % 2 == 0) ? h : h * 0.28f;
                pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
            }
            dl->AddConvexPolyFilled(pts, 8, color);
            break;
        }
        case Icon::SpeakerOn:
        case Icon::SpeakerOff: {
            ImVec2 body[6] = {P(-0.85f, -0.3f), P(-0.45f, -0.3f), P(0.05f, -0.75f),
                              P(0.05f, 0.75f), P(-0.45f, 0.3f), P(-0.85f, 0.3f)};
            dl->AddConvexPolyFilled(body, 6, color);
            if (icon == Icon::SpeakerOn) {
                dl->PathArcTo(P(0.05f, 0.0f), h * 0.42f, -0.8f, 0.8f, 10);
                dl->PathStroke(color, 0, t);
                dl->PathArcTo(P(0.05f, 0.0f), h * 0.78f, -0.8f, 0.8f, 14);
                dl->PathStroke(color, 0, t);
            } else {
                dl->AddLine(P(0.35f, -0.3f), P(0.9f, 0.3f), color, t);
                dl->AddLine(P(0.9f, -0.3f), P(0.35f, 0.3f), color, t);
            }
            break;
        }
        case Icon::Jet:
            drawJet(dl, c + ImVec2(0, h * 0.28f), h * 1.0f, 0.0f, color);
            break;
        case Icon::Table:
            dl->AddRect(P(-0.9f, -0.8f), P(0.9f, 0.8f), color, 2.0f, 0, t);
            dl->AddLine(P(-0.9f, -0.27f), P(0.9f, -0.27f), color, t);
            dl->AddLine(P(-0.9f, 0.27f), P(0.9f, 0.27f), color, t);
            dl->AddLine(P(-0.3f, -0.8f), P(-0.3f, 0.8f), color, t);
            dl->AddLine(P(0.3f, -0.8f), P(0.3f, 0.8f), color, t);
            break;
        case Icon::Gauge:
            dl->AddCircle(c, h * 0.9f, color, 0, t);
            dl->AddLine(P(-0.9f, 0.12f), P(0.9f, -0.2f), color, t);
            dl->AddLine(P(-0.42f, 0.18f), P(0.42f, 0.18f), color, t * 1.2f);
            dl->AddCircleFilled(P(0.0f, 0.18f), h * 0.1f, color);
            break;
        case Icon::Cube: {
            ImVec2 top = P(0.0f, -0.95f), l = P(-0.85f, -0.5f), r = P(0.85f, -0.5f), m = P(0.0f, -0.05f);
            ImVec2 lb = P(-0.85f, 0.45f), rb = P(0.85f, 0.45f), b = P(0.0f, 0.95f);
            dl->AddLine(top, l, color, t); dl->AddLine(top, r, color, t); dl->AddLine(l, m, color, t);
            dl->AddLine(r, m, color, t); dl->AddLine(m, b, color, t); dl->AddLine(l, lb, color, t);
            dl->AddLine(r, rb, color, t); dl->AddLine(lb, b, color, t); dl->AddLine(rb, b, color, t);
            break;
        }
        case Icon::Bolt:  // lightning: two overlapping triangles
            dl->AddTriangleFilled(P(0.3f, -0.95f), P(-0.55f, 0.15f), P(0.15f, 0.0f), color);
            dl->AddTriangleFilled(P(-0.3f, 0.95f), P(0.55f, -0.15f), P(-0.15f, 0.0f), color);
            dl->AddTriangleFilled(P(-0.55f, 0.15f), P(0.55f, -0.15f), P(0.15f, 0.0f), color);
            break;
    }
}

// ================================================================ text

void textIn(ImFont* font, ImVec4 color, const char* text) {
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void wrappedIn(ImFont* font, ImVec4 color, const char* text) {
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void spacedLabel(const char* text, ImVec4 color, float spacing, ImFont* font) {
    if (!font) font = fonts.label;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x;
    const ImU32 c = u32(color);
    for (const char* s = text; *s;) {
        // length of this UTF-8 character
        unsigned char b = static_cast<unsigned char>(*s);
        int len = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : 4;
        textAt(dl, font, ImVec2(x, p.y), c, s, s + len);
        x += measure(font, s, s + len).x + spacing;
        s += len;
    }
    ImGui::Dummy(ImVec2(x - p.x, fontPx(font)));
}

// ================================================================ building blocks

void cardShape(ImDrawList* dl, ImVec2 p, ImVec2 size, float rounding, bool lifted, ImVec4 border,
               float borderWidth) {
    // Soft shadow: a few faint, slightly larger rounded rectangles below the card
    const ImVec4 shadow(0.086f, 0.122f, 0.235f, lifted ? 0.05f : 0.028f);
    for (int i = 1; i <= 4; ++i) {
        float g = 1.5f * static_cast<float>(i);
        float drop = (lifted ? 3.0f : 1.5f) * static_cast<float>(i);
        dl->AddRectFilled(ImVec2(p.x - g * 0.5f, p.y - g * 0.3f + drop), ImVec2(p.x + size.x + g * 0.5f, p.y + size.y + g + drop),
                          u32(shadow), rounding + g);
    }
    dl->AddRectFilled(p, p + size, u32(col::Card), rounding);
    if (borderWidth > 0) dl->AddRect(p, p + size, u32(border), rounding, 0, borderWidth);
}

void gradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 from, ImVec4 to, float rounding) {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
    const int v1 = dl->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, v1, a, b, u32(from), u32(to));
}

void beginCard(const char* id, ImVec2 size, bool scroll, ImVec2 padding) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (size.x <= 0) size.x = avail.x + size.x;
    if (size.y <= 0) size.y = avail.y + size.y;
    cardShape(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding,
                      scroll ? 0 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
    ImGui::PopStyleVar();
}

void endCard() { ImGui::EndChild(); }

// ================================================================ widgets

bool button(const char* label, ButtonStyle style, ImVec2 size, ImFont* font) {
    if (!font) font = fonts.strong;
    const char* end = labelEnd(label);
    ImVec2 ts = measure(font, label, end);
    if (size.x <= 0) size.x = ts.x + 40.0f;
    if (size.y <= 0) size.y = ts.y + 22.0f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, size);
    bool hov = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    handCursorIfHovered();
    if (held) p.y += 1.0f;  // pressed: nudge down a pixel

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 textCol = 0;
    const float r = 10.0f;
    switch (style) {
        case ButtonStyle::Primary:
            dl->AddRectFilled(p + ImVec2(0, 2), p + size + ImVec2(0, 2), u32(col::Accent, 0.25f), r);
            dl->AddRectFilled(p, p + size, u32(hov ? col::AccentHover : col::Accent), r);
            textCol = u32(col::White);
            break;
        case ButtonStyle::Outline:
            dl->AddRectFilled(p, p + size, u32(hov ? col::AccentSoft : col::Card), r);
            dl->AddRect(p, p + size, u32(hov ? col::Accent : ImVec4(0.80f, 0.835f, 0.902f, 1.0f)), r, 0, 1.3f);
            textCol = u32(hov ? col::Accent : col::Ink);
            break;
        case ButtonStyle::White:
            dl->AddRectFilled(p + ImVec2(0, 3), p + size + ImVec2(0, 3), IM_COL32(10, 20, 60, 50), r);
            dl->AddRectFilled(p, p + size, u32(hov ? ImVec4(0.93f, 0.95f, 1.0f, 1.0f) : col::White), r);
            textCol = u32(ImVec4(0.153f, 0.263f, 0.655f, 1.0f));
            break;
        case ButtonStyle::Danger:
            dl->AddRectFilled(p, p + size, u32(hov ? col::BadSoft : col::Card), r);
            dl->AddRect(p, p + size, u32(col::Bad, hov ? 1.0f : 0.7f), r, 0, 1.3f);
            textCol = u32(col::Bad);
            break;
    }
    textCentered(dl, font, p + size * 0.5f, textCol, label, end);
    return clicked;
}

bool chip(const char* id, const char* text, ImVec4 bg, ImVec4 fg, bool clickable, bool bordered, Icon* icon) {
    ImVec2 ts = measure(fonts.strong, text);
    const float iconW = icon ? 22.0f : 0.0f;
    ImVec2 size(ts.x + 30 + iconW, ts.y + 16);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = false, hov = false;
    if (clickable) {
        clicked = ImGui::InvisibleButton(id, size);
        hov = ImGui::IsItemHovered();
        handCursorIfHovered();
    } else {
        ImGui::Dummy(size);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, u32(hov ? col::AccentSoft : bg), size.y * 0.5f);
    if (bordered) dl->AddRect(p, p + size, u32(hov ? col::Accent : col::CardBorder), size.y * 0.5f, 0, 1.2f);
    if (icon) drawIcon(dl, *icon, ImVec2(p.x + 15 + 7, p.y + size.y * 0.5f), 14.0f, u32(fg));
    textAt(dl, fonts.strong, ImVec2(p.x + 15 + iconW, p.y + (size.y - ts.y) * 0.5f), u32(fg), text);
    return clicked;
}

bool navItem(const char* id, const char* label, Icon icon, bool selected, float width, const char* badge) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width, 50);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    handCursorIfHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) {
        dl->AddRectFilled(p, p + size, u32(col::SidebarSel), 10.0f);
        dl->AddRectFilled(p, ImVec2(p.x + 4, p.y + size.y), u32(ImVec4(0.55f, 0.70f, 1.0f, 1.0f)), 10.0f,
                          ImDrawFlags_RoundCornersLeft);
    } else if (hov) {
        dl->AddRectFilled(p, p + size, u32(col::SidebarHover), 10.0f);
    }
    const ImU32 c = u32(selected || hov ? col::White : col::SidebarText);
    drawIcon(dl, icon, ImVec2(p.x + 28, p.y + size.y * 0.5f), 17.0f, c);
    ImFont* f = selected ? fonts.strong : fonts.body;
    textAt(dl, f, ImVec2(p.x + 54, p.y + (size.y - fontPx(f)) * 0.5f), c, label);

    if (badge && *badge) {
        ImVec2 bs = measure(fonts.label, badge);
        ImVec2 a(p.x + width - bs.x - 30, p.y + (size.y - bs.y - 8) * 0.5f);
        ImVec2 b(p.x + width - 14, a.y + bs.y + 8);
        dl->AddRectFilled(a, b, u32(col::Bad), (b.y - a.y) * 0.5f);
        textAt(dl, fonts.label, ImVec2(a.x + 8, a.y + 4), u32(col::White), badge);
    }
    return clicked;
}

void statCard(ImVec2 size, const char* label, const char* value, const char* caption, ImVec4 valueColor) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    cardShape(dl, p, size);
    float y = p.y + 22;
    textAt(dl, fonts.strong, ImVec2(p.x + 24, y), u32(col::InkSoft), label);
    y += fontPx(fonts.strong) + 12;
    ImFont* vf = measure(fonts.big, value).x > size.x - 40 ? fonts.heading : fonts.big;
    textAt(dl, vf, ImVec2(p.x + 24, y), u32(valueColor), value);
    y += fontPx(fonts.big) + 12;
    textAt(dl, fonts.small, ImVec2(p.x + 24, y), u32(col::Muted), caption);
}

void iconTile(ImDrawList* dl, ImVec2 p, float size, Tint t, const char* glyph, Icon* icon) {
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), u32(t.soft), size * 0.24f);
    ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    if (icon) drawIcon(dl, *icon, c, size * 0.46f, u32(t.strong));
    else textCentered(dl, fonts.heading, c, u32(t.strong), glyph);
}

bool answerCard(const char* id, char letter, const char* text, AnswerState state, float width, float anim) {
    const float wrap = width - 92.0f;
    ImVec2 ts = textSize(fonts.body, text, wrap);
    ImVec2 size(width, std::max(58.0f, ts.y + 32.0f));
    ImVec2 p = ImGui::GetCursorScreenPos();

    const bool interactive = state == AnswerState::Normal;
    bool clicked = ImGui::InvisibleButton(id, size) && interactive;
    bool hov = ImGui::IsItemHovered() && interactive;
    if (interactive) handCursorIfHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 fill = col::Card, edge = col::CardBorder, textCol = col::Ink;
    float edgeW = 1.3f;
    switch (state) {
        case AnswerState::Normal:
            if (hov) { fill = col::AccentSoft; edge = col::Accent; edgeW = 1.8f; }
            break;
        case AnswerState::Correct: fill = col::GoodSoft; edge = col::Good; edgeW = 2.0f; break;
        case AnswerState::Wrong: fill = col::BadSoft; edge = col::Bad; edgeW = 2.0f; break;
        case AnswerState::Dimmed: textCol = col::Muted; break;
    }

    // Correct answer: a glow ring that expands outward and fades
    const bool animating = anim >= 0.0f && anim < 1.0f;
    if (animating && state == AnswerState::Correct) {
        float e = easeOut(anim / 0.7f) * 14.0f;
        float a = std::max(0.0f, 1.0f - anim / 0.7f) * 0.5f;
        dl->AddRect(p - ImVec2(e, e), p + size + ImVec2(e, e), u32(col::Good, a), 12.0f + e, 0, 3.0f);
    }
    dl->AddRectFilled(p, p + size, u32(fill), 12.0f);
    dl->AddRect(p, p + size, u32(edge), 12.0f, 0, edgeW);

    // Letter badge (a check or an X after answering), with a "pop" when it appears
    ImVec2 bc(p.x + 32, p.y + size.y * 0.5f);
    float r = 15.0f;
    if (animating && (state == AnswerState::Correct || state == AnswerState::Wrong))
        r *= 1.0f + 0.4f * std::sin(kPi * std::clamp(anim / 0.3f, 0.0f, 1.0f));
    const char l[2] = {letter, 0};
    if (state == AnswerState::Correct || state == AnswerState::Wrong) {
        dl->AddCircleFilled(bc, r, u32(edge));
        drawIcon(dl, state == AnswerState::Correct ? Icon::Check : Icon::Cross, bc, r * 1.1f, u32(col::White));
    } else if (hov) {
        dl->AddCircleFilled(bc, r, u32(col::Accent));
        textCentered(dl, fonts.label, bc, u32(col::White), l);
    } else {
        dl->AddCircleFilled(bc, r, u32(col::Bg));
        dl->AddCircle(bc, r, u32(ImVec4(0.80f, 0.835f, 0.902f, 1.0f)), 0, 1.3f);
        textCentered(dl, fonts.label, bc, u32(state == AnswerState::Dimmed ? col::Muted : col::InkSoft), l);
    }
    textAt(dl, fonts.body, ImVec2(p.x + 64, p.y + (size.y - ts.y) * 0.5f), u32(textCol), text, nullptr, wrap);

    // Confetti burst from the badge, drawn on top of everything
    if (animating && state == AnswerState::Correct) celebrate(ImGui::GetForegroundDrawList(), bc, anim);
    return clicked;
}

void celebrate(ImDrawList* dl, ImVec2 center, float t) {
    const float dur = 0.95f;
    if (t < 0 || t > dur) return;
    const ImVec4 colors[] = {col::Good, col::Accent, col::Gold, ImVec4(0.93f, 0.36f, 0.55f, 1.0f)};
    const float k = t / dur;
    const float fade = 1.0f - k * k;
    for (int i = 0; i < 22; ++i) {
        // fixed pseudo-random numbers per particle, so the burst looks scattered
        float rnd = std::fmod(std::sin(static_cast<float>(i) * 12.9898f) * 43758.5453f, 1.0f);
        rnd = std::fabs(rnd);
        float angle = static_cast<float>(i) * (2.0f * kPi / 22.0f) + 0.35f * std::sin(static_cast<float>(i) * 7.3f);
        float dist = (55.0f + 55.0f * rnd) * easeOut(k * 1.25f);
        ImVec2 q(center.x + std::cos(angle) * dist, center.y + std::sin(angle) * dist + 70.0f * k * k);
        ImU32 c = u32(colors[i % 4], fade);
        float s = 4.2f * (1.0f - 0.6f * k);
        if (i % 3 == 0) {
            dl->AddCircleFilled(q, s * 0.9f, c);
        } else {  // little spinning confetti squares
            float rot = angle + k * 9.0f * (rnd + 0.5f);
            ImVec2 d1(std::cos(rot) * s, std::sin(rot) * s), d2(-d1.y * 0.55f, d1.x * 0.55f);
            dl->AddQuadFilled(q + d1 + d2, q + d1 - d2, q - d1 - d2, q - d1 + d2, c);
        }
    }
    // a few sparkles that twinkle near the center
    for (int i = 0; i < 3; ++i) {
        float a = 0.6f + static_cast<float>(i) * 2.1f;
        ImVec2 q(center.x + std::cos(a) * 34.0f, center.y + std::sin(a) * 30.0f);
        float s = 13.0f * std::sin(kPi * std::min(1.0f, k * 1.6f));
        if (s > 0.5f) drawIcon(dl, Icon::Sparkle, q, s, u32(col::Gold, fade));
    }
}

bool toggle(const char* id, bool* value) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(46, 26);
    bool clicked = ImGui::InvisibleButton(id, size);
    handCursorIfHovered();
    if (clicked) *value = !*value;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    dl->AddRectFilled(p, p + size, u32(*value ? col::Accent : ImVec4(0.80f, 0.835f, 0.902f, 1.0f)), r);
    ImVec2 knob(*value ? p.x + size.x - r : p.x + r, p.y + r);
    dl->AddCircleFilled(knob + ImVec2(0, 1), r - 3, IM_COL32(0, 0, 0, 30));
    dl->AddCircleFilled(knob, r - 3, u32(col::White));
    return clicked;
}

void thinBar(float fraction, float width, ImVec4 color, float height) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), u32(col::Track), height * 0.5f);
    if (fraction > 0)
        dl->AddRectFilled(p, ImVec2(p.x + std::max(height, width * fraction), p.y + height), u32(color), height * 0.5f);
}

void divider(ImVec4 color) {
    ImGui::Dummy(ImVec2(0, 2));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y), u32(color), 1.0f);
    ImGui::Dummy(ImVec2(0, 4));
}

namespace {
ImVec2 g_calloutStart;
}

void beginCallout() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);  // content on top; the box is drawn underneath at the end
    g_calloutStart = ImGui::GetCursorScreenPos();
    ImGui::Indent(24);
    ImGui::Dummy(ImVec2(0, 6));
}

void endCallout(ImVec4 soft, ImVec4 strong) {
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Unindent(24);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + 6;
    float right = g_calloutStart.x + ImGui::GetContentRegionAvail().x;
    dl->ChannelsSetCurrent(0);
    ImVec2 a(g_calloutStart.x, g_calloutStart.y - 6), b(right, bottom);
    dl->AddRectFilled(a, b, u32(soft), 12.0f);
    dl->AddRectFilled(a, ImVec2(a.x + 5, b.y), u32(strong), 12.0f, ImDrawFlags_RoundCornersLeft);
    dl->ChannelsMerge();
    ImGui::Dummy(ImVec2(0, 6));
}

}  // namespace ui
