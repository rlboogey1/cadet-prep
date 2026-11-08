#pragma once
// Colors, fonts, and custom-drawn widgets for the GUI.
//
// Dear ImGui's built-in buttons all look alike, so these draw their own shapes
// with ImDrawList (rounded rectangles, gradients, circles, lines) to get the
// "study lab" look: a navy sidebar, white cards with soft shadows, pill badges,
// and line icons.
#include "imgui.h"
#include <functional>

namespace ui {

// ---------------------------------------------------------------- colors
namespace col {
// Sidebar (dark navy)
inline const ImVec4 Sidebar(0.078f, 0.122f, 0.243f, 1.0f);
inline const ImVec4 SidebarSel(0.231f, 0.322f, 0.565f, 1.0f);
inline const ImVec4 SidebarHover(0.137f, 0.192f, 0.341f, 1.0f);
inline const ImVec4 SidebarText(0.835f, 0.867f, 0.937f, 1.0f);
inline const ImVec4 SidebarMuted(0.580f, 0.635f, 0.761f, 1.0f);
inline const ImVec4 SidebarCard(0.106f, 0.161f, 0.302f, 1.0f);
inline const ImVec4 SidebarLine(1.0f, 1.0f, 1.0f, 0.09f);
// Main area. These change with the theme: setDarkMode() swaps in the dark values.
inline ImVec4 Bg(0.953f, 0.961f, 0.980f, 1.0f);
inline ImVec4 Card(1.0f, 1.0f, 1.0f, 1.0f);
inline ImVec4 CardBorder(0.882f, 0.902f, 0.941f, 1.0f);
inline ImVec4 Ink(0.094f, 0.129f, 0.231f, 1.0f);        // main text
inline ImVec4 InkSoft(0.275f, 0.325f, 0.443f, 1.0f);
inline ImVec4 Muted(0.420f, 0.471f, 0.580f, 1.0f);
inline ImVec4 Track(0.890f, 0.910f, 0.949f, 1.0f);      // empty part of bars
inline ImVec4 Accent(0.247f, 0.373f, 0.855f, 1.0f);     // blue
inline ImVec4 AccentHover(0.196f, 0.318f, 0.780f, 1.0f);
inline ImVec4 AccentSoft(0.910f, 0.929f, 0.992f, 1.0f);
inline ImVec4 HeroA(0.106f, 0.192f, 0.392f, 1.0f);      // banner gradient
inline ImVec4 HeroB(0.282f, 0.427f, 0.875f, 1.0f);
inline ImVec4 Good(0.153f, 0.620f, 0.365f, 1.0f);
inline ImVec4 GoodSoft(0.906f, 0.973f, 0.929f, 1.0f);
inline ImVec4 Bad(0.851f, 0.239f, 0.275f, 1.0f);
inline ImVec4 BadSoft(0.992f, 0.925f, 0.929f, 1.0f);
inline ImVec4 Gold(0.961f, 0.690f, 0.180f, 1.0f);
inline ImVec4 White(1.0f, 1.0f, 1.0f, 1.0f);
// Smaller details that also change with the theme
inline ImVec4 Ring(0.80f, 0.835f, 0.902f, 1.0f);        // outlines on badges, buttons, toggles
inline ImVec4 TagSoft(0.996f, 0.933f, 0.878f, 1.0f);    // orange tag (FOCUS, LAST USED)
inline ImVec4 TagStrong(0.851f, 0.451f, 0.094f, 1.0f);
inline ImVec4 Footer(0.30f, 0.37f, 0.58f, 1.0f);        // footer text on subject cards
inline ImVec4 CalloutEdge(0.75f, 0.80f, 0.89f, 1.0f);   // edge of the passage box
inline ImVec4 Shadow(0.086f, 0.122f, 0.235f, 1.0f);     // card shadows (alpha set where used)
}  // namespace col

// Pastel tile + strong color pairs for subject icons
struct Tint { ImVec4 soft, strong; };
Tint tint(int index);

// ---------------------------------------------------------------- fonts
struct FontSet {
    ImFont* body = nullptr;     // 16 regular
    ImFont* small = nullptr;    // 13.5 regular
    ImFont* strong = nullptr;   // 16 bold
    ImFont* label = nullptr;    // 12.5 bold, for CAPS labels
    ImFont* prompt = nullptr;   // 20 regular, question text
    ImFont* heading = nullptr;  // 21 bold, card titles
    ImFont* title = nullptr;    // 30 bold, page titles
    ImFont* big = nullptr;      // 32 bold, numbers
    ImFont* hero = nullptr;     // 46 bold, banner headline
};
extern FontSet fonts;

void loadFonts(ImGuiIO& io, float scale);  // scale = screen pixel density (2 on Retina)
void applyTheme();
void setDarkMode(bool dark);  // switches every color above and re-applies the theme
bool darkMode();
float fontPx(ImFont* f);
ImVec2 textSize(ImFont* font, const char* text, float wrap = -1.0f);

// ---------------------------------------------------------------- icons
enum class Icon { Home, Target, Clock, Review, Cards, Chart, List, Check, Cross, Sparkle, SpeakerOn, SpeakerOff, Bolt,
                  Jet, Table, Gauge, Cube, Shield, Star, Book, Flag, Org, Gear, Moon };
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// ---------------------------------------------------------------- text
void textIn(ImFont* font, ImVec4 color, const char* text);
void wrappedIn(ImFont* font, ImVec4 color, const char* text);
// Small CAPS text with extra letter spacing, like "TRAINING DASHBOARD".
void spacedLabel(const char* text, ImVec4 color = col::Muted, float spacing = 1.6f, ImFont* font = nullptr);

// ---------------------------------------------------------------- building blocks
// White card with a soft shadow, drawn at `p`.
void cardShape(ImDrawList* dl, ImVec2 p, ImVec2 size, float rounding = 16.0f, bool lifted = false,
               ImVec4 border = col::CardBorder, float borderWidth = 1.0f);
// Rounded rectangle filled with a diagonal gradient.
void gradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 from, ImVec4 to, float rounding);

// A white card that holds other widgets (a child window). Size 0 = fill.
void beginCard(const char* id, ImVec2 size, bool scroll = false, ImVec2 padding = ImVec2(26, 24));
void endCard();

// ---------------------------------------------------------------- widgets
enum class ButtonStyle { Primary, Outline, White, Danger };
bool button(const char* label, ButtonStyle style = ButtonStyle::Primary, ImVec2 size = ImVec2(0, 0),
            ImFont* font = nullptr);

// Rounded "pill" chip. Clickable ones get a hover state.
bool chip(const char* id, const char* text, ImVec4 bg, ImVec4 fg, bool clickable = false,
          bool bordered = true, Icon* icon = nullptr);

// Sidebar menu row. Selected rows get a blue fill and a light bar on the left.
bool navItem(const char* id, const char* label, Icon icon, bool selected, float width, const char* badge = nullptr);

// "Study sessions / 2 / Completed or ended"
void statCard(ImVec2 size, const char* label, const char* value, const char* caption,
              ImVec4 valueColor = col::Ink);

// Square tile with a pastel background and a short glyph ("A:B", "Aa", "÷").
void iconTile(ImDrawList* dl, ImVec2 p, float size, Tint t, const char* glyph, Icon* icon = nullptr);

enum class AnswerState { Normal, Correct, Wrong, Dimmed };
// `anim` = seconds since the answer was given (or < 0 for no animation).
bool answerCard(const char* id, char letter, const char* text, AnswerState state, float width, float anim);
// An answer that's a picture: `drawPicture` fills the area between the two corners.
bool pictureAnswerCard(const char* id, char letter, AnswerState state, ImVec2 size, float anim,
                       const std::function<void(ImDrawList*, ImVec2, ImVec2)>& drawPicture);

bool toggle(const char* id, bool* value);
void thinBar(float fraction, float width, ImVec4 color = col::Accent, float height = 7.0f);
void divider(ImVec4 color = col::CardBorder);

// Tinted box sized to its contents (for feedback and notices).
void beginCallout();
void endCallout(ImVec4 soft, ImVec4 strong);

// ---------------------------------------------------------------- swipe cards
// A flashcard you flip and swipe, like Quizlet: click (or Space) flips it; drag it,
// swipe sideways on a trackpad, or press an arrow key to send it off. Right = "know
// it", left = "still learning". Keep one SwipeCard per card session.
struct SwipeCard {
    float x = 0.0f;             // sideways offset while dragging
    bool dragging = false, wheeling = false;
    bool wheelLock = false;     // ignores trackpad momentum left over from the last swipe
    float wheelIdle = 0.0f;
    int fly = 0;                // -1 / +1 while the card flies off
    double flyStart = -1.0;
    bool flipped = false;
    double flipStart = -1.0;
    double enterStart = -1.0;
    float contentH[2] = {0.0f, 0.0f};  // front and back height last frame (for centering)

    void flip() { flipped = !flipped; flipStart = ImGui::GetTime(); }
    void send(int dir) {
        if (fly) return;
        fly = dir;
        flyStart = ImGui::GetTime();
        dragging = wheeling = false;
    }
    void reset() { const bool lock = wheelLock; const float idle = wheelIdle; *this = SwipeCard{}; wheelLock = lock; wheelIdle = idle; }
};
// Draws the card and handles input. `front`/`back` draw the faces; they get the
// left edge and width to lay text out in (use cardText). Returns +1 or -1 once the
// card has flown off (and resets `s` for the next card), otherwise 0.
// `keys` = also answer to Space, arrows, and 1/2.
int swipeCard(const char* id, SwipeCard& s, float minHeight, const std::function<void(float, float)>& front,
              const std::function<void(float, float)>& back, bool keys = true);
// Wrapped text with every line centered, for swipe card faces.
void cardText(ImFont* font, ImVec4 color, const char* text, float left, float width);

// A burst of colorful particles around `center` (`t` = seconds since start).
void celebrate(ImDrawList* dl, ImVec2 center, float t);

}  // namespace ui
