#pragma once
// Custom-drawn widgets and the color theme for the GUI.
//
// Dear ImGui's built-in buttons all look alike, so these draw their own shapes
// with ImDrawList (rounded rectangles, circles, lines) to get the dashboard look:
// outlined cards that glow cyan, pill buttons, toggle switches, and line icons.
#include "imgui.h"

namespace ui {

// ---------------------------------------------------------------- colors
namespace col {
inline const ImVec4 Bg(0.086f, 0.141f, 0.290f, 1.0f);         // window background
inline const ImVec4 Panel(0.118f, 0.188f, 0.357f, 1.0f);      // filled panels
inline const ImVec4 SidePanel(0.102f, 0.165f, 0.325f, 1.0f);  // right-hand column
inline const ImVec4 CardFill(0.098f, 0.157f, 0.310f, 1.0f);
inline const ImVec4 CardHover(0.125f, 0.200f, 0.380f, 1.0f);
inline const ImVec4 Border(0.560f, 0.640f, 0.780f, 0.45f);
inline const ImVec4 Line(0.560f, 0.640f, 0.780f, 0.20f);       // dividers
inline const ImVec4 Text(0.945f, 0.960f, 0.990f, 1.0f);
inline const ImVec4 Muted(0.640f, 0.700f, 0.810f, 1.0f);
inline const ImVec4 Accent(0.360f, 0.835f, 0.980f, 1.0f);      // cyan
inline const ImVec4 AccentHover(0.520f, 0.890f, 1.000f, 1.0f);
inline const ImVec4 OnAccent(0.055f, 0.110f, 0.240f, 1.0f);    // text on cyan
inline const ImVec4 Good(0.330f, 0.860f, 0.580f, 1.0f);
inline const ImVec4 Bad(0.980f, 0.430f, 0.460f, 1.0f);
}  // namespace col

// ---------------------------------------------------------------- fonts
struct FontSet {
    ImFont* body = nullptr;     // normal text
    ImFont* small = nullptr;    // captions
    ImFont* heading = nullptr;  // card titles, question prompts
    ImFont* title = nullptr;    // screen titles
    ImFont* big = nullptr;      // large numbers
    ImFont* label = nullptr;    // small bold CAPS labels
};
extern FontSet fonts;

// Loads fonts from the system. `scale` is the screen's pixel density (2 on Retina).
void loadFonts(ImGuiIO& io, float scale);
void applyTheme();

// ---------------------------------------------------------------- icons
enum class Icon { Grid, Cards, Chart, Target, Clock, Review, Battery };
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// ---------------------------------------------------------------- text
void textIn(ImFont* font, ImVec4 color, const char* text);           // one line
void wrappedIn(ImFont* font, ImVec4 color, const char* text);        // wraps at window edge
void sectionLabel(const char* text);                                 // "THERMOSTAT"-style label
ImVec2 textSize(ImFont* font, const char* text, float wrap = -1.0f);

// ---------------------------------------------------------------- widgets
// Cyan (filled) or outlined button with rounded corners.
bool pill(const char* label, bool filled = true, ImVec2 size = ImVec2(0, 0),
          ImVec4 outline = col::Accent);

// Left-hand menu row. Selected rows are filled cyan.
bool navItem(const char* id, const char* label, const char* sub, Icon icon, bool selected,
             float width, float height = 66.0f);

// Outlined card with an icon and up to three lines of centered text.
// `iconText` (e.g. "WK") is drawn inside a ring; pass nullptr to use `icon` instead.
bool card(const char* id, ImVec2 size, const char* iconText, Icon icon, const char* title,
          const char* sub, const char* status, bool highlight, bool disabled = false,
          const char* tag = nullptr);

enum class AnswerState { Normal, Correct, Wrong, Dimmed };
bool answerCard(const char* id, char letter, const char* text, AnswerState state, float width);

// iOS-style switch. Returns true when it was clicked.
bool toggle(const char* id, bool* value);

// Outlined box with a large value and a small caption (like the thermostat box).
void statBox(ImVec2 size, const char* value, const char* caption,
             ImVec4 valueColor = col::Text);

// Thin rounded progress bar.
void thinBar(float fraction, float width, ImVec4 color = col::Accent, float height = 6.0f);

// "Kitchen ................ v" style row. Returns true when clicked (if clickable).
bool listRow(const char* id, const char* left, const char* right, ImVec4 rightColor,
             bool clickable);

void divider();

// Tinted box with a colored bar on the left, sized to whatever is drawn
// between begin and end (used for answer feedback and notices).
void beginCallout();
void endCallout(ImVec4 color);

// Starts / ends an outlined (or filled) rounded panel that holds other widgets.
void beginPanel(const char* id, ImVec2 size, bool filled, bool scroll = false);
void endPanel();

}  // namespace ui
