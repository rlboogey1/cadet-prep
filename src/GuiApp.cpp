#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "GuiApp.h"
#include "Composites.h"
#include "Visuals.h"
#include "Widgets.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// Word Knowledge and Verbal Analogies: definitions for the words in a missed question
// (the prompt, then the picked choice, then the right one). Words are looked up in the
// given decks (data/flashcards.json, then data/va_definitions.json); two-word entries
// like "vice president" match first, and a plural falls back to its singular.
std::vector<const Flashcard*> definitionsFor(const Question& q, int chosen,
                                             const std::vector<const FlashcardDeck*>& decks) {
    std::vector<const Flashcard*> out;
    if (q.subtest != "word_knowledge" && q.subtest != "verbal_analogies") return out;
    auto lower = [](std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    auto find = [&](const std::string& word) -> const Flashcard* {
        for (const FlashcardDeck* d : decks)
            for (const Flashcard& c : d->cards())
                if (lower(c.word) == word) return &c;
        return nullptr;
    };
    auto addWords = [&](const std::string& text) {
        std::vector<std::string> words;
        std::string word;
        for (char ch : text + " ") {
            if (std::isalpha(static_cast<unsigned char>(ch))) {
                word += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            } else if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        }
        for (size_t i = 0; i < words.size(); ++i) {
            const Flashcard* c = i + 1 < words.size() ? find(words[i] + " " + words[i + 1]) : nullptr;
            if (c) ++i;
            if (!c) c = find(words[i]);
            if (!c && words[i].size() > 3 && words[i].back() == 's') c = find(words[i].substr(0, words[i].size() - 1));
            if (c && std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
        }
    };
    addWords(q.prompt);
    if (chosen >= 0 && chosen < static_cast<int>(q.choices.size())) addWords(q.choices[static_cast<size_t>(chosen)]);
    addWords(q.choices[static_cast<size_t>(q.correctIndex)]);
    return out;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string plural(int n, const char* word) { return fmt("%d %s%s", n, word, n == 1 ? "" : "s"); }

// Puts the next item flush with the right edge of the current window.
void alignRight(float itemWidth) {
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - itemWidth);
}

// The prep sections shown at the top of the sidebar. A section's entries in
// data/subtests.json set "track" to one of these ids.
struct TrackInfo {
    const char* id;
    const char* name;
    Icon icon;
    const char* label;  // CAPS label on the dashboard
    const char* blurb;  // one plain line under the name on the dashboard banner
};
const std::vector<TrackInfo>& tracks() {
    static const std::vector<TrackInfo> list = {
        {"afoqt", "AFOQT Prep", Icon::Jet, "AFOQT PREP",
         "Practice questions for every AFOQT section, vocabulary cards, and timed tests."},
        {"warrior", "Warrior Knowledge", Icon::Shield, "WARRIOR KNOWLEDGE",
         "Flashcards for Blocks 1\xE2\x80\x93" "11 of the Det 105 handbook."},
    };
    return list;
}
const TrackInfo& trackInfo(const std::string& id) {
    for (const auto& t : tracks())
        if (id == t.id) return t;
    return tracks().front();
}

// Per-subject look: icon glyph and a one-line description
struct SubjectStyle {
    const char* glyph;  // short text for the icon tile, or nullptr to draw `icon`
    Icon icon;
    const char* desc;
};
Icon iconNamed(const std::string& n) {
    static const std::map<std::string, Icon> names = {
        {"Home", Icon::Home}, {"Target", Icon::Target}, {"List", Icon::List}, {"Jet", Icon::Jet}, {"Table", Icon::Table},
        {"Gauge", Icon::Gauge}, {"Cube", Icon::Cube}, {"Shield", Icon::Shield}, {"Star", Icon::Star}, {"Book", Icon::Book},
        {"Flag", Icon::Flag}, {"Org", Icon::Org}, {"Cards", Icon::Cards}, {"Bolt", Icon::Bolt}};
    auto it = names.find(n);
    return it == names.end() ? Icon::List : it->second;
}

SubjectStyle styleFor(const SubtestInfo& s) {
    const std::string& id = s.id;
    if (id == "word_knowledge") return {"Aa", Icon::Home, "Build vocabulary and pick the closest synonym."};
    if (id == "math_knowledge") return {"x\xC2\xB2", Icon::Home, "Practice algebra, geometry, and number rules."};
    if (id == "arithmetic_reasoning") return {"\xC3\xB7", Icon::Home, "Turn word problems into calculations."};
    if (id == "verbal_analogies") return {"A:B", Icon::Home, "Learn word relationships and avoid misleading answers."};
    if (id == "reading_comprehension") return {"\xC2\xB6", Icon::Home, "Read short passages and find what they support."};
    if (id == "table_reading") return {nullptr, Icon::Table, "Find values in a grid fast. A new table every session."};
    if (id == "instrument_comprehension") return {nullptr, Icon::Gauge, "Read the artificial horizon and compass to find the aircraft's position."};
    if (id == "aviation_information") return {nullptr, Icon::Jet, "Aerodynamics, flight controls, instruments, and airports."};
    if (id == "block_counting") return {nullptr, Icon::Cube, "Count how many blocks touch a numbered block in a 3D stack."};
    // Sections that describe themselves in subtests.json (Warrior Knowledge blocks)
    return {nullptr, iconNamed(s.icon), s.description.c_str()};
}

// A white subject card: icon tile, badges, title, description, progress bar,
// footer text, and up to two buttons. Returns 1 or 2 when a button is clicked.
struct SubjectCard {
    int tint = 0;
    const char* glyph = nullptr;  // text for the icon tile...
    Icon icon = Icon::Home;       // ...or an icon, when glyph is nullptr
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
    Icon ic = c.icon;
    iconTile(dl, q + ImVec2(pad, pad), 50.0f, t, c.glyph, c.glyph ? nullptr : &ic);

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
    badge(c.tag, col::TagSoft, col::TagStrong);

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
                u32(col::Footer, fade), c.footer.c_str());

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
            if (!s.file.empty()) bank_.loadFile(dataDir + "/" + s.file, s.id);
        deck_.load(dataDir + "/flashcards.json");
        deck_.loadProgress(apppaths::userFile("afoqt_flashcards.json"));
        repDeck_.load(apppaths::userFile("afoqt_repetition.json"), deck_);
        memDeck_.load(dataDir + "/warrior_cards.json");
        memDeck_.loadProgress(apppaths::userFile("warrior_cards_progress.json"));
        vaWords_.load(dataDir + "/va_definitions.json");
        addWarriorCards();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    loadSettings();
    return true;
}

// Warrior Knowledge blocks hold exactly the handbook items in data/warrior_cards.json:
// the title on the front, the handbook text on the back. Nothing else is added.
void GuiApp::addWarriorCards() {
    for (const Flashcard& c : memDeck_.cards()) {
        int block = 0;
        if (std::sscanf(c.partOfSpeech.c_str(), "BLOCK %d", &block) != 1) continue;
        const std::string subtest = fmt("wk_block%02d", block);
        if (!registry_.find(subtest)) continue;
        Question q;
        q.subtest = subtest;
        q.id = "WK-";  // stable id from the title, so missed cards stay on the review list
        for (char ch : c.word)
            if (std::isalnum(static_cast<unsigned char>(ch))) q.id += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        q.prompt = c.word;
        q.choices = {c.definition, "(not known)"};  // the second entry is only used to record "didn't know"
        q.correctIndex = 0;
        bank_.add(std::move(q));
    }
}

void GuiApp::draw() {
    if (!themeApplied_) {
        setDarkMode(darkMode_);
        themeApplied_ = true;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Cadet Prep", nullptr,
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
        case Screen::Home: drawHome(); break;
        case Screen::Settings: drawSettings(); break;
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
    drawLogo(dl, p + ImVec2(25, 25), 26.0f);
    dl->AddText(fonts.heading, fontPx(fonts.heading), p + ImVec2(62, -2), u32(col::White), "Cadet");
    dl->AddText(fonts.heading, fontPx(fonts.heading), p + ImVec2(62, 22), u32(col::White), "Prep");
    ImGui::SetCursorScreenPos(p + ImVec2(62, 52));
    spacedLabel("PREPARE WITH PURPOSE", col::SidebarMuted, 1.8f);
    ImGui::SetCursorScreenPos(ImVec2(p.x, ImGui::GetCursorScreenPos().y + 6));
    divider(col::SidebarLine);
    ImGui::Dummy(ImVec2(0, 4));

    // Main menu, then the prep sections: switching changes what every page below shows
    const bool inSession = screen_ == Screen::Quiz;
    const bool home = screen_ == Screen::Home || screen_ == Screen::Settings;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));
    if (navItem("n_home", "Main menu", Icon::List, screen_ == Screen::Home, w) && !inSession) goTo(Screen::Home);
    if (navItem("n_set", "Settings", Icon::Gear, screen_ == Screen::Settings, w) && !inSession) goTo(Screen::Settings);
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 4));
    spacedLabel("PREP SECTIONS", col::SidebarMuted, 2.0f);
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));
    for (const auto& t : tracks()) {
        if (navItem((std::string("t_") + t.id).c_str(), t.name, t.icon, !home && track_ == t.id, w) && !inSession) {
            if (track_ != t.id) switchTrack(t.id);
            else goTo(Screen::Dashboard);
        }
    }
    ImGui::PopStyleVar();
    if (home) return;  // the main menu has no workspace pages of its own
    ImGui::Dummy(ImVec2(0, 4));
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
    if (track_ == "afoqt" && navItem("n_vocab", "Vocabulary cards", Icon::Cards, section == Screen::Flashcards, w))
        goTo(Screen::Flashcards);
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

    // Chip on the right: sessions in this prep section (sound is changed on the Settings page)
    const Overall ov = overall();
    std::string sessions = plural(ov.sessions, "session");
    alignRight(textSize(fonts.strong, sessions.c_str()).x + 30);
    ImGui::SetCursorPosY(y);
    chip("sess", sessions.c_str(), col::Card, col::Ink);
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

void GuiApp::drawHome() {
    drawHeader("MAIN MENU");

    // ---- Welcome banner
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 a = ImGui::GetCursorScreenPos(), b = a + ImVec2(w, 190);
    gradientRect(dl, a, b, col::HeroA, col::HeroB, 24.0f);
    dl->PushClipRect(a, b, true);
    dl->AddCircle(ImVec2(b.x - 130, a.y + 40), 170, u32(col::White, 0.10f), 96, 1.5f);
    dl->PopClipRect();
    drawLogo(dl, ImVec2(b.x - 120, a.y + 95), 58.0f);
    const float pad = 42.0f;
    ImGui::SetCursorScreenPos(a + ImVec2(pad, 38));
    spacedLabel("CADET PREP", ImVec4(1, 1, 1, 0.85f), 2.2f);
    float y = ImGui::GetCursorScreenPos().y + 6;
    dl->AddText(fonts.hero, fontPx(fonts.hero), ImVec2(a.x + pad, y), u32(col::White), "Choose a section");
    y += fontPx(fonts.hero) + 14;
    const char* blurb = "AFOQT Prep or Warrior Knowledge. Your progress saves automatically.";
    dl->AddText(fonts.body, fontPx(fonts.body), ImVec2(a.x + pad, y), u32(col::White, 0.88f), blurb, nullptr,
                std::min(620.0f, w * 0.6f));
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 28));

    // ---- One card per prep section
    textIn(fonts.title, col::Ink, "Sections");
    ImGui::Dummy(ImVec2(0, 6));
    Grid grid(2);
    const ImVec2 size(grid.cardW, 262);
    for (const auto& t : tracks()) {
        const auto secs = registry_.inTrack(t.id);
        int questions = 0;
        for (const auto& s : secs) questions += static_cast<int>(bank_.size(s.id));
        const Overall ov = overallFor(t.id);
        const bool afoqt = std::string(t.id) == "afoqt";
        SubjectCard c;
        c.tint = afoqt ? 6 : 3;
        c.icon = t.icon;
        c.title = t.name;
        c.desc = afoqt ? "Every AFOQT knowledge and pilot section, vocabulary cards, and estimated composite scores."
                       : "Det 105 Warrior Knowledge Blocks 1\xE2\x80\x93" "11 as flashcards.";
        c.badge = afoqt ? plural(static_cast<int>(secs.size()), "section") : plural(static_cast<int>(secs.size()), "block");
        if (track_ == t.id) c.tag = "LAST USED";
        c.progress = ov.total ? static_cast<float>(ov.correct) / ov.total : 0.0f;
        c.footer = ov.total ? fmt("%s  \xC2\xB7  %d%% accuracy", plural(ov.sessions, "session").c_str(), ov.correct * 100 / ov.total)
                            : fmt(afoqt ? "%d written questions  \xC2\xB7  Not started yet" : "%d cards  \xC2\xB7  Not started yet", questions);
        c.primary = "Open  \xE2\x86\x92";
        c.primaryStyle = ButtonStyle::Primary;
        c.highlight = track_ == t.id;
        grid.next();
        if (subjectCard((std::string("home_") + t.id).c_str(), c, size) == 1) {
            if (track_ != t.id) switchTrack(t.id);
            else goTo(Screen::Dashboard);
            return;
        }
    }
}

void GuiApp::drawDashboard() {
    const TrackInfo& ti = trackInfo(track_);
    drawHeader(std::string(ti.label) + "  \xC2\xB7  DASHBOARD");

    // ---- Hero banner
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const float pad = 42.0f, wrap = std::min(560.0f, w * 0.6f);
    const char* blurb = ti.blurb;
    const float h = 38 + fontPx(fonts.hero) + 12 + textSize(fonts.body, blurb, wrap).y + 24 + 48 + 38;
    const ImVec2 a = ImGui::GetCursorScreenPos(), b = a + ImVec2(w, h);
    gradientRect(dl, a, b, col::HeroA, col::HeroB, 24.0f);
    dl->PushClipRect(a, b, true);  // decorations stay inside the banner
    dl->AddCircle(ImVec2(b.x - 150, a.y + 70), 200, u32(col::White, 0.10f), 96, 1.5f);
    dl->AddCircle(ImVec2(b.x - 110, a.y + 110), 140, u32(col::White, 0.08f), 96, 1.5f);
    dl->PopClipRect();

    float y = a.y + 38;
    dl->AddText(fonts.hero, fontPx(fonts.hero), ImVec2(a.x + pad, y), u32(col::White), ti.name);
    y += fontPx(fonts.hero) + 12;
    dl->AddText(fonts.body, fontPx(fonts.body), ImVec2(a.x + pad, y), u32(col::White, 0.88f), blurb, nullptr, wrap);
    y += textSize(fonts.body, blurb, wrap).y + 24;
    ImGui::SetCursorScreenPos(ImVec2(a.x + pad, y));
    if (button(track_ == "afoqt" ? "Start a 10-question warm-up  \xE2\x86\x92" : "Start a warm-up deck  \xE2\x86\x92",
               ButtonStyle::White, ImVec2(0, 48)))
        startWarmup();
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

    if (track_ != "afoqt") {
        spacedLabel("BLOCKS", col::Muted, 2.0f);
        drawSectionGrid(trackSections(), false, false);
        return;
    }
    spacedLabel("KNOWLEDGE SECTIONS", col::Muted, 2.0f);
    drawSectionGrid(registry_.inCategory("knowledge"), false, true);
    const auto pilot = registry_.inCategory("pilot");
    if (!pilot.empty()) {
        ImGui::Dummy(ImVec2(0, 14));
        spacedLabel("PILOT SECTIONS", col::Muted, 2.0f);
        textIn(fonts.body, col::Muted, "Table Reading, Instrument Comprehension, and Aviation Information count toward "
                                       "the Pilot score, along with Math Knowledge.");
        ImGui::Dummy(ImVec2(0, 2));
        drawSectionGrid(pilot, false, false);
    }
}

void GuiApp::drawSectionGrid(const std::vector<SubtestInfo>& sections, bool practicePage, bool addVocabCard) {
    const auto stats = progress_.stats();
    const std::string focus = focusId();
    Grid grid(2);
    const ImVec2 size(grid.cardW, 262);
    for (const auto& s : sections) {
        auto it = stats.find(s.id);
        const SubjectStyle st = styleFor(s);
        SubjectCard c;
        c.tint = subjectIndex(s.id);
        c.glyph = st.glyph;
        c.icon = st.icon;
        c.title = s.name;
        c.desc = st.desc;
        const size_t n = bank_.size(s.id);
        c.badge = n ? plural(static_cast<int>(n), track_ == "afoqt" ? "question" : "card") : "Unlimited";
        c.highlight = s.id == focus;
        if (c.highlight) c.tag = "FOCUS";
        c.progress = it != stats.end() ? static_cast<float>(it->second.accuracy() / 100.0) : 0.0f;
        if (practicePage) {
            c.footer = it != stats.end() ? fmt("%.0f%% accuracy", it->second.accuracy()) : "Not started yet";
            c.primary = "Practice  \xE2\x86\x92";
            c.primaryStyle = ButtonStyle::Primary;
        } else {
            c.footer = it != stats.end() ? fmt(track_ == "afoqt" ? "%.0f%% accuracy  \xC2\xB7  %.0f sec/question"
                                                                 : "%.0f%% known  \xC2\xB7  %.0f sec/card", it->second.accuracy(),
                                               it->second.avgSecondsPerQuestion())
                                         : "Not started yet";
            c.primary = "Train  \xE2\x86\x92";
        }
        std::string timed = fmt("Timed \xC2\xB7 %d min", (s.timeLimitSeconds + 30) / 60);
        if (practicePage) c.secondary = timed.c_str();
        grid.next();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const int hit = subjectCard(s.id.c_str(), c, size);
        cardFocusMenu(s, p, p + size);
        if (hit == 1) startSubtest(s, SessionMode::Practice);
        if (hit == 2) startSubtest(s, SessionMode::Timed);
    }
    if (!addVocabCard) return;
    const FlashcardDeck& d = activeDeck();
    const bool vocab = track_ == "afoqt";
    SubjectCard v;
    v.tint = 5;
    v.icon = Icon::Cards;
    v.title = vocab ? "Vocabulary cards" : "Memorize cards";
    v.desc = vocab ? "Flip through Word Knowledge flashcards with spaced repetition."
                   : "Creeds, oaths, the Code of Conduct, and every list you need word for word.";
    v.badge = plural(static_cast<int>(d.size()), vocab ? "word" : "card");
    v.progress = d.size() ? static_cast<float>(d.masteredCount()) / d.size() : 0.0f;
    v.footer = fmt("%d mastered  \xC2\xB7  %d to go", d.masteredCount(), static_cast<int>(d.size()) - d.masteredCount());
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
    if (track_ != "afoqt") {
        // One battery: every topic in this prep section, back to back
        const auto secs = trackSections();
        int total = 0;
        for (const auto& s : secs) total += s.timeLimitSeconds;
        gradientRect(dl, a, b, col::HeroA, col::HeroB, 18.0f);
        dl->AddText(fonts.heading, fontPx(fonts.heading), a + ImVec2(32, 30), u32(col::White), "Full knowledge check");
        std::string sub = fmt("Every block, timed and back to back. About %d min.", total / 60);
        dl->AddText(fonts.body, fontPx(fonts.body), a + ImVec2(32, 64), u32(col::White, 0.85f), sub.c_str(), nullptr, w - 320);
        const char* bl = "Start  \xE2\x86\x92";
        const float bw = textSize(fonts.strong, bl).x + 40;
        ImGui::SetCursorScreenPos(ImVec2(b.x - 32 - bw, a.y + 35));
        if (button(bl, ButtonStyle::White, ImVec2(bw, 48))) {
            queue_ = secs;
            startNextInQueue();
            return;
        }
        ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 24));
        spacedLabel("BLOCKS", col::Muted, 2.0f);
        drawSectionGrid(secs, true, false);
        return;
    }
    gradientRect(dl, a, b, col::HeroA, col::HeroB, 18.0f);
    dl->PushClipRect(a, b, true);
    dl->AddCircle(ImVec2(b.x - 260, a.y + 10), 120, u32(col::White, 0.08f), 64, 1.5f);
    dl->PopClipRect();
    int kSecs = 0, pSecs = 0;
    for (const auto& s : registry_.inCategory("knowledge")) kSecs += s.timeLimitSeconds;
    for (const auto& s : registry_.inCategory("pilot")) pSecs += s.timeLimitSeconds;
    dl->AddText(fonts.heading, fontPx(fonts.heading), a + ImVec2(32, 30), u32(col::White), "Full batteries");
    std::string sub = fmt("Every section in a group, timed and back to back. Knowledge: about %d min. Pilot: about %d min.",
                          kSecs / 60, pSecs / 60);
    dl->AddText(fonts.body, fontPx(fonts.body), a + ImVec2(32, 64), u32(col::White, 0.85f), sub.c_str(), nullptr, w - 520);
    const char* b1 = "Knowledge  \xE2\x86\x92";
    const char* b2 = "Pilot  \xE2\x86\x92";
    const float w1 = textSize(fonts.strong, b1).x + 40, w2 = textSize(fonts.strong, b2).x + 40;
    ImGui::SetCursorScreenPos(ImVec2(b.x - 32 - w2 - 12 - w1, a.y + 35));
    if (button(b1, ButtonStyle::White, ImVec2(w1, 48))) {
        startBattery("knowledge");
        return;
    }
    ImGui::SameLine(0, 12);
    if (button(b2, ButtonStyle::White, ImVec2(w2, 48))) {
        startBattery("pilot");
        return;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 24));

    spacedLabel("KNOWLEDGE SECTIONS", col::Muted, 2.0f);
    drawSectionGrid(registry_.inCategory("knowledge"), true, false);
    ImGui::Dummy(ImVec2(0, 14));
    spacedLabel("PILOT SECTIONS", col::Muted, 2.0f);
    drawSectionGrid(registry_.inCategory("pilot"), true, false);
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
    for (const auto& s : trackSections()) {
        if (bank_.size(s.id) == 0) continue;  // generated sections have nothing saved to review
        const int n = missed.count(s.id) ? missed.at(s.id) : 0;
        SubjectCard c;
        c.tint = subjectIndex(s.id);
        c.glyph = styleFor(s).glyph;
        c.icon = styleFor(s).icon;
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
    Icon clock = Icon::Clock;
    alignRight(textSize(fonts.strong, timer.c_str()).x + 52);
    ImGui::SetCursorPosY(y0);
    const bool urgent = left >= 0 && left <= 30;
    chip("timer", timer.c_str(), urgent ? col::BadSoft : col::Card, urgent ? col::Bad : col::Ink, false, true, &clock);
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

    // ---- Counts; timed tests also get a progress bar (practice, review and warm-up don't)
    const int answered = static_cast<int>(r.answers.size());
    if (sessionTag_ == "TIMED TEST")
        thinBar(static_cast<float>(answered) / engine_.total(), ImGui::GetContentRegionAvail().x, col::Accent, 8.0f);
    textIn(fonts.strong, col::InkSoft,
           fmt(cardMode_ ? "Card %d of %d" : "Question %d of %d", engine_.index() + 1, engine_.total()).c_str());
    std::string score = sessionTag_ == "TIMED TEST" ? std::string("Score shown at the end")
                        : cardMode_ ? fmt("%d know it  \xC2\xB7  %d still learning", r.correctCount(), answered - r.correctCount())
                                    : fmt("%d correct  \xC2\xB7  %d missed", r.correctCount(), answered - r.correctCount());
    alignRight(textSize(fonts.body, score.c_str()).x);
    textIn(fonts.body, col::Muted, score.c_str());
    ImGui::Dummy(ImVec2(0, 2));
    if (cardMode_) {
        drawCardQuestion(q);
        return;
    }

    // ---- The question card
    beginCard("question", ImVec2(0, std::max(420.0f, ImGui::GetContentRegionAvail().y - 26)), true, ImVec2(32, 30));
    const bool hasVisual = q.visual.kind != VisualKind::None;
    if (hasVisual) {
        const float vw = visualWidth(q.visual, ImGui::GetContentRegionAvail().x);
        ImGui::BeginGroup();
        drawVisual(q.visual, vw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 34);
        ImGui::BeginGroup();
    }
    if (!q.passage.empty()) {
        spacedLabel("PASSAGE", col::Muted, 1.8f);
        beginCallout();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24);
        textIn(fonts.body, col::InkSoft, q.passage.c_str());
        ImGui::PopTextWrapPos();
        endCallout(col::Bg, col::CalloutEdge);
        ImGui::Dummy(ImVec2(0, 4));
    }
    wrappedIn(fonts.prompt, col::Ink, q.prompt.c_str());
    ImGui::Dummy(ImVec2(0, 6));

    int clicked = -2;  // -2 = nothing, -1 = skip, 0+ = choice
    const float aw = ImGui::GetContentRegionAvail().x;
    const bool pictures = q.choiceVisuals.size() == q.choices.size();
    const int perRow = 3;
    const float picGap = 12.0f, picW = (aw - picGap * (perRow - 1)) / perRow, picH = std::floor(picW * 0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 10));
    for (int i = 0; pictures && i < static_cast<int>(q.choices.size()); ++i) {
        AnswerState st = AnswerState::Normal;
        if (feedback) {
            if (i == q.correctIndex) st = AnswerState::Correct;
            else if (i == engine_.lastChoice()) st = AnswerState::Wrong;
            else st = AnswerState::Dimmed;
        }
        if (i % perRow != 0) ImGui::SameLine(0, picGap);
        float shake = 0.0f;
        if (st == AnswerState::Wrong && anim >= 0 && anim < 0.4f) shake = std::sin(anim * 48.0f) * 7.0f * (1.0f - anim / 0.4f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shake);
        ImGui::PushID(i);
        const Visual& v = q.choiceVisuals[static_cast<size_t>(i)];
        const bool animated = st == AnswerState::Correct || st == AnswerState::Wrong;
        if (pictureAnswerCard("pic", static_cast<char>('A' + i), st, ImVec2(picW, picH), animated ? anim : -1.0f,
                              [&](ImDrawList* d, ImVec2 a, ImVec2 b) { drawAircraft(d, a, b, v.data[0], v.data[1], v.data[2]); }))
            clicked = i;
        ImGui::PopID();
        if (!feedback && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + i), false)) clicked = i;
    }
    for (int i = 0; !pictures && i < static_cast<int>(q.choices.size()); ++i) {
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
        if (!right) {
            const auto defs = definitionsFor(q, chosen, {&deck_, &vaWords_});
            if (!defs.empty()) {
                ImGui::Dummy(ImVec2(0, 4));
                spacedLabel(defs.size() == 1 ? "DEFINITION" : "DEFINITIONS", col::Muted, 1.6f);
                for (const Flashcard* c : defs) {
                    std::string word = c->word;
                    if (!word.empty()) word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
                    textIn(fonts.strong, col::Ink, (word + (c->partOfSpeech.empty() ? "" : "  (" + c->partOfSpeech + ")")).c_str());
                    wrappedIn(fonts.body, col::InkSoft, c->definition.c_str());
                }
            }
        }
        endCallout(right ? col::GoodSoft : col::BadSoft, right ? col::Good : col::Bad);
        if (button("Next  \xE2\x86\x92   Enter") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            engine_.next();
            feedbackTime_ = -1.0;
        }
    }
    if (hasVisual) ImGui::EndGroup();
    if (!sessionTips_.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        divider();
        const std::string keys = std::string("A\xE2\x80\x93") + static_cast<char>('A' + q.choices.size() - 1);
        wrappedIn(fonts.small, col::Muted, ("Tip: " + sessionTips_ + "   Keys: " + keys + " to answer, S to skip, Enter for next.").c_str());
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

// Warrior Knowledge: the question on the front, the answer on the back. You grade
// yourself: swipe right if you knew it, left if you didn't. Right counts as correct;
// left counts as missed, so the card goes on the review list.
void GuiApp::drawCardQuestion(const Question& q) {
    ImGui::Dummy(ImVec2(0, 8));
    const float minH = std::clamp(ImGui::GetContentRegionAvail().y - 170, 320.0f, 520.0f);
    const std::string& answer = q.choices[static_cast<size_t>(q.correctIndex)];
    const std::string blockLabel = fmt("BLOCK %d", std::atoi(q.subtest.c_str() + std::strlen("wk_block")));
    const int swiped = swipeCard(
        "wkcard", swipe_, minH,
        [&](float left, float width) {
            cardText(fonts.label, col::Muted, blockLabel.c_str(), left, width);
            ImGui::Dummy(ImVec2(0, 10));
            cardText(fonts.title, col::Ink, q.prompt.c_str(), left, width);
        },
        [&](float left, float width) {
            cardText(fonts.label, col::Muted, upper(q.prompt).c_str(), left, width);
            ImGui::Dummy(ImVec2(0, 12));
            cardText(answer.size() < 90 ? fonts.title : fonts.prompt, col::Accent, answer.c_str(), left, width);
        });

    // Buttons for people who'd rather click than drag
    ImGui::Dummy(ImVec2(0, 10));
    const float bw = 210.0f, gap = 14.0f, w = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (w - bw * 3 - gap * 2) * 0.5f));
    if (button("\xE2\x86\x90  Still learning", ButtonStyle::Danger, ImVec2(bw, 50))) swipe_.send(-1);
    ImGui::SameLine(0, gap);
    if (button(swipe_.flipped ? "Show question" : "Flip card", ButtonStyle::Outline, ImVec2(bw, 50))) swipe_.flip();
    ImGui::SameLine(0, gap);
    if (button("Know it  \xE2\x86\x92", ButtonStyle::Primary, ImVec2(bw, 50))) swipe_.send(1);
    ImGui::Dummy(ImVec2(0, 6));
    divider();
    wrappedIn(fonts.small, col::Muted,
              ((sessionTips_.empty() ? std::string() : "Tip: " + sessionTips_ + "   ") +
               "Click the card or press Space to flip. Drag or swipe it right if you knew it, left if you didn't "
               "(or use the arrow keys).").c_str());

    if (swiped != 0) {
        const bool knew = swiped > 0;
        // "Didn't know" is recorded as a wrong choice so it counts as answered but missed.
        const int wrong = (q.correctIndex + 1) % static_cast<int>(q.choices.size());
        engine_.submit(knew ? q.correctIndex : wrong);
        if (engine_.showingFeedback()) engine_.next();  // no feedback step: the back of the card was the feedback
        if (knew) sound_.correct();
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
    statCard(ImVec2(w, 142), cardMode_ ? "Knew it" : "Correct", fmt("%d / %d", r.correctCount(), total).c_str(),
             cardMode_ ? fmt("%d still learning", total - r.correctCount()).c_str()
                       : fmt("%d answered", r.answeredCount()).c_str());
    ImGui::SameLine(0, kGap);
    statCard(ImVec2(w, 142), "Time used", mmss(static_cast<int>(r.totalSeconds)).c_str(),
             r.answeredCount() ? fmt(cardMode_ ? "%.0f sec per card" : "%.0f sec per answer", r.totalSeconds / r.answeredCount()).c_str() : "");
    ImGui::Dummy(ImVec2(0, 4));

    if (r.timeExpired) {
        beginCallout();
        textIn(fonts.strong, col::Ink, cardMode_ ? "Time ran out before you finished. Cards you didn't get to count as missed."
                                                 : "Time ran out before you finished. Unanswered questions count as missed.");
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
    spacedLabel(cardMode_ ? "STILL LEARNING" : "MISSED QUESTIONS", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 2));
    const auto& qs = engine_.questions();
    bool any = false;
    for (size_t i = 0; i < r.answers.size(); ++i) {
        if (r.answers[i].correct) continue;
        any = true;
        const Question& q = qs[i];
        wrappedIn(fonts.strong, col::Ink, q.prompt.c_str());
        const int chosen = r.answers[i].chosenIndex;
        if (cardMode_) {
            wrappedIn(fonts.body, col::Good, fmt("Answer:  %s", q.choices[q.correctIndex].c_str()).c_str());
        } else {
            textIn(fonts.body, col::Bad, chosen >= 0 ? fmt("Your answer:  %s", q.choices[chosen].c_str()).c_str() : "Not answered");
            textIn(fonts.body, col::Good, fmt("Correct:  %s", q.choices[q.correctIndex].c_str()).c_str());
        }
        wrappedIn(fonts.small, col::Muted, q.explanation.c_str());
        divider();
    }
    if (!any) textIn(fonts.heading, col::Good, cardMode_ ? "None. You knew every card!" : "None. Perfect score!");
    endCard();
}

// ================================================================ flashcards

void GuiApp::drawFlashcards() {
    // AFOQT: vocabulary cards. Warrior Knowledge: memorize cards (title on the
    // front, the full text on the back; recite it, then flip to check).
    FlashcardDeck& deck = activeDeck();
    const bool vocab = track_ == "afoqt";
    const char* deckName = vocab ? "Vocabulary cards" : "Memorize cards";
    const char* unit = vocab ? "words" : "cards";
    drawHeader(vocab ? "VOCABULARY CARDS" : "MEMORIZE CARDS");
    const int mastered = deck.masteredCount();
    const int total = static_cast<int>(deck.size());

    // ---- Start screen
    if (!flashActive_) {
        pageTitle(deckName, vocab ? "Word Knowledge flashcards with spaced repetition: words you miss come back "
                                    "sooner, and a word is mastered after 3 correct answers in a row."
                                  : "Everything in the handbook you need word for word. Recite it from memory, then flip to "
                                    "check. Cards you miss come back sooner; 3 in a row and it's mastered.");
        if (!message_.empty()) {
            beginCallout();
            textIn(fonts.strong, col::Ink, message_.c_str());
            endCallout(col::AccentSoft, col::Accent);
        }
        const float w3 = (ImGui::GetContentRegionAvail().x - kGap * 2) / 3;
        statCard(ImVec2(w3, 142), "Mastered", fmt("%d", mastered).c_str(), fmt("of %d %s", total, unit).c_str(), col::Accent);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Still learning", fmt("%d", total - mastered).c_str(), "Not mastered yet");
        ImGui::SameLine(0, kGap);
        // Options card with the "definition first" toggle (vocabulary only)
        if (!vocab) {
            statCard(ImVec2(w3, 142), "Blocks", "11", "From your Det 105 handbook");
        } else {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(w3, 142));
        cardShape(ImGui::GetWindowDrawList(), p, ImVec2(w3, 142));
        ImGui::SetCursorScreenPos(p + ImVec2(24, 22));
        textIn(fonts.strong, col::InkSoft, "Definition first");
        ImGui::SetCursorScreenPos(p + ImVec2(24, 58));
        if (toggle("deffirst", &definitionFirst_)) saveSettings();
        ImGui::SetCursorScreenPos(p + ImVec2(24, 100));
        textIn(fonts.small, col::Muted, "Guess the word from its meaning");
        ImGui::EndGroup();
        }
        ImGui::Dummy(ImVec2(0, 16));

        if (vocab) {
            drawRepetition();
            if (flashActive_) return;  // a session just started
            ImGui::Dummy(ImVec2(0, 16));
        }

        textIn(fonts.title, col::Ink, vocab ? "Other sessions" : "Start a session");
        ImGui::Dummy(ImVec2(0, 4));
        const Icon ic[3] = {Icon::Bolt, Icon::Target, Icon::List};
        struct Opt { const char* title; std::string desc; std::string badge; size_t count; bool learning; };
        Opt opts[3] = {
            {vocab ? "Quick 20" : "Quick 10", vocab ? "Twenty cards, your weakest words first." : "Ten cards, the ones you know least first.",
             vocab ? "20 cards" : "10 cards", vocab ? 20u : 10u, false},
            {"Still learning", vocab ? "Only words you haven't mastered yet." : "Only cards you haven't mastered yet.", "Up to 30", 30, true},
            {"Whole deck", vocab ? "Every word once, in random order." : "Every card once, in random order.", plural(total, "card"), 0, false},
        };
        Grid grid(3);
        for (int i = 0; i < 3; ++i) {
            SubjectCard c;
            c.tint = i == 0 ? 2 : i == 1 ? 0 : 1;
            c.icon = ic[i];
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
    if (deck.sessionDone() && repSession_) {
        const int streak = repDeck_.streak();
        pageTitle("Deck complete",
                  repCounted_ ? fmt("You went through your whole deck today. Your streak is now %s.",
                                    plural(streak, "day").c_str()).c_str()
                              : "You already finished your deck today, so the streak stays the same. Come back tomorrow "
                                "to add another day.");
        const float w3 = (ImGui::GetContentRegionAvail().x - kGap * 2) / 3;
        statCard(ImVec2(w3, 142), "Streak", plural(streak, "day").c_str(),
                 fmt("Best: %s", plural(repDeck_.bestStreak(), "day").c_str()).c_str(), col::Accent);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Knew it", fmt("%d", deck.sessionGotIt()).c_str(), "Moved up a level", col::Good);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Still learning", fmt("%d", deck.sessionMissed()).c_str(), "Repeated until you knew it", col::Bad);
        ImGui::Dummy(ImVec2(0, 8));
        if (button("Back to vocabulary cards")) flashActive_ = false;
        return;
    }
    if (deck.sessionDone()) {
        pageTitle("Session complete", vocab ? "Nice work. Words you marked \"still learning\" will come back sooner next time."
                                            : "Nice work. Cards you marked \"still learning\" will come back sooner next time.");
        const float w3 = (ImGui::GetContentRegionAvail().x - kGap * 2) / 3;
        statCard(ImVec2(w3, 142), "Knew it", fmt("%d", deck.sessionGotIt()).c_str(), "Moved up a level", col::Good);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Still learning", fmt("%d", deck.sessionMissed()).c_str(), "Coming back soon", col::Bad);
        ImGui::SameLine(0, kGap);
        statCard(ImVec2(w3, 142), "Mastered", fmt("%d", mastered).c_str(), fmt("of %d %s", total, unit).c_str(), col::Accent);
        ImGui::Dummy(ImVec2(0, 8));
        if (button(vocab ? "Back to vocabulary cards" : "Back to memorize cards")) flashActive_ = false;
        return;
    }

    // ---- A card
    const Flashcard& c = deck.current();
    const float ty = ImGui::GetCursorPosY();
    textIn(fonts.title, col::Ink, repSession_ ? "Spaced repetition" : deckName);
    alignRight(textSize(fonts.strong, "End session").x + 40);
    ImGui::SetCursorPosY(ty);
    if (button("End session", ButtonStyle::Outline)) {
        flashActive_ = false;
        return;
    }
    textIn(fonts.body, col::Muted, fmt("%d cards left  \xC2\xB7  %d known  \xC2\xB7  %d still learning",
                                       static_cast<int>(deck.remaining()), deck.sessionGotIt(), deck.sessionMissed()).c_str());
    ImGui::Dummy(ImVec2(0, 4));

    const float minH = std::clamp(ImGui::GetContentRegionAvail().y - 170, 300.0f, vocab ? 420.0f : 520.0f);
    auto posName = [&] {
        return c.partOfSpeech == "adj" ? "ADJECTIVE" : c.partOfSpeech == "verb" ? "VERB" : "NOUN";
    };
    const int swiped = swipeCard(
        "flash", swipe_, minH,
        [&](float left, float width) {
            if (!vocab) {  // memorize card: block label and title; recite the text, then flip
                cardText(fonts.label, col::Muted, c.partOfSpeech.c_str(), left, width);
                ImGui::Dummy(ImVec2(0, 10));
                cardText(fonts.title, col::Accent, c.word.c_str(), left, width);
                ImGui::Dummy(ImVec2(0, 14));
                cardText(fonts.body, col::Muted, "Recite it from memory, out loud or on paper. Then flip to check.", left, width);
            } else if (definitionFirst_) {
                cardText(fonts.label, col::Muted, "DEFINITION", left, width);
                ImGui::Dummy(ImVec2(0, 10));
                cardText(fonts.title, col::Ink, c.definition.c_str(), left, width);
                ImGui::Dummy(ImVec2(0, 14));
                cardText(fonts.body, col::Muted, "Which word has this meaning?", left, width);
            } else {
                cardText(fonts.label, col::Muted, posName(), left, width);
                ImGui::Dummy(ImVec2(0, 6));
                cardText(fonts.hero, col::Accent, c.word.c_str(), left, width);
                ImGui::Dummy(ImVec2(0, 14));
                cardText(fonts.body, col::Muted, "What does this word mean?", left, width);
            }
        },
        [&](float left, float width) {
            if (!vocab) {
                cardText(fonts.label, col::Muted, (c.partOfSpeech + "  \xC2\xB7  " + upper(c.word)).c_str(), left, width);
                ImGui::Dummy(ImVec2(0, 12));
                cardText(fonts.prompt, col::Ink, c.definition.c_str(), left, width);
                return;
            }
            cardText(fonts.label, col::Muted, posName(), left, width);
            ImGui::Dummy(ImVec2(0, 4));
            cardText(fonts.title, col::Accent, c.word.c_str(), left, width);
            ImGui::Dummy(ImVec2(0, 12));
            cardText(fonts.prompt, col::Ink, c.definition.c_str(), left, width);
            ImGui::Dummy(ImVec2(0, 12));
            cardText(fonts.label, col::Muted, "SYNONYM", left, width);
            ImGui::Dummy(ImVec2(0, 4));
            cardText(fonts.strong, col::Good, c.synonym.c_str(), left, width);
            if (!c.example.empty()) {
                ImGui::Dummy(ImVec2(0, 10));
                cardText(fonts.body, col::InkSoft, ("\"" + c.example + "\"").c_str(), left, width);
            }
        });

    ImGui::Dummy(ImVec2(0, 10));
    const float bw = 210.0f, gap = 14.0f, w = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (w - bw * 3 - gap * 2) * 0.5f));
    if (button("\xE2\x86\x90  Still learning", ButtonStyle::Danger, ImVec2(bw, 50))) swipe_.send(-1);
    ImGui::SameLine(0, gap);
    if (button(swipe_.flipped ? "Flip back" : "Flip card", ButtonStyle::Outline, ImVec2(bw, 50))) swipe_.flip();
    ImGui::SameLine(0, gap);
    if (button("Know it  \xE2\x86\x92", ButtonStyle::Primary, ImVec2(bw, 50))) swipe_.send(1);
    ImGui::Dummy(ImVec2(0, 6));
    divider();
    wrappedIn(fonts.small, col::Muted, "Click the card or press Space to flip. Drag or swipe it right if you knew it, "
                                       "left if you're still learning (or use the arrow keys).");
    if (swiped != 0) {
        if (swiped > 0) sound_.correct();
        deck.mark(swiped > 0);
        if (repSession_ && deck.sessionDone()) repCounted_ = repDeck_.completePass();
    }
}

// The spaced repetition deck on the vocabulary cards page: a small deck that grows
// 3 words at a time, and a streak for going through all of it once a day.
void GuiApp::drawRepetition() {
    const auto& words = repDeck_.words();
    const int n = static_cast<int>(words.size());
    const int streak = repDeck_.streak();
    const bool done = repDeck_.doneToday();

    textIn(fonts.title, col::Ink, "Spaced repetition");
    wrappedIn(fonts.body, col::Muted,
              "Your own deck, starting with 3 words. Go through the whole deck once a day to keep your streak; "
              "words you miss come back until you know them. Add 3 more words whenever you're ready.");
    ImGui::Dummy(ImVec2(0, 4));

    const float w3 = (ImGui::GetContentRegionAvail().x - kGap * 2) / 3;
    statCard(ImVec2(w3, 142), "Streak", plural(streak, "day").c_str(),
             fmt("Best: %s", plural(repDeck_.bestStreak(), "day").c_str()).c_str(), streak > 0 ? col::Accent : col::Ink);
    ImGui::SameLine(0, kGap);
    statCard(ImVec2(w3, 142), "Today", done ? "Done" : "Not yet",
             done ? "Come back tomorrow for the next day" : streak > 0 ? "Finish your deck to keep the streak"
                                                                      : "Finish your deck to start a streak",
             done ? col::Good : col::Bad);
    ImGui::SameLine(0, kGap);
    int mastered = 0;
    for (const auto& w : words) mastered += deck_.box(w) >= FlashcardDeck::kMasteredBox ? 1 : 0;
    statCard(ImVec2(w3, 142), "In your deck", plural(n, "word").c_str(), fmt("%d mastered", mastered).c_str());

    ImGui::Dummy(ImVec2(0, 4));
    std::string list;
    for (const auto& w : words) list += (list.empty() ? "" : ",  ") + w;
    wrappedIn(fonts.small, col::Muted, ("Words: " + list).c_str());
    ImGui::Dummy(ImVec2(0, 4));

    if (button(fmt("Practice my deck  (%s)  \xE2\x86\x92", plural(n, "card").c_str()).c_str())) {
        startRepetition();
        return;
    }
    const int canAdd = repDeck_.canAdd(deck_);
    if (canAdd > 0) {
        ImGui::SameLine(0, 12);
        if (button(fmt("+  Add %d more %s", canAdd, canAdd == 1 ? "word" : "words").c_str(), ButtonStyle::Outline))
            repDeck_.addMore(deck_);
    }
    const int last = repDeck_.lastBatchSize();
    if (last > 0) {
        ImGui::SameLine(0, 12);
        if (button(fmt("Remove the %d just added", last).c_str(), ButtonStyle::Outline)) repDeck_.removeLastBatch();
    }
    if (canAdd == 0) wrappedIn(fonts.small, col::Muted, "Every vocabulary word is in your deck.");
}

// ================================================================ progress

void GuiApp::drawProgress() {
    drawHeader("MY PROGRESS");
    pageTitle("My progress", "Accuracy and pace for each section. Your focus area is highlighted.");
    drawStatRow();
    ImGui::Dummy(ImVec2(0, 10));
    if (track_ == "afoqt") {  // composites are an AFOQT idea
        drawComposites();
        ImGui::Dummy(ImVec2(0, 10));
    }

    const auto stats = progress_.stats();
    const std::string focusSid = focusId();
    const float listW = ImGui::GetContentRegionAvail().x * 0.64f;

    // ---- Left: accuracy by section
    const float rowH = 84.0f;
    const auto sections = trackSections();
    const float listH = std::max(470.0f, 70 + rowH * sections.size());
    beginCard("bysection", ImVec2(listW, listH));
    spacedLabel("ACCURACY BY SECTION", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 4));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    for (const auto& s : sections) {
        auto it = stats.find(s.id);
        const bool has = it != stats.end();
        const bool focus = s.id == focusSid;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, rowH - 12));
        Icon rowIcon = styleFor(s).icon;
        iconTile(dl, p + ImVec2(0, 6), 44, tint(subjectIndex(s.id)), styleFor(s).glyph,
                 styleFor(s).glyph ? nullptr : &rowIcon);
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

    // ---- Right: focus area (sound, theme, and card options live on the Settings page)
    ImGui::SameLine(0, kGap);
    beginCard("settings", ImVec2(0, listH));
    spacedLabel("FOCUS AREA", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 4));
    wrappedIn(fonts.small, col::Muted, "The section highlighted across the app. Automatic picks your lowest accuracy.");
    ImGui::Dummy(ImVec2(0, 4));
    textIn(fonts.strong, col::Ink, "Current focus");
    const SubtestInfo* fi = registry_.find(focusSid);
    textIn(fonts.small, col::Muted, fi ? (fi->name + (pinnedFocus_.empty() ? "  (automatic)" : "  (pinned)")).c_str()
                                       : "Not set yet");
    ImGui::Dummy(ImVec2(0, 2));
    if (button("Change focus area", ButtonStyle::Outline)) ImGui::OpenPopup("focuspicker2");
    if (beginPopup("focuspicker2")) {
        focusMenuItems();
        ImGui::EndPopup();
    }
    ImGui::Dummy(ImVec2(0, 6));
    divider();
    wrappedIn(fonts.small, col::Muted, "Sound, dark mode, and flashcard options are on the Settings page.");
    if (button("Open settings", ButtonStyle::Outline)) goTo(Screen::Settings);
    endCard();
}

// ================================================================ settings

void GuiApp::drawSettings() {
    drawHeader("SETTINGS");
    pageTitle("Settings", "Changes save right away.");

    // One card per group, each row: title and description on the left, a switch on the right
    auto row = [&](const char* id, const char* title, const char* sub, bool* v) {
        const float y = ImGui::GetCursorPosY();
        textIn(fonts.strong, col::Ink, title);
        alignRight(46);
        ImGui::SetCursorPosY(y + 2);
        const bool changed = toggle(id, v);
        ImGui::SetCursorPosY(y + 28);
        wrappedIn(fonts.small, col::Muted, sub);
        return changed;
    };
    const float w = ImGui::GetContentRegionAvail().x;
    const float colW = (w - kGap) / 2;

    // ---- Sound
    beginCard("set_sound", ImVec2(colW, 220));
    spacedLabel("SOUND", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 4));
    if (row("snd", "Sound effects", "A chime when you get an answer right, and a low tone when you miss.", &sound_.enabled)) {
        saveSettings();
        if (sound_.enabled) sound_.correct();  // let them hear it
    }
    ImGui::Dummy(ImVec2(0, 10));
    if (button("Play the correct sound", ButtonStyle::Outline) && sound_.enabled) sound_.correct();
    ImGui::SameLine(0, 10);
    if (button("Play the wrong sound", ButtonStyle::Outline) && sound_.enabled) sound_.wrong();
    endCard();

    // ---- Appearance: two swatches to pick from
    ImGui::SameLine(0, kGap);
    beginCard("set_look", ImVec2(0, 220));
    spacedLabel("APPEARANCE", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 4));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float tw = (ImGui::GetContentRegionAvail().x - 14) / 2, th = 128;
    for (int i = 0; i < 2; ++i) {
        const bool dark = i == 1, picked = darkMode_ == dark;
        if (i) ImGui::SameLine(0, 14);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("theme", ImVec2(tw, th));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        // Mini preview of the app in that theme: sidebar, background, and a card
        const ImU32 bg = dark ? IM_COL32(16, 21, 34, 255) : IM_COL32(243, 245, 250, 255);
        const ImU32 card = dark ? IM_COL32(26, 33, 50, 255) : IM_COL32(255, 255, 255, 255);
        const ImU32 line = dark ? IM_COL32(60, 72, 100, 255) : IM_COL32(220, 226, 238, 255);
        const ImVec2 pa = p + ImVec2(10, 10), pb = p + ImVec2(tw - 10, th - 40);
        dl->AddRectFilled(p, p + ImVec2(tw, th), u32(picked ? col::AccentSoft : col::Bg), 12.0f);
        dl->AddRectFilled(pa, pb, bg, 6.0f);
        dl->AddRectFilled(pa, ImVec2(pa.x + 28, pb.y), u32(col::Sidebar), 6.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddRectFilled(pa + ImVec2(38, 10), ImVec2(pb.x - 10, pa.y + 46), card, 4.0f);
        dl->AddRectFilled(pa + ImVec2(48, 20), pa + ImVec2(110, 26), IM_COL32(63, 95, 218, 255), 2.0f);
        dl->AddRectFilled(pa + ImVec2(48, 32), pa + ImVec2(140, 36), line, 2.0f);
        dl->AddRect(p, p + ImVec2(tw, th), u32(picked ? col::Accent : hov ? col::Ring : col::CardBorder), 12.0f, 0, picked ? 2.0f : 1.2f);
        const char* label = dark ? "Dark" : "Light";
        Icon ic = dark ? Icon::Moon : Icon::Sparkle;
        drawIcon(dl, ic, p + ImVec2(22, th - 20), 14, u32(picked ? col::Accent : col::InkSoft));
        dl->AddText(fonts.strong, fontPx(fonts.strong), p + ImVec2(38, th - 30), u32(picked ? col::Accent : col::Ink), label);
        if (picked) drawIcon(dl, Icon::Check, p + ImVec2(tw - 22, th - 20), 16, u32(col::Accent));
        if (clicked && !picked) {
            darkMode_ = dark;
            setDarkMode(darkMode_);
            saveSettings();
        }
    }
    endCard();
    ImGui::Dummy(ImVec2(0, kGap - 12));

    // ---- Flashcards
    beginCard("set_cards", ImVec2(colW, 150));
    spacedLabel("VOCABULARY CARDS", col::Muted, 1.8f);
    ImGui::Dummy(ImVec2(0, 4));
    if (row("def", "Definition first", "AFOQT vocabulary cards show the meaning, and you guess the word.", &definitionFirst_))
        saveSettings();
    endCard();
}

void GuiApp::drawComposites() {
    const auto estimates = estimateComposites(progress_.stats());
    const int perRow = 3;
    const float tileH = 150.0f, gap = 14.0f;
    const int rows = (static_cast<int>(estimates.size()) + perRow - 1) / perRow;
    beginCard("composites", ImVec2(0, 130 + rows * tileH + (rows - 1) * gap));
    spacedLabel("ESTIMATED COMPOSITE SCORES", col::Muted, 1.8f);
    wrappedIn(fonts.small, col::Muted,
              "The average of your accuracy on the sections in each composite. The real AFOQT reports "
              "percentiles (1\xE2\x80\x93" "99) compared with other test takers, so use these as a guide to "
              "where you're strong, not as your official score.");
    ImGui::Dummy(ImVec2(0, 4));

    auto nameOf = [&](const std::string& id) {
        const SubtestInfo* s = registry_.find(id);
        return s ? s->name : id;
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float tileW = (ImGui::GetContentRegionAvail().x - gap * (perRow - 1)) / perRow;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    for (size_t i = 0; i < estimates.size(); ++i) {
        const CompositeEstimate& e = estimates[i];
        const int r = static_cast<int>(i) / perRow, c = static_cast<int>(i) % perRow;
        const ImVec2 p = origin + ImVec2(c * (tileW + gap), r * (tileH + gap));
        const bool pilot = i == 0;
        dl->AddRectFilled(p, p + ImVec2(tileW, tileH), u32(pilot ? col::AccentSoft : col::Bg), 12.0f);
        if (pilot) dl->AddRect(p, p + ImVec2(tileW, tileH), u32(col::Accent, 0.5f), 12.0f, 0, 1.3f);

        const float pad = 16.0f, inner = tileW - pad * 2;
        dl->AddText(fonts.strong, fontPx(fonts.strong), p + ImVec2(pad, 14), u32(col::Ink), e.info->name.c_str());
        dl->AddText(fonts.small, fontPx(fonts.small), p + ImVec2(pad, 37), u32(col::Muted), e.info->description.c_str());

        const ImVec4 color = !e.hasData() ? col::Muted : e.score >= 80 ? col::Good : e.score >= 60 ? col::Accent : col::Bad;
        std::string big = e.hasData() ? fmt("%.0f%%", e.score) : "\xE2\x80\x94";
        ImVec2 bs = textSize(fonts.heading, big.c_str());
        dl->AddText(fonts.heading, fontPx(fonts.heading), ImVec2(p.x + tileW - pad - bs.x, p.y + 12), u32(color), big.c_str());

        const float by = p.y + 66;
        dl->AddRectFilled(ImVec2(p.x + pad, by), ImVec2(p.x + pad + inner, by + 6), u32(col::Track), 3.0f);
        if (e.hasData())
            dl->AddRectFilled(ImVec2(p.x + pad, by), ImVec2(p.x + pad + inner * static_cast<float>(e.score / 100.0), by + 6),
                              u32(color), 3.0f);

        // What's in it, and what's holding it back
        std::string line1, line2;
        if (!e.hasData()) {
            line1 = fmt("%d sections, none practiced yet", static_cast<int>(e.info->sections.size()));
        } else {
            line1 = "Weakest: " + nameOf(e.weakest);
            if (!e.complete()) {
                line2 = "Not practiced: ";
                for (size_t k = 0; k < e.missing.size(); ++k) line2 += (k ? ", " : "") + nameOf(e.missing[k]);
            } else {
                line2 = fmt("Based on all %d sections", static_cast<int>(e.info->sections.size()));
            }
        }
        dl->PushClipRect(p, p + ImVec2(tileW - pad, tileH), true);
        dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(p.x + pad, by + 18), u32(col::InkSoft), line1.c_str());
        if (!line2.empty())  // can be a long list of names, so it wraps
            dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(p.x + pad, by + 38),
                        u32(e.complete() ? col::Muted : col::Bad), line2.c_str(), nullptr, inner);
        dl->PopClipRect();
    }
    ImGui::Dummy(ImVec2(0, rows * tileH + (rows - 1) * gap));
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
    cardMode_ = track_ == "warrior";
    swipe_ = SwipeCard{};
    engine_.start(s.id, std::move(questions), mode, s.timeLimitSeconds);
    screen_ = Screen::Quiz;
}

void GuiApp::startBattery(const std::string& category) {
    queue_ = registry_.inCategory(category);
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
    // Two questions from each section, mixed together (AFOQT: the knowledge sections)
    std::vector<Question> questions;
    for (const auto& s : track_ == "afoqt" ? registry_.inCategory("knowledge") : trackSections()) {
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
    cardMode_ = track_ == "warrior";
    swipe_ = SwipeCard{};
    engine_.start(track_ == "afoqt" ? "warmup" : "warmup_" + track_, std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::startReview(const std::string& subtestId) {
    auto questions = bank_.drawByIds(progress_.missedQuestionIds(), rng_);
    questions.erase(std::remove_if(questions.begin(), questions.end(),
                                   [&](const Question& q) { return !inTrack(q.subtest); }),
                    questions.end());
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
    cardMode_ = track_ == "warrior";
    swipe_ = SwipeCard{};
    engine_.start("review", std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::startFlashcards(size_t count, bool learningOnly) {
    activeDeck().startSession(count, learningOnly, rng_);
    swipe_ = SwipeCard{};
    repSession_ = repCounted_ = false;
    if (activeDeck().sessionDone()) {
        message_ = "Nothing to study. You've mastered them all!";
        return;
    }
    message_.clear();
    flashActive_ = true;
}

void GuiApp::startRepetition() {
    deck_.startSession(repDeck_.words(), rng_);
    swipe_ = SwipeCard{};
    repSession_ = true;
    repCounted_ = false;
    message_.clear();
    flashActive_ = !deck_.sessionDone();
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
    for (const auto& s : trackSections())
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
    if (id == "aviation_information") return 6;
    if (id == "table_reading") return 7;
    if (id == "instrument_comprehension") return 8;
    if (id == "block_counting") return 9;
    if (id.rfind("wk_block", 0) == 0) {  // Warrior Knowledge blocks: cycle through the colors
        static const int cycle[] = {0, 3, 2, 1, 4, 7, 5, 6, 8, 9, 0};
        return cycle[(std::atoi(id.c_str() + 8) - 1 + 11) % 11];
    }
    const auto& all = registry_.all();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].id == id) return static_cast<int>(i);
    return 6;
}

std::string GuiApp::focusId() const {
    if (!pinnedFocus_.empty() && inTrack(pinnedFocus_)) return pinnedFocus_;
    return weakestId();
}

void GuiApp::setFocus(const std::string& id) {
    pinnedFocus_ = id;
    pinnedByTrack_[track_] = id;
    saveSettings();
}

bool GuiApp::inTrackOf(const std::string& subtestId, const std::string& track) const {
    if (subtestId == (track == "afoqt" ? std::string("warmup") : "warmup_" + track)) return true;
    const SubtestInfo* s = registry_.find(subtestId);
    return s && s->track == track;
}

void GuiApp::switchTrack(const std::string& track) {
    track_ = track;
    pinnedFocus_ = pinnedByTrack_[track_];
    saveSettings();
    goTo(Screen::Dashboard);
}

void GuiApp::loadSettings() {
    std::ifstream in(apppaths::userFile("afoqt_settings.json"));
    if (!in) return;  // first run
    try {
        nlohmann::json j;
        in >> j;
        pinnedByTrack_["afoqt"] = j.value("focus", "");  // older files only had the AFOQT focus
        if (j.contains("focus_by_track"))
            for (auto& [t, id] : j["focus_by_track"].items()) pinnedByTrack_[t] = id.get<std::string>();
        sound_.enabled = j.value("sound", true);
        darkMode_ = j.value("dark", false);
        definitionFirst_ = j.value("definition_first", false);
        track_ = j.value("track", "afoqt");
        if (track_ != trackInfo(track_).id) track_ = "afoqt";  // unknown track
    } catch (...) {
        pinnedByTrack_.clear();  // unreadable file: use defaults
        track_ = "afoqt";
    }
    pinnedFocus_ = pinnedByTrack_[track_];
}

void GuiApp::saveSettings() const {
    nlohmann::json j;
    auto pins = pinnedByTrack_;
    pins[track_] = pinnedFocus_;
    j["focus"] = pins["afoqt"];
    j["focus_by_track"] = pins;
    j["sound"] = sound_.enabled;
    j["dark"] = darkMode_;
    j["definition_first"] = definitionFirst_;
    j["track"] = track_;
    std::ofstream(apppaths::userFile("afoqt_settings.json")) << j.dump(2);
}

// ================================================================ stats

GuiApp::Overall GuiApp::overall() const { return overallFor(track_); }

GuiApp::Overall GuiApp::overallFor(const std::string& track) const {
    Overall o;
    for (const auto& [id, st] : progress_.stats()) {
        if (!inTrackOf(id, track)) continue;
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
        if (!registry_.find(sid) || !inTrack(sid)) continue;  // skip warm-ups and other tracks
        if (st.total > 0 && st.accuracy() < worst) {
            worst = st.accuracy();
            id = sid;
        }
    }
    return id;
}

std::map<std::string, int> GuiApp::missedBySubtest() const {
    std::map<std::string, int> out;
    for (const auto& q : bank_.byIds(progress_.missedQuestionIds()))
        if (inTrack(q.subtest)) out[q.subtest]++;
    return out;
}
