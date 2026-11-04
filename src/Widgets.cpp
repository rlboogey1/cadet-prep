#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "Widgets.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>

namespace ui {

FontSet fonts;

namespace {

ImU32 u32(ImVec4 c, float alphaMul = 1.0f) {
    c.w *= alphaMul;
    return ImGui::ColorConvertFloat4ToU32(c);
}

float px(ImFont* f) { return f->FontSize * ImGui::GetIO().FontGlobalScale; }

// The visible part of a label: "Quit##quiz" shows as "Quit".
const char* labelEnd(const char* label) {
    const char* hash = std::strstr(label, "##");
    return hash ? hash : label + std::strlen(label);
}

void textAt(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 color, const char* text,
            const char* end = nullptr) {
    dl->AddText(f, px(f), pos, color, text, end);
}

// Draws text horizontally centered on `cx`.
void textCentered(ImDrawList* dl, ImFont* f, float cx, float y, ImU32 color, const char* text,
                  const char* end = nullptr) {
    ImVec2 s = f->CalcTextSizeA(px(f), FLT_MAX, 0.0f, text, end);
    textAt(dl, f, ImVec2(std::floor(cx - s.x * 0.5f), y), color, text, end);
}

void handCursorIfHovered() {
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

const char* firstExisting(std::initializer_list<const char*> paths) {
    for (const char* p : paths)
        if (std::ifstream(p).good()) return p;
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- setup

void loadFonts(ImGuiIO& io, float scale) {
    // Character ranges: Latin, Greek (pi), punctuation and dashes, superscripts,
    // arrows, and math symbols (square root, minus sign).
    static const ImWchar fullRanges[] = {0x0020, 0x00FF, 0x0370, 0x03FF, 0x2000, 0x206F,
                                         0x2070, 0x209F, 0x2190, 0x21FF, 0x2200, 0x22FF, 0};
    static const ImWchar basicRanges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2022, 0x2022, 0};

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
    // Fills in symbols the main font lacks (on macOS, Arial Unicode has them all).
    const char* symbols = firstExisting({
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "C:\\Windows\\Fonts\\seguisym.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    });

    if (!regular) {  // no system fonts found: use ImGui's built-in font everywhere
        ImFont* f = io.Fonts->AddFontDefault();
        fonts = {f, f, f, f, f, f};
        io.FontGlobalScale = 1.3f;
        return;
    }

    auto add = [&](const char* path, float size, const ImWchar* ranges) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, size * scale, nullptr, ranges);
        if (ranges == fullRanges && symbols && std::strcmp(symbols, path) != 0) {
            ImFontConfig merge;
            merge.MergeMode = true;  // only adds characters the main font is missing
            io.Fonts->AddFontFromFileTTF(symbols, size * scale, &merge, ranges);
        }
        return f;
    };

    fonts.body = add(regular, 17.0f, fullRanges);
    fonts.small = add(regular, 14.0f, fullRanges);
    fonts.heading = add(regular, 21.0f, fullRanges);
    fonts.title = add(regular, 34.0f, basicRanges);
    fonts.big = add(regular, 40.0f, basicRanges);
    fonts.label = add(bold ? bold : regular, 13.0f, basicRanges);

    io.FontDefault = fonts.body;
    io.FontGlobalScale = 1.0f / scale;  // fonts are rendered at full Retina resolution
}

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(0, 0);
    s.ItemSpacing = ImVec2(12, 12);
    s.FramePadding = ImVec2(12, 8);
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.FrameRounding = 8.0f;
    s.ScrollbarSize = 10.0f;
    s.ScrollbarRounding = 8.0f;
    s.PopupRounding = 10.0f;
    s.PopupBorderSize = 1.0f;
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);  // menu text vertically centered

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = col::Bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = col::Text;
    c[ImGuiCol_TextDisabled] = col::Muted;
    c[ImGuiCol_PopupBg] = col::Panel;
    c[ImGuiCol_Border] = col::Border;
    c[ImGuiCol_Header] = ImVec4(col::Accent.x, col::Accent.y, col::Accent.z, 0.22f);  // selected menu row
    c[ImGuiCol_HeaderHovered] = ImVec4(col::Accent.x, col::Accent.y, col::Accent.z, 0.32f);
    c[ImGuiCol_HeaderActive] = ImVec4(col::Accent.x, col::Accent.y, col::Accent.z, 0.45f);
    c[ImGuiCol_Separator] = col::Line;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(col::Border.x, col::Border.y, col::Border.z, 0.35f);
    c[ImGuiCol_ScrollbarGrabHovered] = col::Border;
    c[ImGuiCol_ScrollbarGrabActive] = col::Accent;
}

// ---------------------------------------------------------------- icons

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 color) {
    const float h = size * 0.5f;
    const float t = std::max(1.6f, size * 0.075f);  // line thickness
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };

    switch (icon) {
        case Icon::Grid: {
            const float q = 0.78f, g = 0.14f;  // square size and gap, in half-sizes
            for (int i = 0; i < 4; ++i) {
                float x0 = (i % 2 == 0) ? -g / 2 - q : g / 2;
                float y0 = (i < 2) ? -g / 2 - q : g / 2;
                dl->AddRect(P(x0, y0), P(x0 + q, y0 + q), color, size * 0.08f, 0, t);
            }
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
        case Icon::Target:
            dl->AddCircle(c, h * 0.9f, color, 0, t);
            dl->AddCircle(c, h * 0.5f, color, 0, t);
            dl->AddCircleFilled(c, h * 0.16f, color);
            break;
        case Icon::Clock:
            dl->AddCircle(c, h * 0.9f, color, 0, t);
            dl->AddLine(c, P(0.0f, -0.55f), color, t);
            dl->AddLine(c, P(0.38f, 0.18f), color, t);
            break;
        case Icon::Review: {
            const float r = h * 0.78f;
            const float a0 = -2.6f, a1 = 2.0f;  // radians: an open circle
            dl->PathArcTo(c, r, a0, a1, 24);
            dl->PathStroke(color, 0, t);
            ImVec2 tip(c.x + std::cos(a0) * r, c.y + std::sin(a0) * r);
            dl->AddTriangleFilled(ImVec2(tip.x - h * 0.42f, tip.y - h * 0.02f),
                                  ImVec2(tip.x + h * 0.18f, tip.y - h * 0.48f),
                                  ImVec2(tip.x + h * 0.22f, tip.y + h * 0.18f), color);
            break;
        }
        case Icon::Battery:  // a stacked list: "all sections"
            for (int i = 0; i < 3; ++i) {
                float y = -0.6f + 0.6f * static_cast<float>(i);
                dl->AddCircleFilled(P(-0.7f, y), h * 0.12f, color);
                dl->AddLine(P(-0.35f, y), P(0.85f, y), color, t);
            }
            break;
    }
}

// ---------------------------------------------------------------- text

ImVec2 textSize(ImFont* font, const char* text, float wrap) {
    return font->CalcTextSizeA(px(font), FLT_MAX, wrap > 0 ? wrap : 0.0f, text);
}

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

void sectionLabel(const char* text) {
    textIn(fonts.label, ImVec4(0.84f, 0.88f, 0.96f, 1.0f), text);
}

// ---------------------------------------------------------------- widgets

bool pill(const char* label, bool filled, ImVec2 size, ImVec4 outline) {
    const char* end = labelEnd(label);
    ImVec2 ts = fonts.body->CalcTextSizeA(px(fonts.body), FLT_MAX, 0.0f, label, end);
    if (size.x <= 0) size.x = ts.x + 40.0f;
    if (size.y <= 0) size.y = ts.y + 20.0f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label, size);
    bool hov = ImGui::IsItemHovered();
    handCursorIfHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 textCol;
    if (filled) {
        dl->AddRectFilled(p, p + size, u32(hov ? col::AccentHover : col::Accent), 8.0f);
        textCol = u32(col::OnAccent);
    } else {
        if (hov) dl->AddRectFilled(p, p + size, u32(outline, 0.14f), 8.0f);
        dl->AddRect(p, p + size, u32(outline, hov ? 1.0f : 0.8f), 8.0f, 0, 1.4f);
        bool plain = outline.x == col::Border.x && outline.y == col::Border.y;
        textCol = u32(plain ? col::Text : outline);
    }
    textCentered(dl, fonts.body, p.x + size.x * 0.5f, p.y + (size.y - ts.y) * 0.5f, textCol,
                 label, end);
    return clicked;
}

bool navItem(const char* id, const char* label, const char* sub, Icon icon, bool selected,
             float width, float height) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width, height);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    handCursorIfHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) {
        dl->AddRectFilled(p, p + size, u32(col::Accent), 8.0f);
    } else {
        if (hov) dl->AddRectFilled(p, p + size, u32(col::CardHover), 8.0f);
        dl->AddRect(p, p + size, u32(hov ? col::Accent : col::Border), 8.0f, 0, 1.2f);
    }
    ImU32 main = u32(selected ? col::OnAccent : col::Text);
    ImU32 dim = selected ? u32(col::OnAccent, 0.75f) : u32(col::Muted);

    drawIcon(dl, icon, ImVec2(p.x + 32, p.y + height * 0.5f), 24.0f, main);

    const float bodyH = px(fonts.body), smallH = px(fonts.small);
    const bool hasSub = sub && *sub;
    float y = p.y + (height - bodyH - (hasSub ? smallH + 2 : 0)) * 0.5f;
    textAt(dl, fonts.body, ImVec2(p.x + 62, y), main, label);
    if (hasSub) textAt(dl, fonts.small, ImVec2(p.x + 62, y + bodyH + 2), dim, sub);
    return clicked;
}

bool card(const char* id, ImVec2 size, const char* iconText, Icon icon, const char* title,
          const char* sub, const char* status, bool highlight, bool disabled, const char* tag) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size) && !disabled;
    bool hov = ImGui::IsItemHovered() && !disabled;
    if (!disabled) handCursorIfHovered();
    const bool lit = (highlight || hov) && !disabled;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, u32(hov ? col::CardHover : col::CardFill), 12.0f);
    dl->AddRect(p, p + size, u32(lit ? col::Accent : col::Border, disabled ? 0.5f : 1.0f),
                12.0f, 0, lit ? 2.2f : 1.2f);
    if (lit) {  // soft outer glow
        dl->AddRect(p - ImVec2(3, 3), p + size + ImVec2(3, 3), u32(col::Accent, 0.18f), 15.0f,
                    0, 3.0f);
    }

    const ImU32 main = u32(lit ? col::Accent : col::Text, disabled ? 0.5f : 1.0f);
    const ImU32 dim = u32(col::Muted, disabled ? 0.6f : 1.0f);

    // A long title gets a smaller font so it fits the card.
    ImFont* tf = fonts.heading;
    if (textSize(tf, title).x > size.x - 24) tf = fonts.body;

    const float ring = 23.0f;
    const float titleH = px(tf), subH = px(fonts.small);
    const bool hasSub = sub && *sub, hasStatus = status && *status;
    float total = ring * 2 + 14 + titleH + (hasSub ? subH + 6 : 0) + (hasStatus ? subH + 4 : 0);
    float y = p.y + (size.y - total) * 0.5f;
    const float cx = p.x + size.x * 0.5f;

    ImVec2 ic(cx, y + ring);
    if (iconText) {
        dl->AddCircle(ic, ring, main, 0, 1.6f);
        float lh = px(fonts.label);
        textCentered(dl, fonts.label, cx, ic.y - lh * 0.5f, main, iconText);
    } else {
        drawIcon(dl, icon, ic, 34.0f, main);
    }
    y += ring * 2 + 14;
    textCentered(dl, tf, cx, y, main, title);
    y += titleH + 6;
    if (hasSub) {
        textCentered(dl, fonts.small, cx, y, dim, sub);
        y += subH + 4;
    }
    if (hasStatus) textCentered(dl, fonts.small, cx, y, lit ? main : dim, status);

    if (tag) {  // small badge in the top-right corner
        ImVec2 ts = textSize(fonts.label, tag);
        ImVec2 a(p.x + size.x - ts.x - 26, p.y + 12), b(p.x + size.x - 12, p.y + 14 + ts.y + 6);
        dl->AddRectFilled(a, b, u32(col::Accent), 5.0f);
        textAt(dl, fonts.label, ImVec2(a.x + 7, a.y + 4), u32(col::OnAccent), tag);
    }
    return clicked;
}

bool answerCard(const char* id, char letter, const char* text, AnswerState state, float width) {
    const float wrap = width - 84.0f;
    ImVec2 ts = textSize(fonts.body, text, wrap);
    ImVec2 size(width, std::max(56.0f, ts.y + 30.0f));
    ImVec2 p = ImGui::GetCursorScreenPos();

    const bool interactive = state == AnswerState::Normal;
    bool clicked = ImGui::InvisibleButton(id, size) && interactive;
    bool hov = ImGui::IsItemHovered() && interactive;
    if (interactive) handCursorIfHovered();

    ImVec4 edge = col::Border, badge = col::Border, textCol = col::Text;
    bool filledBadge = false;
    float fillAlpha = 0.0f;
    switch (state) {
        case AnswerState::Normal:
            if (hov) { edge = col::Accent; badge = col::Accent; filledBadge = true; }
            break;
        case AnswerState::Correct:
            edge = badge = col::Good; filledBadge = true; fillAlpha = 0.12f;
            break;
        case AnswerState::Wrong:
            edge = badge = col::Bad; filledBadge = true; fillAlpha = 0.12f;
            break;
        case AnswerState::Dimmed:
            edge = col::Line; textCol = col::Muted;
            break;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, hov ? u32(col::CardHover) : u32(col::CardFill), 10.0f);
    if (fillAlpha > 0) dl->AddRectFilled(p, p + size, u32(edge, fillAlpha), 10.0f);
    dl->AddRect(p, p + size, u32(edge), 10.0f, 0, state == AnswerState::Normal && !hov ? 1.2f : 2.0f);

    ImVec2 bc(p.x + 30, p.y + size.y * 0.5f);
    char l[2] = {letter, 0};
    float lh = px(fonts.label);
    if (filledBadge) {
        dl->AddCircleFilled(bc, 15, u32(badge));
        textCentered(dl, fonts.label, bc.x, bc.y - lh * 0.5f, u32(col::OnAccent), l);
    } else {
        dl->AddCircle(bc, 15, u32(badge), 0, 1.4f);
        textCentered(dl, fonts.label, bc.x, bc.y - lh * 0.5f, u32(textCol), l);
    }
    dl->AddText(fonts.body, px(fonts.body), ImVec2(p.x + 62, p.y + (size.y - ts.y) * 0.5f),
                u32(textCol), text, nullptr, wrap);
    return clicked;
}

bool toggle(const char* id, bool* value) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(52, 28);
    bool clicked = ImGui::InvisibleButton(id, size);
    handCursorIfHovered();
    if (clicked) *value = !*value;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    if (*value) {
        dl->AddRectFilled(p, p + size, u32(col::Accent, 0.22f), r);
        dl->AddRect(p, p + size, u32(col::Accent, 0.8f), r, 0, 1.4f);
        dl->AddCircleFilled(ImVec2(p.x + size.x - r, p.y + r), r - 5, u32(col::Accent));
    } else {
        dl->AddRectFilled(p, p + size, IM_COL32(0, 0, 0, 40), r);
        dl->AddRect(p, p + size, u32(col::Border), r, 0, 1.4f);
        dl->AddCircleFilled(ImVec2(p.x + r, p.y + r), r - 5, u32(col::Muted));
    }
    return clicked;
}

void statBox(ImVec2 size, const char* value, const char* caption, ImVec4 valueColor) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, u32(col::CardFill), 10.0f);
    dl->AddRect(p, p + size, u32(col::Border, 1.3f), 10.0f, 0, 1.4f);

    const bool hasCaption = caption && *caption;
    // Use the big font unless the value is too wide for the box.
    ImFont* vf = textSize(fonts.big, value).x > size.x - 20 ? fonts.heading : fonts.big;
    float vh = px(vf), ch = hasCaption ? px(fonts.small) + 4 : 0;
    float y = p.y + (size.y - vh - ch) * 0.5f;
    textCentered(dl, vf, p.x + size.x * 0.5f, y, u32(valueColor), value);
    if (hasCaption)
        textCentered(dl, fonts.small, p.x + size.x * 0.5f, y + vh + 4, u32(col::Muted), caption);
}

void thinBar(float fraction, float width, ImVec4 color, float height) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), u32(col::Line, 1.6f), height * 0.5f);
    if (fraction > 0)
        dl->AddRectFilled(p, ImVec2(p.x + std::max(height, width * fraction), p.y + height),
                          u32(color), height * 0.5f);
}

bool listRow(const char* id, const char* left, const char* right, ImVec4 rightColor,
             bool clickable) {
    const float width = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width, 40);
    bool clicked = false, hov = false;
    if (clickable) {
        clicked = ImGui::InvisibleButton(id, size);
        hov = ImGui::IsItemHovered();
        handCursorIfHovered();
    } else {
        ImGui::Dummy(size);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = px(fonts.body);
    textAt(dl, fonts.body, ImVec2(p.x, p.y + (size.y - th) * 0.5f),
           u32(hov ? col::Accent : col::Text), left);

    float rightEdge = p.x + width - (clickable ? 22 : 0);
    if (right && *right) {
        ImVec2 rs = textSize(fonts.body, right);
        textAt(dl, fonts.body, ImVec2(rightEdge - rs.x, p.y + (size.y - th) * 0.5f),
               u32(rightColor), right);
    }
    if (clickable) {  // chevron ">"
        ImVec2 c(p.x + width - 6, p.y + size.y * 0.5f);
        ImU32 cc = u32(hov ? col::Accent : col::Muted);
        dl->AddLine(ImVec2(c.x - 4, c.y - 5), c, cc, 1.6f);
        dl->AddLine(c, ImVec2(c.x - 4, c.y + 5), cc, 1.6f);
    }
    dl->AddLine(ImVec2(p.x, p.y + size.y), ImVec2(p.x + width, p.y + size.y), u32(col::Line), 1.0f);
    return clicked;
}

void divider() {
    ImGui::Dummy(ImVec2(0, 4));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y), u32(col::Line), 1.0f);
    ImGui::Dummy(ImVec2(0, 8));
}

void beginPanel(const char* id, ImVec2 size, bool filled, bool scroll) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (size.x <= 0) size.x = avail.x + size.x;
    if (size.y <= 0) size.y = avail.y + size.y;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (filled) dl->AddRectFilled(p, p + size, u32(col::Panel), 12.0f);
    else dl->AddRect(p, p + size, u32(col::Border, 1.2f), 12.0f, 0, 1.4f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 24));
    ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding,
                      scroll ? 0 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
    ImGui::PopStyleVar();
}

void endPanel() { ImGui::EndChild(); }

// ---------------------------------------------------------------- callout

namespace {
ImVec2 g_calloutStart;
}

void beginCallout() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);  // draw content on top; the box goes underneath later
    g_calloutStart = ImGui::GetCursorScreenPos();
    ImGui::Indent(22);
    ImGui::Dummy(ImVec2(0, 4));
}

void endCallout(ImVec4 color) {
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Unindent(22);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + 6;
    float right = g_calloutStart.x + ImGui::GetContentRegionAvail().x;
    dl->ChannelsSetCurrent(0);
    ImVec2 a(g_calloutStart.x, g_calloutStart.y - 6), b(right, bottom);
    dl->AddRectFilled(a, b, u32(color, 0.12f), 10.0f);
    dl->AddRectFilled(a, ImVec2(a.x + 4, b.y), u32(color), 10.0f, ImDrawFlags_RoundCornersLeft);
    dl->ChannelsMerge();
    ImGui::Dummy(ImVec2(0, 6));
}

}  // namespace ui
