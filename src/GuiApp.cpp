#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "GuiApp.h"
#include "Widgets.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace ui;

namespace {

constexpr float kSidebar = 288.0f;  // width of the navy sidebar
constexpr float kGap = 18.0f;       // space between cards

ImU32 u32(ImVec4 c, float alphaMul = 1.0f) {
    c.w *= alphaMul;
    return ImGui::ColorConvertFloat4ToU32(c);
}

// printf-style formatting into a std::string
std::string fmt(const char* f, ...) {
    char buf[512];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buf, sizeof buf, f, args);
    va_end(args);
    return buf;
}

std::string mmss(int seconds) { return fmt("%d:%02d", seconds / 60, seconds % 60); }

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string plural(int n, const char* word) { return fmt("%d %s%s", n, word, n == 1 ? "" : "s"); }

// Puts the next item flush with the right edge of the current window.
void alignRight(float itemWidth) {
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - itemWidth);
}

// Per-subject look: icon glyph and a one-line description
struct SubjectStyle { const char* glyph; const char* desc; };
SubjectStyle styleFor(const std::string& id) {
    if (id == "word_knowledge") return {"Aa", "Build vocabulary and pick the closest synonym."};
    if (id == "math_knowledge") return {"x\xC2\xB2", "Practice algebra, geometry, and number rules."};
    if (id == "arithmetic_reasoning") return {"\xC3\xB7", "Turn word problems into calculations."};
    if (id == "verbal_analogies") return {"A:B", "Learn word relationships and avoid misleading answers."};
    if (id == "reading_comprehension") return {"\xC2\xB6", "Read short passages and find what they support."};
    return {"?", ""};
}

// A white subject card: icon tile, badges, title, description, progress bar,
// footer text, and up to two buttons. Returns 1 or 2 when a button is clicked.
struct SubjectCard {
    int tint = 0;
    const char* glyph = nullptr;
    Icon* icon = nullptr;
    std::string title, desc, badge, tag;  // tag = extra badge such as "FOCUS"
    float progress = -1.0f;               // < 0 hides the bar
    std::string footer;
    const char* primary = nullptr;
    const char* secondary = nullptr;
    ButtonStyle primaryStyle = ButtonStyle::Outline;
    bool highlight = false;
    bool disabled = false;
};

int subjectCard(const char* id, const SubjectCard& c, ImVec2 size) {
    ImGui::PushID(id);
    ImGui::BeginGroup();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hov = !c.disabled && ImGui::IsMouseHoveringRect(p, p + size) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    cardShape(dl, p - ImVec2(0, hov ? 2.0f : 0.0f), size, 16.0f, hov,
              c.highlight ? col::Accent : col::CardBorder, c.highlight ? 2.0f : 1.0f);
    const ImVec2 q = p - ImVec2(0, hov ? 2.0f : 0.0f);  // card lifts slightly on hover
    const float pad = 26.0f;
    const float fade = c.disabled ? 0.55f : 1.0f;

    Tint t = tint(c.tint);
    if (c.disabled) t.strong.w = 0.5f;
    iconTile(dl, q + ImVec2(pad, pad), 50.0f, t, c.glyph, c.icon);

    // Badges in the top-right corner, right to left
    float bx = q.x + size.x - pad;
    auto badge = [&](const std::string& text, ImVec4 bg, ImVec4 fg) {
        if (text.empty()) return;
        ImVec2 ts = textSize(fonts.label, text.c_str());
        ImVec2 a(bx - ts.x - 20, q.y + pad + 10), b(bx, q.y + pad + 10 + ts.y + 12);
        dl->AddRectFilled(a, b, u32(bg, fade), (b.y - a.y) * 0.5f);
        dl->AddText(fonts.label, fontPx(fonts.label), ImVec2(a.x + 10, a.y + 6), u32(fg, fade), text.c_str());
        bx = a.x - 8;
    };
    badge(c.badge, col::AccentSoft, col::Accent);
    badge(c.tag, ImVec4(0.996f, 0.933f, 0.878f, 1.0f), ImVec4(0.851f, 0.451f, 0.094f, 1.0f));

    float y = q.y + pad + 50 + 18;
    dl->AddText(fonts.heading, fontPx(fonts.heading), ImVec2(q.x + pad, y), u32(col::Ink, fade), c.title.c_str());
    y += fontPx(fonts.heading) + 10;
    dl->AddText(fonts.body, fontPx(fonts.body), ImVec2(q.x + pad, y), u32(col::Muted, fade), c.desc.c_str(), nullptr,
                size.x - pad * 2);

    const float barY = q.y + size.y - pad - 40 - 26;
    if (c.progress >= 0) {
        const float w = size.x - pad * 2;
        dl->AddRectFilled(ImVec2(q.x + pad, barY), ImVec2(q.x + pad + w, barY + 7), u32(col::Track), 3.5f);
        if (c.progress > 0)
            dl->AddRectFilled(ImVec2(q.x + pad, barY), ImVec2(q.x + pad + std::max(7.0f, w * c.progress), barY + 7),
                              u32(col::Accent), 3.5f);
    }

    // Footer: stats text on the left, buttons on the right
    const float rowY = q.y + size.y - pad - 40;
    dl->AddText(fonts.label, fontPx(fonts.label), ImVec2(q.x + pad, rowY + 20 - fontPx(fonts.label) * 0.5f),
                u32(ImVec4(0.30f, 0.37f, 0.58f, 1.0f), fade), c.footer.c_str());

    int clicked = 0;
    if (!c.disabled) {
        float x = q.x + size.x - pad;
        auto place = [&](const char* label, ButtonStyle style) {
            float w = textSize(fonts.strong, label).x + 36;
            x -= w;
            ImGui::SetCursorScreenPos(ImVec2(x, rowY));
            bool hit = button(label, style, ImVec2(w, 40));
            x -= 10;
            return hit;
        };
        if (c.primary && place(c.primary, c.primaryStyle)) clicked = 1;
        if (c.secondary && place(c.secondary, ButtonStyle::Outline)) clicked = 2;
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return clicked;
}

// Lays out cards in a grid: call next() before each card.
struct Grid {
    int cols;
    float cardW;
    int i = 0;
    Grid(int columns) : cols(columns) {
        cardW = (ImGui::GetContentRegionAvail().x - kGap * (cols - 1)) / cols;
    }
    void next() {
        if (i % cols != 0) ImGui::SameLine(0, kGap);
        else if (i > 0) ImGui::Dummy(ImVec2(0, kGap - ImGui::GetStyle().ItemSpacing.y));
        ++i;
    }
};

// A page title with a muted subtitle underneath.
void pageTitle(const char* title, const char* subtitle = nullptr) {
    textIn(fonts.title, col::Ink, title);
    if (subtitle) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 760);
        textIn(fonts.body, col::Muted, subtitle);
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, 6));
}

// Popups need their own padding (the main window has none).
bool beginPopup(const char* id, bool contextOnLastItem = false) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
    bool open = contextOnLastItem ? ImGui::BeginPopupContextItem(id) : ImGui::BeginPopup(id);
    ImGui::PopStyleVar(2);
    return open;
}

}  // namespace

// ================================================================ setup

bool GuiApp::load(const std::string& dataDir, std::string& error) {
    try {
        registry_.load(dataDir + "/subtests.json");
        for (const auto& s : registry_.all())
            bank_.loadFile(dataDir + "/" + s.file, s.id);
        deck_.load(dataDir + "/flashcards.json");
        deck_.loadProgress("afoqt_flashcards.json");
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    loadSettings();
    return true;
}

void GuiApp::draw() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("AFOQT Study", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    const ImVec2 o = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(o, ImVec2(o.x + kSidebar, o.y + sz.y), u32(col::Sidebar));

    // Sidebar
    ImGui::SetCursorScreenPos(o);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 30));
    ImGui::BeginChild("sidebar", ImVec2(kSidebar, sz.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawSidebar(sz.y);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Main area
    ImGui::SetCursorScreenPos(ImVec2(o.x + kSidebar, o.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(46, 34));
    ImGui::BeginChild("main", ImVec2(sz.x - kSidebar, sz.y), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (screen_ != lastScreen_) {  // new page: start at the top
        ImGui::SetScrollY(0.0f);
        lastScreen_ = screen_;
    }
    switch (screen_) {
        case Screen::Dashboard: drawDashboard(); break;
        case Screen::Practice: drawPractice(); break;
        case Screen::Review: drawReview(); break;
        case Screen::Flashcards: drawFlashcards(); break;
        case Screen::Progress: drawProgress(); break;
        case Screen::Quiz: drawQuiz(); break;
        case Screen::Results: drawResults(); break;
    }
    ImGui::Dummy(ImVec2(0, 12));  // breathing room at the bottom when scrolling
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
}

// ================================================================ frame

void GuiApp::drawSidebar(float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;

    // Brand: gradient "A" tile, name, and tagline
    const ImVec2 p = ImGui::GetCursorScreenPos();
    gradientRect(dl, p, p + ImVec2(48, 48), ImVec4(0.40f, 0.58f, 1.0f, 1.0f), ImVec4(0.23f, 0.38f, 0.88f, 1.0f), 13.0f);
    ImVec2 as = textSize(fonts.heading, "A");
    dl->AddText(fonts.heading, fontPx(fonts.heading), p + ImVec2(24 - as.x * 0.5f, 24 - as.y * 0.5f), u32(col::White), "A");
    dl->AddText(fonts.heading, fontPx(fonts.heading), p + ImVec2(62, -2), u32(col::White), "AFOQT");
    dl->AddText(fonts.heading, fontPx(fonts.heading), p + ImVec2(62, 22), u32(col::White), "Study Lab");
    ImGui::SetCursorScreenPos(p + ImVec2(62, 52));
    spacedLabel("PREPARE WITH PURPOSE", col::SidebarMuted, 1.8f);
    ImGui::SetCursorScreenPos(ImVec2(p.x, ImGui::GetCursorScreenPos().y + 6));
    divider(col::SidebarLine);
    ImGui::Dummy(ImVec2(0, 4));
    spacedLabel("WORKSPACE", col::SidebarMuted, 2.0f);
    ImGui::Dummy(ImVec2(0, 2));

    // Menu
    int missedTotal = 0;
    for (const auto& [id, n] : missedBySubtest()) missedTotal += n;
    Screen section = screen_;
    if (section == Screen::Quiz || section == Screen::Results)
        section = sessionTag_ == "REVIEW" ? Screen::Review : sessionTag_ == "WARM-UP" ? Screen::Dashboard : Screen::Practice;
    std::string badge = missedTotal ? std::to_string(missedTotal) : "";

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));
    if (navItem("n_dash", "Dashboard", Icon::Home, section == Screen::Dashboard, w)) goTo(Screen::Dashboard);
    if (navItem("n_prac", "Practice tests", Icon::Target, section == Screen::Practice, w)) goTo(Screen::Practice);
    if (navItem("n_rev", "Review missed", Icon::Review, section == Screen::Review, w, badge.c_str())) goTo(Screen::Review);
    if (navItem("n_vocab", "Vocabulary cards", Icon::Cards, section == Screen::Flashcards, w)) goTo(Screen::Flashcards);
    if (navItem("n_prog", "My progress", Icon::Chart, section == Screen::Progress, w)) goTo(Screen::Progress);
    ImGui::PopStyleVar();

    // Focus-area card pinned to the bottom
    const float cardH = 132.0f;
    const float top = std::max(ImGui::GetCursorPosY() + 20, height - cardH - 30);
    ImGui::SetCursorPosY(top);
    const ImVec2 c = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(c, c + ImVec2(w, cardH), u32(col::SidebarCard), 14.0f);
    dl->AddRect(c, c + ImVec2(w, cardH), u32(col::SidebarLine), 14.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(c + ImVec2(20, 18));
    spacedLabel("YOUR FOCUS AREA", col::SidebarMuted, 1.6f);
    const SubtestInfo* fi = registry_.find(focusId());
    std::string name = fi ? fi->name : "Not set yet";
    ImFont* nf = textSize(fonts.heading, name.c_str()).x > w - 40 ? fonts.strong : fonts.heading;
    ImGui::SetCursorScreenPos(ImVec2(c.x + 20, ImGui::GetCursorScreenPos().y - 4));
    textIn(nf, col::White, name.c_str());
    ImGui::SetCursorScreenPos(ImVec2(c.x + 20, ImGui::GetCursorScreenPos().y - 6));
    const char* why = !pinnedFocus_.empty() ? "Pinned by you." : fi ? "Your lowest accuracy so far." : "Practice a section to find it.";
    textIn(fonts.small, col::SidebarMuted, why);

    // "Change focus" link
    ImGui::SetCursorScreenPos(ImVec2(c.x + 20, ImGui::GetCursorScreenPos().y - 2));
    const char* link = "Change focus area  \xE2\x86\x92";
    ImVec2 ls = textSize(fonts.small, link);
    ImVec2 lp = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("focuslink", ls)) ImGui::OpenPopup("focuspicker");
    const bool lh = ImGui::IsItemHovered();
    if (lh) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImU32 lc = u32(lh ? col::White : ImVec4(0.62f, 0.75f, 1.0f, 1.0f));
    dl->AddText(fonts.small, fontPx(fonts.small), lp, lc, link);
    dl->AddLine(lp + ImVec2(0, ls.y + 1), lp + ImVec2(ls.x, ls.y + 1), lc, 1.0f);
    if (beginPopup("focuspicker")) {
        focusMenuItems();
        ImGui::EndPopup();
    }
}

void GuiApp::drawHeader(const std::string& label) {
    const float y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y + 9);
    spacedLabel(label.c_str(), col::Muted, 2.2f);

    // Chips on the right: sessions, then the sound toggle
    const Overall ov = overall();
    std::string sessions = plural(ov.sessions, "session");
    Icon spk = sound_.enabled ? Icon::SpeakerOn : Icon::SpeakerOff;
    const char* soundText = sound_.enabled ? "Sound on" : "Sound off";
    float w1 = textSize(fonts.strong, soundText).x + 30 + 22;
    float w2 = textSize(fonts.strong, sessions.c_str()).x + 30;
    alignRight(w1 + 10 + w2);
    ImGui::SetCursorPosY(y);
    chip("sess", sessions.c_str(), col::Card, col::Ink);
    ImGui::SameLine(0, 10);
    ImGui::SetCursorPosY(y);
    if (chip("sound", soundText, col::Card, sound_.enabled ? col::Accent : col::Muted, true, true, &spk)) {
        sound_.enabled = !sound_.enabled;
        saveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        ImGui::SetTooltip("Play a chime for correct answers");
        ImGui::PopStyleVar();
    }
    ImGui::Dummy(ImVec2(0, 10));
}

// ================================================================ dashboard

void GuiApp::drawStatRow() {
    const Overall ov = overall();
    int missedTotal = 0;
    for (const auto& [id, n] : missedBySubtest()) missedTotal += n;
    const float w = (ImGui::GetContentRegionAvail().x - kGap * 3) / 4;
    const ImVec2 size(w, 142);
    statCard(size, "Study sessions", fmt("%d", ov.sessions).c_str(), "Practice and timed tests");
    ImGui::SameLine(0, kGap);
    statCard(size, "Questions answered", fmt("%d", ov.total).c_str(), "Across all sections");
    ImGui::SameLine(0, kGap);
    statCard(size, "Practice accuracy", ov.total ? fmt("%d%%", ov.correct * 100 / ov.total).c_str() : "\xE2\x80\x94",
             "Not an official percentile");
    ImGui::SameLine(0, kGap);
    statCard(size, "Saved to review", fmt("%d", missedTotal).c_str(), "Questions to revisit");
}

void GuiApp::drawDashboard() {
    drawHeader("TRAINING DASHBOARD");

    // ---- Hero banner
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 a = ImGui::GetCursorScreenPos(), b = a + ImVec2(w, 318);
    gradientRect(dl, a, b, col::HeroA, col::HeroB, 24.0f);
    dl->PushClipRect(a, b, true);  // decorations stay inside the banner
    dl->AddCircle(ImVec2(b.x - 150, a.y + 70), 200, u32(col::White, 0.10f), 96, 1.5f);
    dl->AddCircle(ImVec2(b.x - 110, a.y + 110), 140, u32(col::White, 0.08f), 96, 1.5f);
    dl->AddCircleFilled(ImVec2(b.x - 60, b.y + 40), 120, u32(col::White, 0.04f), 64);
    drawIcon(dl, Icon::Sparkle, ImVec2(b.x - 128, a.y + 186), 54, u32(col::White, 0.55f));
    drawIcon(dl, Icon::Sparkle, ImVec2(b.x - 200, a.y + 120), 20, u32(col::White, 0.35f));
    dl->PopClipRect();

    const float pad = 42.0f;
    ImGui::SetCursorScreenPos(a + ImVec2(pad, 38));
    spacedLabel("YOUR PERSONAL PREP CENTER", ImVec4(1, 1, 1, 0.85f), 2.2f);
    float y = ImGui::GetCursorScreenPos().y + 6;
    dl->AddText(fonts.hero, fontPx(fonts.hero), ImVec2(a.x + pad, y), u32(col::White), "Train smarter.");
    y += fontPx(fonts.hero) + 2;
    dl->AddText(fonts.hero, fontPx(fonts.hero), ImVec2(a.x + pad, y), u32(col::White), "Test with confidence.");
    y += fontPx(fonts.hero) + 16;
    const char* blurb = "Five AFOQT knowledge sections, explanations that actually teach, flashcards for "
                        "vocabulary, and timed drills when you're ready.";
    dl->AddText(fonts.body, fontPx(fonts.body), ImVec2(a.x + pad, y), u32(col::White, 0.88f), blurb, nullptr,
                std::min(560.0f, w * 0.6f));
    y += textSize(fonts.body, blurb, std::min(560.0f, w * 0.6f)).y + 24;
    ImGui::SetCursorScreenPos(ImVec2(a.x + pad, y));
    if (button("Start a 10-question warm-up  \xE2\x86\x92", ButtonStyle::White, ImVec2(0, 48))) startWarmup();
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 24));

    if (!message_.empty()) {
        beginCallout();
        textIn(fonts.strong, col::Ink, message_.c_str());
        endCallout(col::AccentSoft, col::Accent);
    }

    drawStatRow();
    ImGui::Dummy(ImVec2(0, 16));

    // ---- Training areas
    const float ty = ImGui::GetCursorPosY();
    textIn(fonts.title, col::Ink, "Choose your training area");
    const char* note = "Original practice questions \xC2\xB7 Full explanations";
    alignRight(textSize(fonts.body, note).x);
    ImGui::SetCursorPosY(ty + fontPx(fonts.title) - fontPx(fonts.body) - 2);
    textIn(fonts.body, col::Muted, note);
    ImGui::Dummy(ImVec2(0, 6));

    const auto stats = progress_.stats();
    const std::string focus = focusId();
    Grid grid(2);
    const ImVec2 size(grid.cardW, 262);
    const auto& subs = registry_.all();
    for (size_t i = 0; i < subs.size(); ++i) {
        const SubtestInfo& s = subs[i];
        auto it = stats.find(s.id);
        SubjectCard c;
        c.tint = subjectIndex(s.id);
        c.glyph = styleFor(s.id).glyph;
        c.title = s.name;
        c.desc = styleFor(s.id).desc;
        c.badge = plural(static_cast<int>(bank_.size(s.id)), "question");
        c.highlight = s.id == focus;
        if (c.highlight) c.tag = "FOCUS";
        c.progress = it != stats.end() ? static_cast<float>(it->second.accuracy() / 100.0) : 0.0f;
        c.footer = it != stats.end()
            ? fmt("%.0f%% accuracy  \xC2\xB7  %.0f sec/question", it->second.accuracy(), it->second.avgSecondsPerQuestion())
            : "Not started yet";
        c.primary = "Train  \xE2\x86\x92";
        grid.next();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (subjectCard(s.id.c_str(), c, size) == 1) startSubtest(s, SessionMode::Practice);
        cardFocusMenu(s, p, p + size);
    }
    // Vocabulary card
    Icon cards = Icon::Cards;
    SubjectCard v;
    v.tint = 5;
    v.icon = &cards;
    v.title = "Vocabulary cards";
    v.desc = "Flip through Word Knowledge flashcards with spaced repetition.";
    v.badge = plural(static_cast<int>(deck_.size()), "word");
    v.progress = deck_.size() ? static_cast<float>(deck_.masteredCount()) / deck_.size() : 0.0f;
    v.footer = fmt("%d mastered  \xC2\xB7  %d to go", deck_.masteredCount(),
                   static_cast<int>(deck_.size()) - deck_.masteredCount());
    v.primary = "Study  \xE2\x86\x92";
    grid.next();
    if (subjectCard("vocab", v, size) == 1) goTo(Screen::Flashcards);
}

// ================================================================ practice tests

void GuiApp::drawPractice() {
    drawHeader("PRACTICE TESTS");
    pageTitle("Practice by section",
              "Practice is untimed and explains every answer. Timed tests use real pacing and show your score at the end.");
    if (!message_.empty()) {
        beginCallout();
        textIn(fonts.strong, col::Ink, message_.c_str());
        endCallout(col::AccentSoft, col::Accent);
    }

    // Full battery banner
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 a = ImGui::GetCursorScreenPos(), b = a + ImVec2(w, 118);
    gradientRect(dl, a, b, col::HeroA, col::HeroB, 18.0f);
    dl->PushClipRect(a, b, true);
    dl->AddCircle(ImVec2(b.x - 260, a.y + 10), 120, u32(col::White, 0.08f), 64, 1.5f);
    dl->PopClipRect();
    int secs = 0;
    for (const auto& s : registry_.inCategory("knowledge")) secs += s.timeLimitSeconds;
    dl->AddText(fonts.heading, fontPx(fonts.heading), a + ImVec2(32, 30), u32(col::White), "Full knowledge battery");
    std::string sub = fmt("All five sections, timed and back to back. About %d minutes.", secs / 60);
    dl->AddText(fonts.body, fontPx(fonts.body), a + ImVec2(32, 64), u32(col::White, 0.85f), sub.c_str());
    const char* bl = "Start battery  \xE2\x86\x92";
    const float bw = textSize(fonts.strong, bl).x + 40;
    ImGui::SetCursorScreenPos(ImVec2(b.x - 32 - bw, a.y + 35));
    if (button(bl, ButtonStyle::White, ImVec2(bw, 48))) {
        startBattery();
        return;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 24));

    const auto stats = progress_.stats();
    const std::string focus = focusId();
    Grid grid(2);
    const ImVec2 size(grid.cardW, 262);
    for (const auto& s : registry_.all()) {
        auto it = stats.find(s.id);
        SubjectCard c;
        c.tint = subjectIndex(s.id);
        c.glyph = styleFor(s.id).glyph;
        c.title = s.name;
        c.desc = styleFor(s.id).desc;
        c.badge = plural(static_cast<int>(bank_.size(s.id)), "question");
        c.highlight = s.id == focus;
        if (c.highlight) c.tag = "FOCUS";
        c.progress = it != stats.end() ? static_cast<float>(it->second.accuracy() / 100.0) : 0.0f;
        c.footer = it != stats.end() ? fmt("%.0f%% accuracy", it->second.accuracy()) : "Not started yet";
        c.primary = "Practice  \xE2\x86\x92";
        c.primaryStyle = ButtonStyle::Primary;
        std::string timed = fmt("Timed \xC2\xB7 %d min", (s.timeLimitSeconds + 30) / 60);
        c.secondary = timed.c_str();
        grid.next();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const int hit = subjectCard(s.id.c_str(), c, size);
        cardFocusMenu(s, p, p + size);
        if (hit == 1) { startSubtest(s, SessionMode::Practice); return; }
        if (hit == 2) { startSubtest(s, SessionMode::Timed); return; }
    }
}

// ================================================================ review

void GuiApp::drawReview() {
    drawHeader("REVIEW");
    const auto missed = missedBySubtest();
    int total = 0;
    for (const auto& [id, n] : missed) total += n;

    const float ty = ImGui::GetCursorPosY();
    pageTitle("Review missed questions",
              "Every question you miss is saved here. Answer it correctly to clear it from the list.");
    if (total > 0) {
        std::string all = fmt("Review all %d  \xE2\x86\x92", total);
        alignRight(textSize(fonts.strong, all.c_str()).x + 40);
        ImGui::SetCursorPosY(ty);
        if (button(all.c_str(), ButtonStyle::Primary, ImVec2(0, 46))) {
            startReview("");
            return;
        }
        ImGui::Dummy(ImVec2(0, 0));
    }

    if (total == 0) {
        beginCard("empty", ImVec2(0, 230), false, ImVec2(40, 40));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 c = ImGui::GetCursorScreenPos() + ImVec2(30, 30);
        dl->AddCircleFilled(c, 30, u32(col::GoodSoft));
        drawIcon(dl, Icon::Check, c, 30, u32(col::Good));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 84);
        ImGui::BeginGroup();
        textIn(fonts.heading, col::Ink, "Nothing to review");
        wrappedIn(fonts.body, col::Muted, "You have no missed questions right now. Take a practice test, and any "
                                           "question you miss will show up here.");
        ImGui::Dummy(ImVec2(0, 4));
        if (button("Go to practice tests  \xE2\x86\x92")) goTo(Screen::Practice);
        ImGui::EndGroup();
        endCard();
        return;
    }

    Grid grid(2);
    const ImVec2 size(grid.cardW, 236);
    for (const auto& s : registry_.all()) {
        const int n = missed.count(s.id) ? missed.at(s.id) : 0;
        SubjectCard c;
        c.tint = subjectIndex(s.id);
        c.glyph = styleFor(s.id).glyph;
        c.title = s.name;
        c.desc = n ? "Questions you missed in this section." : "Nothing to review here.";
        c.badge = n ? plural(n, "question") : "All clear";
        c.footer = n ? "Clears as you get them right" : "";
        c.primary = n ? "Review  \xE2\x86\x92" : nullptr;
        c.primaryStyle = ButtonStyle::Primary;
        c.disabled = n == 0;
        grid.next();
        if (subjectCard(s.id.c_str(), c, size) == 1) {
            startReview(s.id);
            return;
        }
    }
}

// ================================================================ quiz

void GuiApp::drawQuiz() {
    engine_.update();
    if (engine_.finished()) {
        finishSession();
        return;
    }
    const Question& q = engine_.current();
    const bool feedback = engine_.showingFeedback();
    const SessionResult& r = engine_.result();
    const float anim = feedback && feedbackTime_ >= 0 ? static_cast<float>(ImGui::GetTime() - feedbackTime_) : -1.0f;

    // ---- Header: label on the left; timer and sound chips on the right
    const float y0 = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y0 + 9);
    spacedLabel((sessionTag_ + "  \xC2\xB7  " + upper(sessionName_)).c_str(), col::Muted, 2.0f);
    const int left = engine_.secondsLeft();
    std::string timer = left >= 0 ? mmss(left) + " left" : "Untimed";
    Icon clock = Icon::Clock, spk = sound_.enabled ? Icon::SpeakerOn : Icon::SpeakerOff;
    const char* soundText = sound_.enabled ? "Sound on" : "Sound off";
    float w1 = textSize(fonts.strong, timer.c_str()).x + 52, w2 = textSize(fonts.strong, soundText).x + 52;
    alignRight(w1 + 10 + w2);
    ImGui::SetCursorPosY(y0);
    const bool urgent = left >= 0 && left <= 30;
    chip("timer", timer.c_str(), urgent ? col::BadSoft : col::Card, urgent ? col::Bad : col::Ink, false, true, &clock);
    ImGui::SameLine(0, 10);
    ImGui::SetCursorPosY(y0);
    if (chip("sound", soundText, col::Card, sound_.enabled ? col::Accent : col::Muted, true, true, &spk)) {
        sound_.enabled = !sound_.enabled;
        saveSettings();
    }
    ImGui::Dummy(ImVec2(0, 6));

    // ---- Title and quit
    const float ty = ImGui::GetCursorPosY();
    textIn(fonts.title, col::Ink, sessionName_.c_str());
    alignRight(textSize(fonts.strong, "Quit").x + 40);
    ImGui::SetCursorPosY(ty);
    if (button("Quit", ButtonStyle::Outline)) {
        queue_.clear();
        goTo(sessionTag_ == "REVIEW" ? Screen::Review : sessionTag_ == "WARM-UP" ? Screen::Dashboard : Screen::Practice);
        return;
    }

    // ---- Progress bar and counts
    const float w = ImGui::GetContentRegionAvail().x;
    const int answered = static_cast<int>(r.answers.size());
    thinBar(static_cast<float>(answered) / engine_.total(), w, col::Accent, 8.0f);
    textIn(fonts.strong, col::InkSoft, fmt("Question %d of %d", engine_.index() + 1, engine_.total()).c_str());
    std::string score = sessionTag_ == "TIMED TEST" ? std::string("Score shown at the end")
                                                    : fmt("%d correct  \xC2\xB7  %d missed", r.correctCount(),
                                                          answered - r.correctCount());
    alignRight(textSize(fonts.body, score.c_str()).x);
    textIn(fonts.body, col::Muted, score.c_str());
    ImGui::Dummy(ImVec2(0, 2));

    // ---- The question card
    beginCard("question", ImVec2(0, std::max(420.0f, ImGui::GetContentRegionAvail().y - 26)), true, ImVec2(32, 30));
    if (!q.passage.empty()) {
        spacedLabel("PASSAGE", col::Muted, 1.8f);
        beginCallout();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24);
        textIn(fonts.body, col::InkSoft, q.passage.c_str());
        ImGui::PopTextWrapPos();
        endCallout(col::Bg, ImVec4(0.75f, 0.80f, 0.89f, 1.0f));
        ImGui::Dummy(ImVec2(0, 4));
    }
    wrappedIn(fonts.prompt, col::Ink, q.prompt.c_str());
    ImGui::Dummy(ImVec2(0, 6));

    int clicked = -2;  // -2 = nothing, -1 = skip, 0+ = choice
    const float aw = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 10));
    for (int i = 0; i < static_cast<int>(q.choices.size()); ++i) {
        AnswerState st = AnswerState::Normal;
        if (feedback) {
            if (i == q.correctIndex) st = AnswerState::Correct;
            else if (i == engine_.lastChoice()) st = AnswerState::Wrong;
            else st = AnswerState::Dimmed;
        }
        // A wrong answer shakes side to side for a moment
        if (st == AnswerState::Wrong && anim >= 0 && anim < 0.4f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::sin(anim * 48.0f) * 7.0f * (1.0f - anim / 0.4f));
        ImGui::PushID(i);
        const bool animated = st == AnswerState::Correct || st == AnswerState::Wrong;
        if (answerCard("ans", static_cast<char>('A' + i), q.choices[i].c_str(), st, aw, animated ? anim : -1.0f))
            clicked = i;
        ImGui::PopID();
        if (!feedback && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + i), false)) clicked = i;
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 4));

    if (!feedback) {
        if (button("Skip   S", ButtonStyle::Outline) || ImGui::IsKeyPressed(ImGuiKey_S, false)) clicked = -1;
    } else {
        const int chosen = engine_.lastChoice();
        const bool right = chosen == q.correctIndex;
        beginCallout();
        textIn(fonts.heading, right ? col::Good : col::Bad, right ? "Correct!" : chosen < 0 ? "Skipped" : "Not quite");
        wrappedIn(fonts.body, col::Ink, q.explanation.c_str());
        endCallout(right ? col::GoodSoft : col::BadSoft, right ? col::Good : col::Bad);
        if (button("Next  \xE2\x86\x92   Enter") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            engine_.next();
            feedbackTime_ = -1.0;
        }
    }
    if (!sessionTips_.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        divider();
        wrappedIn(fonts.small, col::Muted, ("Tip: " + sessionTips_ + "   Keys: A\xE2\x80\x93" "E to answer, S to skip, Enter for next.").c_str());
    }
    endCard();

    if (clicked != -2) {
        engine_.submit(clicked);
        if (engine_.showingFeedback()) {  // practice and review show the answer right away
            feedbackTime_ = ImGui::GetTime();
            if (clicked == q.correctIndex) sound_.correct();
            else sound_.wrong();
        }
    }
}

// ================================================================ results

void GuiApp::drawResults() {
    drawHeader("RESULTS");
    const SessionResult& r = engine_.result();
    const int total = static_cast<int>(r.answers.size());
    const int pct = total ? r.correctCount() * 100 / total : 0;

    pageTitle(fmt("%s results", sessionName_.c_str()).c_str());
    const float w = (ImGui::GetContentRegionAvail().x - kGap * 2) / 3;
    statCard(ImVec2(w, 142), "Score", fmt("%d%%", pct).c_str(),
             pct >= 80 ? "Strong work" : pct >= 60 ? "Getting there" : "Keep practicing",
             pct >= 80 ? col::Good : pct >= 60 ? col::Accent : col::Bad);
    ImGui::SameLine(0, kGap);
    statCard(ImVec2(w, 142), "Correct", fmt("%d / %d", r.correctCount(), total).c_str(),
             fmt("%d answered", r.answeredCount()).c_str());
    ImGui::SameLine(0, kGap);
    statCard(ImVec2(w, 142), "Time used", mmss(static_cast<int>(r.totalSeconds)).c_str(),
             r.answeredCount() ? fmt("%.0f sec per answer", r.totalSeconds / r.answeredCount()).c_str() : "");
    ImGui::Dummy(ImVec2(0, 4));

    if (r.timeExpired) {
        beginCallout();
        textIn(fonts.strong, col::Ink, "Time ran out before you finished. Unanswered questions count as missed.");
        endCallout(col::BadSoft, col::Bad);
    }

    if (!queue_.empty()) {
        if (button(fmt("Next section: %s  \xE2\x86\x92", queue_.front().name.c_str()).c_str())) {
            startNextInQueue();
            return;
        }
        ImGui::SameLine(0, 12);
    }
    if (button("Back to dashboard", queue_.empty() ? ButtonStyle::Primary : ButtonStyle::Outline)) {
        queue_.clear();
        goTo(Screen::Dashboard);
        return;
    }
    ImGui::Dummy(ImVec2(0, 8));

    beginCard("missed", ImVec2(0, std::max(300.0f, ImGui::GetContentRegionAvail().y - 26)), true);
    spacedLabel("MISSED QUESTIONS", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 2));
    const auto& qs = engine_.questions();
    bool any = false;
    for (size_t i = 0; i < r.answers.size(); ++i) {
        if (r.answers[i].correct) continue;
        any = true;
        const Question& q = qs[i];
        wrappedIn(fonts.strong, col::Ink, q.prompt.c_str());
        const int chosen = r.answers[i].chosenIndex;
        textIn(fonts.body, col::Bad, chosen >= 0 ? fmt("Your answer:  %s", q.choices[chosen].c_str()).c_str() : "Not answered");
        textIn(fonts.body, col::Good, fmt("Correct:  %s", q.choices[q.correctIndex].c_str()).c_str());
        wrappedIn(fonts.small, col::Muted, q.explanation.c_str());
        divider();
    }
    if (!any) textIn(fonts.heading, col::Good, "None. Perfect score!");
    endCard();
}

// ================================================================ flashcards

void GuiApp::drawFlashcards() {
    drawHeader("VOCABULARY CARDS");
    const int mastered = deck_.masteredCount();
    const int total = static_cast<int>(deck_.size());

    // ---- Start screen
    if (!flashActive_) {
        pageTitle("Vocabulary cards", "Word Knowledge flashcards with spaced repetition: words you miss come back "
                                      "sooner, and a word is mastered after 3 correct answers in a row.");
        if (!message_.empty()) {
            beginCallout();
            textIn(fonts.strong, col::Ink, message_.c_str());
            endCallout(col::AccentSoft, col::Accent);
        }
        const float w3 = (ImGui::GetContentRegionAvail().x - kGap * 2) / 3;
        statCard(ImVec2(w3, 142), "Mastered", fmt("%d", mastered).c_str(), fmt("of %d words", total).c_str(), col::Accent);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Still learning", fmt("%d", total - mastered).c_str(), "Not mastered yet");
        ImGui::SameLine(0, kGap);
        // Options card with the "definition first" toggle
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(w3, 142));
        cardShape(ImGui::GetWindowDrawList(), p, ImVec2(w3, 142));
        ImGui::SetCursorScreenPos(p + ImVec2(24, 22));
        textIn(fonts.strong, col::InkSoft, "Definition first");
        ImGui::SetCursorScreenPos(p + ImVec2(24, 58));
        toggle("deffirst", &definitionFirst_);
        ImGui::SetCursorScreenPos(p + ImVec2(24, 100));
        textIn(fonts.small, col::Muted, "Guess the word from its meaning");
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, 16));

        textIn(fonts.title, col::Ink, "Start a session");
        ImGui::Dummy(ImVec2(0, 4));
        Icon ic[3] = {Icon::Bolt, Icon::Target, Icon::List};
        struct Opt { const char* title; std::string desc; std::string badge; size_t count; bool learning; };
        Opt opts[3] = {
            {"Quick 20", "Twenty cards, your weakest words first.", "20 cards", 20, false},
            {"Still learning", "Only words you haven't mastered yet.", "Up to 30", 30, true},
            {"Whole deck", "Every word once, in random order.", plural(total, "card"), 0, false},
        };
        Grid grid(3);
        for (int i = 0; i < 3; ++i) {
            SubjectCard c;
            c.tint = i == 0 ? 2 : i == 1 ? 0 : 1;
            c.icon = &ic[i];
            c.title = opts[i].title;
            c.desc = opts[i].desc;
            c.badge = opts[i].badge;
            c.primary = "Start  \xE2\x86\x92";
            c.primaryStyle = i == 0 ? ButtonStyle::Primary : ButtonStyle::Outline;
            c.disabled = opts[i].learning && mastered == total;
            grid.next();
            if (subjectCard(fmt("fc%d", i).c_str(), c, ImVec2(grid.cardW, 236)) == 1) {
                startFlashcards(opts[i].count, opts[i].learning);
                return;
            }
        }
        return;
    }

    // ---- Session finished
    if (deck_.sessionDone()) {
        pageTitle("Session complete", "Nice work. Words you marked \"still learning\" will come back sooner next time.");
        const float w3 = (ImGui::GetContentRegionAvail().x - kGap * 2) / 3;
        statCard(ImVec2(w3, 142), "Knew it", fmt("%d", deck_.sessionGotIt()).c_str(), "Moved up a level", col::Good);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Still learning", fmt("%d", deck_.sessionMissed()).c_str(), "Coming back soon", col::Bad);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Mastered", fmt("%d", mastered).c_str(), fmt("of %d words", total).c_str(), col::Accent);
        ImGui::Dummy(ImVec2(0, 8));
        if (button("Back to vocabulary cards")) flashActive_ = false;
        return;
    }

    // ---- A card
    const Flashcard& c = deck_.current();
    const float ty = ImGui::GetCursorPosY();
    textIn(fonts.title, col::Ink, "Vocabulary cards");
    alignRight(textSize(fonts.strong, "End session").x + 40);
    ImGui::SetCursorPosY(ty);
    if (button("End session", ButtonStyle::Outline)) {
        flashActive_ = false;
        return;
    }
    textIn(fonts.body, col::Muted, fmt("%d cards left  \xC2\xB7  %d known  \xC2\xB7  %d still learning",
                                       static_cast<int>(deck_.remaining()), deck_.sessionGotIt(), deck_.sessionMissed()).c_str());
    ImGui::Dummy(ImVec2(0, 4));

    const float cardH = std::clamp(ImGui::GetContentRegionAvail().y - 100, 300.0f, 400.0f);
    beginCard("flash", ImVec2(0, cardH), true, ImVec2(44, 38));
    const bool showWord = !definitionFirst_ || cardFlipped_;
    const bool showMeaning = definitionFirst_ || cardFlipped_;
    if (showWord) {
        spacedLabel(c.partOfSpeech == "adj" ? "ADJECTIVE" : c.partOfSpeech == "verb" ? "VERB" : "NOUN", col::Muted, 1.8f);
        textIn(fonts.hero, col::Accent, c.word.c_str());
        ImGui::Dummy(ImVec2(0, 2));
    }
    if (showMeaning) {
        if (showWord) divider();
        spacedLabel("DEFINITION", col::Muted, 1.8f);
        wrappedIn(fonts.prompt, col::Ink, c.definition.c_str());
        if (cardFlipped_) {
            ImGui::Dummy(ImVec2(0, 4));
            spacedLabel("SYNONYM", col::Muted, 1.8f);
            textIn(fonts.strong, col::Good, c.synonym.c_str());
            if (!c.example.empty()) {
                ImGui::Dummy(ImVec2(0, 4));
                spacedLabel("EXAMPLE", col::Muted, 1.8f);
                wrappedIn(fonts.body, col::InkSoft, ("\"" + c.example + "\"").c_str());
            }
        }
    }
    if (!cardFlipped_) {
        ImGui::Dummy(ImVec2(0, 10));
        textIn(fonts.body, col::Muted, definitionFirst_ ? "Which word has this meaning?" : "What does this word mean?");
    }
    endCard();
    ImGui::Dummy(ImVec2(0, 8));

    if (!cardFlipped_) {
        if (button("Flip card    Space", ButtonStyle::Primary, ImVec2(240, 50)) ||
            ImGui::IsKeyPressed(ImGuiKey_Space, false))
            cardFlipped_ = true;
    } else {
        int answer = -1;
        if (button("Still learning    1", ButtonStyle::Danger, ImVec2(240, 50))) answer = 0;
        ImGui::SameLine(0, 14);
        if (button("Got it    2", ButtonStyle::Primary, ImVec2(240, 50))) answer = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) answer = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) answer = 1;
        if (answer >= 0) {
            if (answer == 1) sound_.correct();
            deck_.mark(answer == 1);
            cardFlipped_ = false;
        }
    }
}

// ================================================================ progress

void GuiApp::drawProgress() {
    drawHeader("MY PROGRESS");
    pageTitle("My progress", "Accuracy and pace for each section. Your focus area is highlighted.");
    drawStatRow();
    ImGui::Dummy(ImVec2(0, 10));

    const auto stats = progress_.stats();
    const std::string focusSid = focusId();
    const float listW = ImGui::GetContentRegionAvail().x * 0.64f;

    // ---- Left: accuracy by section
    const float rowH = 84.0f;
    const float listH = 70 + rowH * registry_.all().size();
    beginCard("bysection", ImVec2(listW, listH));
    spacedLabel("ACCURACY BY SECTION", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 4));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    for (const auto& s : registry_.all()) {
        auto it = stats.find(s.id);
        const bool has = it != stats.end();
        const bool focus = s.id == focusSid;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, rowH - 12));
        iconTile(dl, p + ImVec2(0, 6), 44, tint(subjectIndex(s.id)), styleFor(s.id).glyph);
        dl->AddText(fonts.strong, fontPx(fonts.strong), p + ImVec2(60, 8), u32(col::Ink), s.name.c_str());
        std::string sub = has ? fmt("%s  \xC2\xB7  %.0f sec/question", plural(it->second.sessions, "session").c_str(),
                                    it->second.avgSecondsPerQuestion())
                              : "Not started yet";
        if (focus) sub += "  \xC2\xB7  FOCUS AREA";
        dl->AddText(fonts.small, fontPx(fonts.small), p + ImVec2(60, 32), u32(focus ? col::Accent : col::Muted), sub.c_str());
        if (has) {
            const double acc = it->second.accuracy();
            const ImVec4 c = acc >= 80 ? col::Good : acc >= 60 ? col::Accent : col::Bad;
            std::string pct = fmt("%.0f%%", acc);
            ImVec2 ps = textSize(fonts.heading, pct.c_str());
            dl->AddText(fonts.heading, fontPx(fonts.heading), ImVec2(p.x + w - ps.x, p.y + 6), u32(c), pct.c_str());
            const float x0 = p.x + 60, x1 = p.x + w, y = p.y + 58;
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 6), u32(col::Track), 3.0f);
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + (x1 - x0) * static_cast<float>(acc / 100.0), y + 6), u32(c), 3.0f);
        }
    }
    endCard();

    // ---- Right: settings
    ImGui::SameLine(0, kGap);
    beginCard("settings", ImVec2(0, listH));
    spacedLabel("SETTINGS", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 4));

    auto toggleRow = [&](const char* id, const char* title, const char* sub, bool* v) {
        const float y = ImGui::GetCursorPosY();
        textIn(fonts.strong, col::Ink, title);
        alignRight(46);
        ImGui::SetCursorPosY(y);
        bool changed = toggle(id, v);
        ImGui::SetCursorPosY(y + 26);
        wrappedIn(fonts.small, col::Muted, sub);
        divider();
        return changed;
    };
    if (toggleRow("snd", "Sound effects", "A chime when you answer correctly.", &sound_.enabled)) {
        saveSettings();
        if (sound_.enabled) sound_.correct();  // let them hear it
    }
    toggleRow("def", "Definition first", "Flashcards show the meaning; you guess the word.", &definitionFirst_);

    textIn(fonts.strong, col::Ink, "Focus area");
    const SubtestInfo* fi = registry_.find(focusSid);
    textIn(fonts.small, col::Muted, fi ? (fi->name + (pinnedFocus_.empty() ? "  (automatic)" : "  (pinned)")).c_str()
                                       : "Not set yet");
    ImGui::Dummy(ImVec2(0, 2));
    if (button("Change focus area", ButtonStyle::Outline)) ImGui::OpenPopup("focuspicker2");
    if (beginPopup("focuspicker2")) {
        focusMenuItems();
        ImGui::EndPopup();
    }
    endCard();
}

// ================================================================ actions

void GuiApp::goTo(Screen s) {
    message_.clear();
    if (s != Screen::Results) queue_.clear();
    if (s == Screen::Flashcards && screen_ != Screen::Flashcards) flashActive_ = false;
    feedbackTime_ = -1.0;
    screen_ = s;
}

void GuiApp::startSubtest(const SubtestInfo& s, SessionMode mode) {
    const int count = mode == SessionMode::Timed ? s.questionCount : 0;
    auto questions = bank_.draw(s.id, count, rng_);
    if (questions.empty()) {
        message_ = "No questions loaded for " + s.name + ".";
        return;
    }
    message_.clear();
    sessionName_ = s.name;
    sessionTag_ = mode == SessionMode::Timed ? "TIMED TEST" : "PRACTICE";
    sessionTips_ = s.tips;
    sessionTimeLimit_ = s.timeLimitSeconds;
    isReview_ = false;
    feedbackTime_ = -1.0;
    engine_.start(s.id, std::move(questions), mode, s.timeLimitSeconds);
    screen_ = Screen::Quiz;
}

void GuiApp::startBattery() {
    queue_ = registry_.inCategory("knowledge");
    startNextInQueue();
}

void GuiApp::startNextInQueue() {
    if (queue_.empty()) {
        screen_ = Screen::Dashboard;
        return;
    }
    SubtestInfo next = queue_.front();
    queue_.erase(queue_.begin());
    startSubtest(next, SessionMode::Timed);
}

void GuiApp::startWarmup() {
    // Two questions from each knowledge section, mixed together
    std::vector<Question> questions;
    for (const auto& s : registry_.inCategory("knowledge")) {
        auto two = bank_.draw(s.id, 2, rng_);
        questions.insert(questions.end(), two.begin(), two.end());
    }
    if (questions.empty()) return;
    std::shuffle(questions.begin(), questions.end(), rng_);
    message_.clear();
    sessionName_ = "Warm-up";
    sessionTag_ = "WARM-UP";
    sessionTips_ = "A quick mix from every section. Read each question type carefully as it switches.";
    sessionTimeLimit_ = 0;
    isReview_ = false;
    feedbackTime_ = -1.0;
    engine_.start("warmup", std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::startReview(const std::string& subtestId) {
    auto questions = bank_.drawByIds(progress_.missedQuestionIds(), rng_);
    if (!subtestId.empty()) {
        questions.erase(std::remove_if(questions.begin(), questions.end(),
                                       [&](const Question& q) { return q.subtest != subtestId; }),
                        questions.end());
    }
    if (questions.empty()) {
        message_ = "No missed questions to review. Nice work!";
        return;
    }
    message_.clear();
    const SubtestInfo* info = subtestId.empty() ? nullptr : registry_.find(subtestId);
    sessionName_ = info ? "Review: " + info->name : "Review missed questions";
    sessionTag_ = "REVIEW";
    sessionTips_ = info ? info->tips : "";
    sessionTimeLimit_ = 0;
    isReview_ = true;
    feedbackTime_ = -1.0;
    engine_.start("review", std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::startFlashcards(size_t count, bool learningOnly) {
    deck_.startSession(count, learningOnly, rng_);
    cardFlipped_ = false;
    if (deck_.sessionDone()) {
        message_ = "No words to study. You've mastered them all!";
        return;
    }
    message_.clear();
    flashActive_ = true;
}

void GuiApp::finishSession() {
    progress_.record(engine_.result(), isReview_);
    feedbackTime_ = -1.0;
    screen_ = Screen::Results;
}

// ================================================================ focus area

void GuiApp::focusMenuItems() {
    spacedLabel("CHOOSE YOUR FOCUS AREA", col::Muted, 1.6f);
    ImGui::Dummy(ImVec2(0, 2));
    const ImVec2 row(310, 34);
    const SubtestInfo* w = registry_.find(weakestId());
    std::string autoLabel = w ? "Automatic  (now: " + w->name + ")" : "Automatic  (lowest accuracy)";
    if (ImGui::Selectable(autoLabel.c_str(), pinnedFocus_.empty(), 0, row)) setFocus("");
    ImGui::Separator();
    for (const auto& s : registry_.all())
        if (ImGui::Selectable(s.name.c_str(), pinnedFocus_ == s.id, 0, row)) setFocus(s.id);
}

void GuiApp::cardFocusMenu(const SubtestInfo& s, ImVec2 cardMin, ImVec2 cardMax) {
    const std::string id = "focusmenu_" + s.id;
    if (ImGui::IsMouseHoveringRect(cardMin, cardMax) && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        ImGui::OpenPopup(id.c_str());
    if (!beginPopup(id.c_str())) return;
    const ImVec2 row(310, 34);
    spacedLabel("FOCUS AREA", col::Muted, 1.6f);
    if (pinnedFocus_ == s.id) {
        if (ImGui::Selectable("Unpin  (go back to automatic)", false, 0, row)) setFocus("");
    } else {
        if (ImGui::Selectable(("Set " + s.name + " as focus").c_str(), false, 0, row)) setFocus(s.id);
        if (!pinnedFocus_.empty() && ImGui::Selectable("Use automatic focus", false, 0, row)) setFocus("");
    }
    ImGui::EndPopup();
}

int GuiApp::subjectIndex(const std::string& id) const {
    // Fixed colors per subject, matching the sample design
    if (id == "verbal_analogies") return 0;
    if (id == "word_knowledge") return 1;
    if (id == "arithmetic_reasoning") return 2;
    if (id == "math_knowledge") return 3;
    if (id == "reading_comprehension") return 4;
    const auto& all = registry_.all();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].id == id) return static_cast<int>(i);
    return 6;
}

std::string GuiApp::focusId() const {
    if (!pinnedFocus_.empty() && registry_.find(pinnedFocus_)) return pinnedFocus_;
    return weakestId();
}

void GuiApp::setFocus(const std::string& id) {
    pinnedFocus_ = id;
    saveSettings();
}

void GuiApp::loadSettings() {
    std::ifstream in("afoqt_settings.json");
    if (!in) return;  // first run
    try {
        nlohmann::json j;
        in >> j;
        pinnedFocus_ = j.value("focus", "");
        sound_.enabled = j.value("sound", true);
    } catch (...) {
        pinnedFocus_.clear();  // unreadable file: use defaults
    }
}

void GuiApp::saveSettings() const {
    nlohmann::json j;
    j["focus"] = pinnedFocus_;
    j["sound"] = sound_.enabled;
    std::ofstream("afoqt_settings.json") << j.dump(2);
}

// ================================================================ stats

GuiApp::Overall GuiApp::overall() const {
    Overall o;
    for (const auto& [id, st] : progress_.stats()) {
        o.correct += st.correct;
        o.total += st.total;
        o.sessions += st.sessions;
    }
    return o;
}

std::string GuiApp::weakestId() const {
    std::string id;
    double worst = 101.0;
    for (const auto& [sid, st] : progress_.stats()) {
        if (!registry_.find(sid)) continue;  // skip warm-ups and other mixed sessions
        if (st.total > 0 && st.accuracy() < worst) {
            worst = st.accuracy();
            id = sid;
        }
    }
    return id;
}

std::map<std::string, int> GuiApp::missedBySubtest() const {
    std::map<std::string, int> out;
    for (const auto& q : bank_.byIds(progress_.missedQuestionIds())) out[q.subtest]++;
    return out;
}
