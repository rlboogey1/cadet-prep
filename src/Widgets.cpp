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
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <CoreText/CoreText.h>
#define CADET_IOS_FONTS 1
#endif
#endif

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

#ifdef CADET_IOS_FONTS
// iOS keeps its fonts in system folders that move between versions, so ask Core
// Text where a font lives (by PostScript name). Empty if it isn't installed.
std::string systemFontPath(const char* psName) {
    std::string out;
    CFStringRef name = CFStringCreateWithCString(nullptr, psName, kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(name, 16.0, nullptr);
    CFStringRef actual = CTFontCopyPostScriptName(font);  // a missing font gives a stand-in
    if (CFStringCompare(actual, name, 0) == kCFCompareEqualTo) {
        if (CFURLRef url = static_cast<CFURLRef>(CTFontCopyAttribute(font, kCTFontURLAttribute))) {
            char buf[1024];
            if (CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<UInt8*>(buf), sizeof buf)) out = buf;
            CFRelease(url);
        }
    }
    CFRelease(actual);
    CFRelease(font);
    CFRelease(name);
    return out;
}
#endif

bool gCompact = false;  // phone layout (see setDevice)
bool gTouch = false;

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

namespace {
bool gDark = false;
ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}
}  // namespace

bool darkMode() { return gDark; }

TextKind gTextKind = TextKind::None;  // set by textField() while it has focus

void setDevice(bool compact, bool touch) {
    gCompact = compact;
    gTouch = touch;
    gTextKind = TextKind::None;  // a field that has focus sets it again while drawing
}
TextKind activeTextKind() { return gTextKind; }
bool compact() { return gCompact; }
bool touch() { return gTouch; }

void setDarkMode(bool dark) {
    gDark = dark;
    auto set = [](ImVec4& c, ImVec4 light, ImVec4 darkC) { c = gDark ? darkC : light; };
    // Briefing palette: neutral grays, USAF ultramarine for actions, amber only for
    // "needs attention", muted green and red only for right and wrong answers.
    set(col::Bg, {0.957f, 0.961f, 0.969f, 1}, {0.043f, 0.063f, 0.094f, 1});
    set(col::Card, {1.0f, 1.0f, 1.0f, 1}, {0.071f, 0.098f, 0.141f, 1});
    set(col::CardBorder, {0.851f, 0.867f, 0.890f, 1}, {0.161f, 0.200f, 0.263f, 1});
    set(col::Ink, {0.067f, 0.094f, 0.153f, 1}, {0.902f, 0.914f, 0.933f, 1});
    set(col::InkSoft, {0.227f, 0.263f, 0.322f, 1}, {0.718f, 0.745f, 0.788f, 1});
    set(col::Muted, {0.420f, 0.447f, 0.502f, 1}, {0.541f, 0.576f, 0.635f, 1});
    set(col::Track, {0.890f, 0.902f, 0.918f, 1}, {0.118f, 0.149f, 0.200f, 1});
    set(col::Accent, {0.0f, 0.188f, 0.561f, 1}, {0.416f, 0.561f, 0.910f, 1});       // ultramarine #00308F
    set(col::AccentHover, {0.0f, 0.149f, 0.439f, 1}, {0.518f, 0.639f, 0.933f, 1});
    set(col::AccentSoft, {0.902f, 0.925f, 0.969f, 1}, {0.082f, 0.137f, 0.247f, 1});
    set(col::OnAccent, {1, 1, 1, 1}, {0.039f, 0.063f, 0.141f, 1});  // dark text on the light dark-mode accent
    set(col::HeroA, {0.0f, 0.188f, 0.561f, 1}, {0.059f, 0.133f, 0.290f, 1});         // flat: no gradient
    set(col::HeroB, {0.0f, 0.188f, 0.561f, 1}, {0.059f, 0.133f, 0.290f, 1});
    set(col::Good, {0.184f, 0.490f, 0.310f, 1}, {0.373f, 0.698f, 0.514f, 1});
    set(col::GoodSoft, {0.910f, 0.949f, 0.925f, 1}, {0.071f, 0.149f, 0.106f, 1});
    set(col::Bad, {0.698f, 0.227f, 0.227f, 1}, {0.878f, 0.443f, 0.435f, 1});
    set(col::BadSoft, {0.969f, 0.914f, 0.910f, 1}, {0.173f, 0.082f, 0.090f, 1});
    set(col::Gold, {0.784f, 0.506f, 0.102f, 1}, {0.886f, 0.631f, 0.290f, 1});
    set(col::Ring, {0.765f, 0.788f, 0.824f, 1}, {0.212f, 0.255f, 0.329f, 1});
    set(col::TagSoft, {0.984f, 0.945f, 0.882f, 1}, {0.176f, 0.129f, 0.075f, 1});    // amber #C8811A
    set(col::TagStrong, {0.784f, 0.506f, 0.102f, 1}, {0.886f, 0.631f, 0.290f, 1});
    set(col::Footer, {0.290f, 0.322f, 0.384f, 1}, {0.604f, 0.639f, 0.694f, 1});
    set(col::CalloutEdge, {0.722f, 0.753f, 0.800f, 1}, {0.227f, 0.278f, 0.361f, 1});
    set(col::Shadow, {0.067f, 0.094f, 0.153f, 1}, {0.0f, 0.0f, 0.0f, 1});
    applyTheme();
}

void loadFonts(ImGuiIO& io, float scale, const std::string& fontDir) {
    // Latin, Greek (pi), punctuation, superscripts and subscripts, arrows, math symbols
    static const ImWchar fullRanges[] = {0x0020, 0x00FF, 0x0370, 0x03FF, 0x2000, 0x206F, 0x2070, 0x209F,
                                         0x2190, 0x21FF, 0x2200, 0x22FF, 0};
    static const ImWchar basicRanges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x201D, 0x2022, 0x2022, 0x2026, 0x2026, 0x2190, 0x2193, 0x2212, 0x2212, 0};

    // Bundled IBM Plex (data/fonts); the system's symbol font fills in arrows and math.
    const std::string plexRegular = fontDir + "/IBMPlexSans-Regular.ttf", plexSemi = fontDir + "/IBMPlexSans-SemiBold.ttf",
                      plexBold = fontDir + "/IBMPlexSans-Bold.ttf", plexMono = fontDir + "/IBMPlexMono-Regular.ttf",
                      plexMonoSemi = fontDir + "/IBMPlexMono-SemiBold.ttf";
    const bool plex = !fontDir.empty() && std::ifstream(plexRegular).good() && std::ifstream(plexSemi).good() &&
                      std::ifstream(plexBold).good() && std::ifstream(plexMono).good() && std::ifstream(plexMonoSemi).good();

#ifdef CADET_IOS_FONTS
    // iPhone and iPad have Arial too; Menlo fills in arrows and math symbols.
    static const std::string iosRegular = systemFontPath("ArialMT"), iosBold = systemFontPath("Arial-BoldMT");
    static std::string iosSymbols;
    for (const char* n : {"ArialUnicodeMS", "AppleSymbols", "Menlo-Regular"})
        if (iosSymbols.empty()) iosSymbols = systemFontPath(n);
    const char* regular = iosRegular.empty() ? nullptr : iosRegular.c_str();
    const char* bold = iosBold.empty() ? nullptr : iosBold.c_str();
    const char* symbols = iosSymbols.empty() ? nullptr : iosSymbols.c_str();
#else
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
#endif

    if (!regular && !plex) {  // no fonts at all: ImGui's built-in font everywhere
        ImFont* f = io.Fonts->AddFontDefault();
        fonts = {f, f, f, f, f, f, f, f, f, f, f};
        io.FontGlobalScale = 1.3f;
        return;
    }
    if (!regular) regular = plexRegular.c_str();  // Plex only; keeps the symbol check below working
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

    if (plex) {
        fonts.body = add(plexRegular.c_str(), 15.5f, fullRanges);
        fonts.small = add(plexRegular.c_str(), 13.0f, fullRanges);
        fonts.strong = add(plexSemi.c_str(), 15.5f, fullRanges);
        fonts.label = add(plexMonoSemi.c_str(), 11.5f, basicRanges);
        fonts.prompt = add(plexRegular.c_str(), 19.5f, fullRanges);
        fonts.heading = add(plexSemi.c_str(), 19.0f, fullRanges);
        fonts.title = add(plexBold.c_str(), 26.0f, basicRanges);
        fonts.big = add(plexMonoSemi.c_str(), 28.0f, basicRanges);
        fonts.hero = add(plexBold.c_str(), 40.0f, basicRanges);
        fonts.mono = add(plexMono.c_str(), 14.5f, basicRanges);
        fonts.monoStrong = add(plexMonoSemi.c_str(), 14.5f, basicRanges);
    } else {
        fonts.body = add(regular, 16.0f, fullRanges);
        fonts.small = add(regular, 13.5f, fullRanges);
        fonts.strong = add(bold, 16.0f, fullRanges);
        fonts.label = add(bold, 12.5f, basicRanges);
        fonts.prompt = add(regular, 20.0f, fullRanges);
        fonts.heading = add(bold, 21.0f, fullRanges);
        fonts.title = add(bold, 28.0f, basicRanges);
        fonts.big = add(bold, 30.0f, basicRanges);
        fonts.hero = add(bold, 42.0f, basicRanges);
        fonts.mono = fonts.body;
        fonts.monoStrong = fonts.strong;
    }

    io.FontDefault = fonts.body;
    io.FontGlobalScale = 1.0f / scale;  // fonts are rendered at full Retina resolution
}

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    if (gDark) ImGui::StyleColorsDark(&s);
    else ImGui::StyleColorsLight(&s);
    s.WindowPadding = ImVec2(0, 0);
    s.ItemSpacing = ImVec2(12, 12);
    s.FramePadding = ImVec2(12, 8);
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.PopupRounding = kRadius;
    s.PopupBorderSize = 1.0f;
    s.FrameRounding = kRadius;
    s.ScrollbarSize = 8.0f;
    s.ScrollbarRounding = 2.0f;
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
    c[ImGuiCol_HeaderHovered] = mix(col::AccentSoft, col::Accent, 0.12f);
    c[ImGuiCol_HeaderActive] = mix(col::AccentSoft, col::Accent, 0.22f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(col::Ring.x, col::Ring.y, col::Ring.z, 0.8f);
    c[ImGuiCol_ScrollbarGrabHovered] = col::Muted;
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
        case Icon::Shield: {
            ImVec2 pts[] = {P(0.0f, -0.95f), P(0.8f, -0.62f), P(0.72f, 0.15f), P(0.0f, 0.95f), P(-0.72f, 0.15f), P(-0.8f, -0.62f)};
            dl->AddPolyline(pts, 6, color, ImDrawFlags_Closed, t);
            dl->AddLine(P(-0.32f, 0.0f), P(-0.05f, 0.3f), color, t);
            dl->AddLine(P(-0.05f, 0.3f), P(0.38f, -0.28f), color, t);
            break;
        }
        case Icon::Star: {  // five-pointed outline
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) {
                float a = kPi / 5.0f * static_cast<float>(i) - kPi / 2.0f;
                float r = (i % 2 == 0) ? h * 0.95f : h * 0.4f;
                pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r + h * 0.06f);
            }
            dl->AddPolyline(pts, 10, color, ImDrawFlags_Closed, t);
            break;
        }
        case Icon::Book:
            dl->AddRect(P(-0.9f, -0.75f), P(0.0f, 0.75f), color, 2.0f, 0, t);
            dl->AddRect(P(0.0f, -0.75f), P(0.9f, 0.75f), color, 2.0f, 0, t);
            dl->AddLine(P(-0.65f, -0.3f), P(-0.25f, -0.3f), color, t * 0.8f);
            dl->AddLine(P(0.25f, -0.3f), P(0.65f, -0.3f), color, t * 0.8f);
            dl->AddLine(P(-0.65f, 0.15f), P(-0.25f, 0.15f), color, t * 0.8f);
            dl->AddLine(P(0.25f, 0.15f), P(0.65f, 0.15f), color, t * 0.8f);
            break;
        case Icon::Flag:
            dl->AddLine(P(-0.7f, -0.9f), P(-0.7f, 0.95f), color, t);
            dl->AddRect(P(-0.7f, -0.85f), P(0.85f, 0.15f), color, 2.0f, 0, t);
            dl->AddLine(P(-0.7f, -0.35f), P(0.85f, -0.35f), color, t * 0.8f);
            break;
        case Icon::Org:
            dl->AddRect(P(-0.3f, -0.9f), P(0.3f, -0.4f), color, 2.0f, 0, t);
            dl->AddRect(P(-0.95f, 0.4f), P(-0.35f, 0.9f), color, 2.0f, 0, t);
            dl->AddRect(P(0.35f, 0.4f), P(0.95f, 0.9f), color, 2.0f, 0, t);
            dl->AddLine(P(0.0f, -0.4f), P(0.0f, 0.0f), color, t);
            dl->AddLine(P(-0.65f, 0.0f), P(0.65f, 0.0f), color, t);
            dl->AddLine(P(-0.65f, 0.0f), P(-0.65f, 0.4f), color, t);
            dl->AddLine(P(0.65f, 0.0f), P(0.65f, 0.4f), color, t);
            break;
        case Icon::Gear: {  // eight teeth around a ring
            for (int i = 0; i < 8; ++i) {
                float a = kPi / 4.0f * static_cast<float>(i);
                ImVec2 d(std::cos(a), std::sin(a));
                dl->AddLine(c + d * (h * 0.55f), c + d * (h * 0.92f), color, t * 1.7f);
            }
            dl->AddCircle(c, h * 0.58f, color, 0, t * 1.2f);
            dl->AddCircle(c, h * 0.22f, color, 0, t);
            break;
        }
        case Icon::Moon: {  // crescent: a big arc on the left, closed by a smaller arc
            const float R = h * 0.85f, a0 = 0.35f * kPi, a1 = 1.65f * kPi;
            const float ex = std::cos(a0) * R, ey = std::sin(a0) * R;  // the two tips are (ex, +ey) and (ex, -ey)
            const float d = R * 0.3f, r2 = std::sqrt(d * d + ey * ey);
            const ImVec2 c2(c.x + ex + d, c.y);
            const float b = std::atan2(ey, -d);  // angle of the lower tip as seen from c2
            dl->PathArcTo(c, R, a0, a1, 24);
            dl->PathArcTo(c2, r2, -b, -2.0f * kPi + b, 16);
            dl->PathStroke(color, ImDrawFlags_Closed, t);
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
    // Flat: no shadow. A lifted card (hovered, dragged) gets a darker border instead.
    dl->AddRectFilled(p, p + size, u32(col::Card), rounding);
    if (lifted && border.x == col::CardBorder.x && border.y == col::CardBorder.y && border.z == col::CardBorder.z)
        border = col::Ring;
    if (borderWidth > 0) dl->AddRect(p, p + size, u32(border), rounding, 0, borderWidth);
}

void gradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 from, ImVec4 to, float rounding) {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
    const int v1 = dl->VtxBuffer.Size;
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, v1, a, b, u32(from), u32(to));
}

void beginCard(const char* id, ImVec2 size, bool scroll, ImVec2 padding) {
    if (gCompact) padding = ImVec2(std::min(padding.x, 18.0f), std::min(padding.y, 18.0f));
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

void beginAutoCard(const char* id, ImVec2 padding) {
    if (gCompact) padding = ImVec2(std::min(padding.x, 18.0f), std::min(padding.y, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
}

void endAutoCard() {
    ImGui::EndChild();
    // The child draws on top of its parent, so the card can go in afterwards.
    cardShape(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectSize());
}

// ================================================================ widgets

bool button(const char* label, ButtonStyle style, ImVec2 size, ImFont* font) {
    if (!font) font = fonts.strong;
    const char* end = labelEnd(label);
    ImVec2 ts = measure(font, label, end);
    if (size.x <= 0) size.x = ts.x + 32.0f;
    if (size.y <= 0) size.y = ts.y + 20.0f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    const bool off = style == ButtonStyle::Disabled;
    bool clicked = ImGui::InvisibleButton(label, size) && !off;
    bool hov = ImGui::IsItemHovered() && !off;
    bool held = ImGui::IsItemActive() && !off;
    if (!off) handCursorIfHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 textCol = 0;
    const float r = kRadius;
    switch (style) {
        case ButtonStyle::Primary:
            dl->AddRectFilled(p, p + size, u32(hov || held ? col::AccentHover : col::Accent), r);
            textCol = u32(col::OnAccent);
            break;
        case ButtonStyle::Outline:
            dl->AddRectFilled(p, p + size, u32(hov ? col::AccentSoft : col::Card), r);
            dl->AddRect(p, p + size, u32(hov || held ? col::Accent : col::Ring), r, 0, 1.0f);
            textCol = u32(hov ? col::Accent : col::Ink);
            break;
        case ButtonStyle::White:
            dl->AddRectFilled(p, p + size, u32(hov ? ImVec4(0.90f, 0.925f, 0.97f, 1.0f) : col::White), r);
            textCol = u32(ImVec4(0.0f, 0.188f, 0.561f, 1.0f));  // ultramarine on white, both themes
            break;
        case ButtonStyle::Danger:
            dl->AddRectFilled(p, p + size, u32(hov ? col::BadSoft : col::Card), r);
            dl->AddRect(p, p + size, u32(col::Bad, hov ? 1.0f : 0.7f), r, 0, 1.0f);
            textCol = u32(col::Bad);
            break;
        case ButtonStyle::Disabled:
            dl->AddRectFilled(p, p + size, u32(col::Bg), r);
            dl->AddRect(p, p + size, u32(col::CardBorder), r, 0, 1.0f);
            textCol = u32(col::Muted);
            break;
    }
    textCentered(dl, font, p + size * 0.5f, textCol, label, end);
    return clicked;
}

bool navItem(const char* id, const char* label, Icon icon, bool selected, float width, const char* badge) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width, 42);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    handCursorIfHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) {
        dl->AddRectFilled(p, p + size, u32(col::SidebarSel));
        dl->AddRectFilled(p, ImVec2(p.x + 3, p.y + size.y), u32(col::SidebarMark));
    } else if (hov) {
        dl->AddRectFilled(p, p + size, u32(col::SidebarHover));
    }
    const ImU32 c = u32(selected || hov ? col::White : col::SidebarText);
    drawIcon(dl, icon, ImVec2(p.x + 26, p.y + size.y * 0.5f), 15.0f, c);
    ImFont* f = selected ? fonts.strong : fonts.body;
    textAt(dl, f, ImVec2(p.x + 48, p.y + (size.y - fontPx(f)) * 0.5f), c, label);

    if (badge && *badge) {
        ImVec2 bs = measure(fonts.label, badge);
        // A count that needs attention: amber with mono figures
        ImVec2 a(p.x + width - bs.x - 28, p.y + (size.y - bs.y - 8) * 0.5f);
        ImVec2 b(p.x + width - 14, a.y + bs.y + 8);
        dl->AddRectFilled(a, b, u32(col::SidebarAmber), 2.0f);
        textAt(dl, fonts.label, ImVec2(a.x + 7, a.y + 4), u32(col::White), badge);
    }
    return clicked;
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
    float edgeW = 1.0f;
    switch (state) {
        case AnswerState::Normal:
            if (hov) { fill = col::AccentSoft; edge = col::Accent; edgeW = 1.5f; }
            break;
        case AnswerState::Correct: fill = col::GoodSoft; edge = col::Good; edgeW = 1.5f; break;
        case AnswerState::Wrong: fill = col::BadSoft; edge = col::Bad; edgeW = 1.5f; break;
        case AnswerState::Dimmed: textCol = col::Muted; break;
    }

    const bool animating = anim >= 0.0f && anim < 1.0f;
    dl->AddRectFilled(p, p + size, u32(fill), kRadius);
    dl->AddRect(p, p + size, u32(edge), kRadius, 0, edgeW);

    // Letter badge (a check or an X after answering), with a "pop" when it appears
    ImVec2 bc(p.x + 32, p.y + size.y * 0.5f);
    float r = 13.0f;
    if (animating && (state == AnswerState::Correct || state == AnswerState::Wrong))
        r *= 1.0f + 0.4f * std::sin(kPi * std::clamp(anim / 0.3f, 0.0f, 1.0f));
    const char l[2] = {letter, 0};
    const ImVec2 rr(r, r);  // letter badges are small square boxes
    if (state == AnswerState::Correct || state == AnswerState::Wrong) {
        dl->AddRectFilled(bc - rr, bc + rr, u32(edge), 2.0f);
        drawIcon(dl, state == AnswerState::Correct ? Icon::Check : Icon::Cross, bc, r * 1.1f, u32(col::White));
    } else if (hov) {
        dl->AddRectFilled(bc - rr, bc + rr, u32(col::Accent), 2.0f);
        textCentered(dl, fonts.label, bc, u32(col::OnAccent), l);
    } else {
        dl->AddRectFilled(bc - rr, bc + rr, u32(col::Bg), 2.0f);
        dl->AddRect(bc - rr, bc + rr, u32(col::Ring), 2.0f, 0, 1.0f);
        textCentered(dl, fonts.label, bc, u32(state == AnswerState::Dimmed ? col::Muted : col::InkSoft), l);
    }
    textAt(dl, fonts.body, ImVec2(p.x + 64, p.y + (size.y - ts.y) * 0.5f), u32(textCol), text, nullptr, wrap);

    // Confetti burst from the badge, drawn on top of everything
    return clicked;
}

bool pictureAnswerCard(const char* id, char letter, AnswerState state, ImVec2 size, float anim,
                       const std::function<void(ImDrawList*, ImVec2, ImVec2)>& drawPicture) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool interactive = state == AnswerState::Normal;
    bool clicked = ImGui::InvisibleButton(id, size) && interactive;
    bool hov = ImGui::IsItemHovered() && interactive;
    if (interactive) handCursorIfHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 fill = col::Card, edge = col::CardBorder;
    float edgeW = 1.0f;
    switch (state) {
        case AnswerState::Normal:
            if (hov) { fill = col::AccentSoft; edge = col::Accent; edgeW = 1.5f; }
            break;
        case AnswerState::Correct: fill = col::GoodSoft; edge = col::Good; edgeW = 2.0f; break;
        case AnswerState::Wrong: fill = col::BadSoft; edge = col::Bad; edgeW = 2.0f; break;
        case AnswerState::Dimmed: break;
    }
    const bool animating = anim >= 0.0f && anim < 1.0f;
    dl->AddRectFilled(p, p + size, u32(fill), kRadius);

    // The picture, inset from the card edge
    const float inset = 7.0f;
    const ImVec2 a = p + ImVec2(inset, inset), b = p + size - ImVec2(inset, inset);
    dl->PushClipRect(a, b, true);
    drawPicture(dl, a, b);
    if (state == AnswerState::Dimmed) dl->AddRectFilled(a, b, u32(col::Card, 0.55f));
    dl->PopClipRect();
    dl->AddRect(p, p + size, u32(edge), kRadius, 0, edgeW);

    // Letter badge in the top-left corner (a check or an X after answering)
    const ImVec2 bc = p + ImVec2(22, 22);
    float r = 12.0f;
    if (animating && (state == AnswerState::Correct || state == AnswerState::Wrong))
        r *= 1.0f + 0.4f * std::sin(kPi * std::clamp(anim / 0.3f, 0.0f, 1.0f));
    const char l[2] = {letter, 0};
    const ImVec2 rr(r, r);  // letter badges are small square boxes
    if (state == AnswerState::Correct || state == AnswerState::Wrong) {
        dl->AddRectFilled(bc - rr, bc + rr, u32(edge), 2.0f);
        drawIcon(dl, state == AnswerState::Correct ? Icon::Check : Icon::Cross, bc, r * 1.1f, u32(col::White));
    } else {
        dl->AddRectFilled(bc - rr, bc + rr, u32(hov ? col::Accent : col::Card), 2.0f);
        dl->AddRect(bc - rr, bc + rr, u32(hov ? col::Accent : col::Ring), 2.0f, 0, 1.0f);
        textCentered(dl, fonts.label, bc, u32(hov ? col::OnAccent : state == AnswerState::Dimmed ? col::Muted : col::InkSoft), l);
    }
    return clicked;
}

bool textField(const char* id, const char* hint, char* buf, size_t size, TextKind kind, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, gTouch ? 13.0f : 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col::Bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::Bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, col::Card);
    ImGui::PushStyleColor(ImGuiCol_Border, col::Ring);
    ImGui::PushStyleColor(ImGuiCol_Text, col::Ink);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, col::Muted);  // the hint
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(col::Accent.x, col::Accent.y, col::Accent.z, 0.3f));
    ImGui::PushFont(fonts.body);
    ImGui::SetNextItemWidth(width);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (kind == TextKind::Password) flags |= ImGuiInputTextFlags_Password;
    if (kind != TextKind::Text) flags |= ImGuiInputTextFlags_CharsNoBlank;
    const bool entered = ImGui::InputTextWithHint(id, hint, buf, size, flags);
    const bool active = ImGui::IsItemActive();
    ImGui::PopFont();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);
    if (active) {
        gTextKind = kind;
        // Blue outline while typing
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), u32(col::Accent), kRadius, 0, 1.5f);
    }
    return entered;
}

namespace {
// Keeps a multi-line box's text inside its width: every line break becomes a space,
// then the last space before each line would overflow becomes a line break again.
// Swapping one byte for another leaves the cursor and selection where they were.
int wrapCallback(ImGuiInputTextCallbackData* data) {
    const float width = *static_cast<const float*>(data->UserData);
    ImFont* font = ImGui::GetFont();
    const float px = ImGui::GetFontSize();
    char* b = data->Buf;
    const std::string before(b, static_cast<size_t>(data->BufTextLen));
    for (int i = 0; i < data->BufTextLen; ++i)
        if (b[i] == '\n') b[i] = ' ';
    int lineStart = 0, lastSpace = -1;
    for (int i = 0; i < data->BufTextLen; ++i) {
        if (b[i] == ' ') {
            const float w = font->CalcTextSizeA(px, FLT_MAX, 0.0f, b + lineStart, b + i).x;
            if (w > width && lastSpace > lineStart) {
                b[lastSpace] = '\n';
                lineStart = lastSpace + 1;
            }
            lastSpace = i;
        }
    }
    // The last word on the last line
    if (lastSpace > lineStart &&
        font->CalcTextSizeA(px, FLT_MAX, 0.0f, b + lineStart, b + data->BufTextLen).x > width) {
        b[lastSpace] = '\n';
    }
    // Only tell ImGui when the line breaks really moved
    if (before.compare(0, before.size(), b, static_cast<size_t>(data->BufTextLen)) != 0) data->BufDirty = true;
    return 0;
}
}  // namespace

void textArea(const char* id, const char* hint, char* buf, size_t size, ImVec2 box, bool focus) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, gTouch ? 13.0f : 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col::Bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::Bg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, col::Card);
    ImGui::PushStyleColor(ImGuiCol_Border, col::Ring);
    ImGui::PushStyleColor(ImGuiCol_Text, col::Ink);
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(col::Accent.x, col::Accent.y, col::Accent.z, 0.3f));
    ImGui::PushFont(fonts.body);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float wrapWidth = box.x - 28.0f - ImGui::GetStyle().ScrollbarSize;  // inside the padding, clear of a scrollbar
    if (focus) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextMultiline(id, buf, size, box, ImGuiInputTextFlags_CallbackAlways, wrapCallback, &wrapWidth);
    const bool active = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!buf[0] && !active)
        textAt(dl, fonts.body, p + ImVec2(14, gTouch ? 13.0f : 10.0f), u32(col::Muted), hint);
    ImGui::PopFont();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
    if (active) {
        gTextKind = TextKind::Text;
        dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), u32(col::Accent), kRadius, 0, 1.5f);
    }
}

bool toggle(const char* id, bool* value) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(42, 22);
    bool clicked = ImGui::InvisibleButton(id, size);
    handCursorIfHovered();
    if (clicked) *value = !*value;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f, k = r - 3.0f;
    dl->AddRectFilled(p, p + size, u32(*value ? col::Accent : col::Ring), kRadius);
    ImVec2 knob(*value ? p.x + size.x - r : p.x + r, p.y + r);
    dl->AddRectFilled(knob - ImVec2(k, k), knob + ImVec2(k, k), u32(col::White), 2.0f);
    return clicked;
}

void thinBar(float fraction, float width, ImVec4 color, float height) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), u32(col::Track));  // square ends
    if (fraction > 0) dl->AddRectFilled(p, ImVec2(p.x + std::max(2.0f, width * fraction), p.y + height), u32(color));
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
    dl->AddRectFilled(a, b, u32(soft), kRadius);
    dl->AddRectFilled(a, ImVec2(a.x + 3, b.y), u32(strong));
    dl->ChannelsMerge();
    ImGui::Dummy(ImVec2(0, 6));
}

// ================================================================ swipe cards

void cardText(ImFont* font, ImVec4 color, const char* text, float left, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float px = fontPx(font), scale = px / font->FontSize, lineH = px * 1.25f;
    color.w *= ImGui::GetStyle().Alpha;
    const ImU32 c = u32(color);
    const float y0 = ImGui::GetCursorScreenPos().y;
    float y = y0;
    const char* s = text;
    const char* end = text + std::strlen(text);
    while (s < end) {
        // Wrap within one line of the text: ImGui's wrapping doesn't stop at a line break
        const char* nl = static_cast<const char*>(std::memchr(s, '\n', static_cast<size_t>(end - s)));
        const char* lineEnd = font->CalcWordWrapPositionA(scale, s, nl ? nl : end, width);
        if (lineEnd == s) ++lineEnd;  // a word wider than the card: break it
        const char* trim = lineEnd;
        while (trim > s && (trim[-1] == ' ' || trim[-1] == '\n')) --trim;
        const float w = measure(font, s, trim).x;
        textAt(dl, font, ImVec2(left + (width - w) * 0.5f, y), c, s, trim);
        y += lineH;
        s = lineEnd;
        if (s < end && *s == '\n') ++s;
        while (s < end && *s == ' ') ++s;
    }
    ImGui::SetCursorScreenPos(ImVec2(left, y0));
    ImGui::Dummy(ImVec2(width, y - y0));
}

int swipeCard(const char* id, SwipeCard& s, float minHeight, const std::function<void(float, float)>& front,
              const std::function<void(float, float)>& back, bool keys) {
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    const float dt = io.DeltaTime;
    const float pad = gCompact ? 24.0f : 44.0f, rounding = 4.0f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float cw = std::min(avail, 820.0f);
    if (s.enterStart < 0) s.enterStart = now;

    // Flip animation: the card squashes to an edge and opens on the other face
    float squash = 1.0f;
    int shown = s.flipped ? 1 : 0;
    if (s.flipStart >= 0) {
        const float ft = static_cast<float>(now - s.flipStart) / 0.26f;
        if (ft < 1.0f) {
            squash = std::fabs(std::cos(ft * 3.14159265f));
            if (ft < 0.5f) shown = 1 - shown;
        } else {
            s.flipStart = -1.0;
        }
    }
    const float ch = std::max(minHeight, s.contentH[shown] + pad * 2 + 30);
    const ImVec2 base = start + ImVec2((avail - cw) * 0.5f, 0);
    const float threshold = cw * 0.25f;

    // ---- Input
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(base);
    ImGui::InvisibleButton("card", ImVec2(cw, ch));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (!s.fly) {
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f)) {
            s.dragging = true;
            s.x = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 5.0f).x;
        }
        if (ImGui::IsItemDeactivated()) {
            if (!s.dragging) s.flip();
            else if (std::fabs(s.x) > threshold) s.send(s.x > 0 ? 1 : -1);
            s.dragging = false;
        }
        // Two-finger swipe on a trackpad
        const bool wheel = hovered && io.MouseWheelH != 0.0f;
        if (s.wheelLock) {
            s.wheelIdle = wheel ? 0.0f : s.wheelIdle + dt;
            if (s.wheelIdle > 0.2f) s.wheelLock = false;
        } else if (wheel) {
            s.x += io.MouseWheelH * 22.0f;
            s.wheeling = true;
            s.wheelIdle = 0.0f;
        } else if (s.wheeling && (s.wheelIdle += dt) > 0.12f) {
            s.wheeling = false;
            if (std::fabs(s.x) > threshold) s.send(s.x > 0 ? 1 : -1);
        }
        if (keys && !s.fly) {
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_2, false)) s.send(1);
            else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_1, false)) s.send(-1);
            else if (ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) ||
                     ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
                s.flip();
        }
        if (!s.dragging && !s.wheeling && !s.fly) s.x -= s.x * std::min(1.0f, dt * 14.0f);  // spring back
    }
    if (s.fly) s.wheelLock = true;
    if (hovered && !s.fly) ImGui::SetMouseCursor(s.dragging ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);

    float x = s.x;
    int result = 0;
    if (s.fly) {
        const float t = static_cast<float>(now - s.flyStart);
        x = s.x + s.fly * (t * 1800.0f + t * t * 26000.0f);  // speeds up as it leaves
        if (t > 0.24f) result = s.fly;
    }

    // ---- Drawing
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float enter = easeOut(static_cast<float>(now - s.enterStart) / 0.2f);
    cardShape(dl, base + ImVec2(20, 14), ImVec2(cw - 40, ch), rounding);  // the next card, waiting underneath

    const float k = std::clamp(std::fabs(x) / threshold, 0.0f, 1.0f);
    const ImVec4 edge = x > 0 ? col::Good : col::Bad;
    const float cwS = cw * std::max(squash, 0.02f);
    const ImVec2 p = base + ImVec2(x + (cw - cwS) * 0.5f, std::fabs(x) * 0.05f + (1.0f - enter) * 14.0f);
    const ImVec2 sz(cwS, ch);
    cardShape(dl, p, sz, rounding, hovered || s.dragging, k > 0.01f ? mix(col::CardBorder, edge, k) : col::CardBorder,
              1.0f + 2.0f * k);
    if (k > 0.01f) dl->AddRectFilled(p, p + sz, u32(edge, 0.10f * k), rounding);

    // Face content, centered, clipped to the (possibly squashed) card
    const float h = s.contentH[shown];
    const float alpha = (h > 0 ? 1.0f : 0.0f) * squash * squash * enter;
    const float left = base.x + x + pad;
    ImGui::PushClipRect(p, p + sz, true);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(alpha, 0.0f));
    ImGui::SetCursorScreenPos(ImVec2(left, p.y + std::max(pad, (ch - h) * 0.5f - 10)));
    ImGui::BeginGroup();
    (shown ? back : front)(left, cw - pad * 2);
    ImGui::EndGroup();
    if (squash > 0.98f) s.contentH[shown] = ImGui::GetItemRectSize().y;
    ImGui::PopStyleVar();

    const char* hint = gTouch ? (shown ? "Tap to flip back" : "Tap to flip") : shown ? "Click to flip back" : "Click to flip";
    textCentered(dl, fonts.small, ImVec2(p.x + sz.x * 0.5f, p.y + ch - 24), u32(col::Muted, alpha * (1.0f - k)), hint);
    if (k > 0.01f) {  // "KNOW IT" / "STILL LEARNING" stamp
        const char* stamp = x > 0 ? "KNOW IT" : "STILL LEARNING";
        const ImVec2 ts = measure(fonts.strong, stamp);
        const ImVec2 c(p.x + sz.x * 0.5f, p.y + 34);
        const ImVec2 a = c - ts * 0.5f - ImVec2(16, 8), b = c + ts * 0.5f + ImVec2(16, 8);
        dl->AddRectFilled(a, b, u32(col::Card, k), kRadius);
        dl->AddRect(a, b, u32(edge, k), kRadius, 0, 1.5f);
        textCentered(dl, fonts.strong, c, u32(edge, k), stamp);
    }
    ImGui::PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(start.x, base.y + ch));
    ImGui::Dummy(ImVec2(avail, 0.0f));
    if (result) s.reset();
    return result;
}

}  // namespace ui
