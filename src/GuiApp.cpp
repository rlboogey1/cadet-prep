#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "GuiApp.h"
#include "Composites.h"
#include "AtomicFile.h"
#include "QuestionGenerator.h"
#include "SyncMerge.h"
#include "VerbalTypes.h"
#include "Visuals.h"
#include "Widgets.h"
#include "imgui.h"
#include "imgui_internal.h"  // touch scrolling: the window under the finger, SetScrollY
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace ui;

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace {

constexpr float kSidebar = 288.0f;       // width of the navy sidebar
constexpr float kGap = 18.0f;            // space between cards
constexpr float kCompactBelow = 1000.0f; // narrower than this (points): phone layout
constexpr float kTopBar = 58.0f;         // phone layout: height of the navy top bar

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

// The label over a card's word: its part of speech, or for the roots deck its kind.
const char* partName(const std::string& pos) {
    return pos == "adj"      ? "ADJECTIVE"
           : pos == "verb"   ? "VERB"
           : pos == "prefix" ? "PREFIX"
           : pos == "root"   ? "ROOT"
           : pos == "suffix" ? "SUFFIX"
                             : "NOUN";
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
#ifndef CADET_NO_WARRIOR  // the website is AFOQT Prep only
        {"warrior", "Warrior Knowledge", Icon::Shield, "WARRIOR KNOWLEDGE",
         "Flashcards for Blocks 1\xE2\x80\x93" "11 of the Det 105 handbook."},
#endif
    };
    return list;
}
const TrackInfo& trackInfo(const std::string& id) {
    for (const auto& t : tracks())
        if (id == t.id) return t;
    return tracks().front();
}


// A page title with a muted subtitle underneath.
void pageTitle(const char* title, const char* subtitle = nullptr) {
    if (compact()) wrappedIn(fonts.heading, col::Ink, title);
    else textIn(fonts.title, col::Ink, title);
    if (subtitle) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(760.0f, ImGui::GetContentRegionAvail().x));
        textIn(fonts.body, col::Muted, subtitle);
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0, 6));
}

float buttonWidth(const char* label) { return textSize(fonts.strong, label).x + 40.0f; }

// Puts the next item (`width` wide) on the same line if it fits, else on a new line.
void sameLineIfFits(float width, float gap = 12.0f) {
    const float right = ImGui::GetItemRectMax().x + gap + width;
    if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine(0, gap);
}

// "Today", "Yesterday", "3 days ago", or "Nov 2", from a session date ("YYYY-MM-DD HH:MM").
std::string whenText(const std::string& date) {
    if (date.size() < 10) return "\xE2\x80\x94";
    std::tm d{};
    d.tm_year = std::atoi(date.c_str()) - 1900;
    d.tm_mon = std::atoi(date.c_str() + 5) - 1;
    d.tm_mday = std::atoi(date.c_str() + 8);
    d.tm_hour = 12;
    d.tm_isdst = -1;
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_r(&now, &t);
    t.tm_hour = 12;
    t.tm_min = t.tm_sec = 0;
    t.tm_isdst = -1;
    const long days = std::lround(std::difftime(std::mktime(&t), std::mktime(&d)) / 86400.0);
    if (days <= 0) return "Today";
    if (days == 1) return "Yesterday";
    if (days < 7) return fmt("%ld days ago", days);
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return fmt("%s %d", months[std::clamp(d.tm_mon, 0, 11)], d.tm_mday);
}

// A line of figures: each value in mono with a muted label after it, wrapping on narrow screens.
// One figure: a mono value and a muted label after it. Amber marks one that needs attention.
struct Fig {
    std::string value, label;
    ImVec4 color;
    Fig(std::string v, std::string l, ImVec4 c = col::Ink) : value(std::move(v)), label(std::move(l)), color(c) {}
};

void figuresLine(const std::vector<Fig>& items) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, gap = compact() ? 22.0f : 34.0f;
    const float vh = fontPx(fonts.monoStrong), lh = fontPx(fonts.small);
    float x = o.x, y = o.y;
    for (const Fig& f : items) {
        const std::string& value = f.value;
        const std::string& label = f.label;
        const float vw = textSize(fonts.monoStrong, value.c_str()).x;
        const float iw = vw + 7 + textSize(fonts.small, label.c_str()).x;
        if (x > o.x && x + iw > o.x + w) {
            x = o.x;
            y += vh + 10;
        }
        dl->AddText(fonts.monoStrong, vh, ImVec2(x, y), u32(f.color), value.c_str());
        dl->AddText(fonts.small, lh, ImVec2(x + vw + 7, y + vh - lh - 1), u32(col::Muted), label.c_str());
        x += iw + gap;
    }
    ImGui::Dummy(ImVec2(w, y - o.y + vh));
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
        rootDeck_.load(dataDir + "/roots.json");
        rootDeck_.loadProgress(apppaths::userFile("afoqt_roots.json"));
        rootRep_.load(apppaths::userFile("afoqt_roots_repetition.json"), rootDeck_);
        vaWords_.load(dataDir + "/va_definitions.json");
#ifndef CADET_NO_WARRIOR  // the website has no Warrior Knowledge files, and never writes its progress
        memDeck_.load(dataDir + "/warrior_cards.json");
        ui::setImageFolder(dataDir + "/insignia");
        memDeck_.loadProgress(apppaths::userFile("warrior_cards_progress.json"));
        addWarriorCards();
        wkRep_.load(apppaths::userFile("warrior_repetition.json"), static_cast<int>(registry_.inTrack("warrior").size()));
#endif
        for (const auto& q : saved_.all()) bank_.add(q);  // generated questions saved for review
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    loadSettings();  // first: it has this device's id for the miss counts
    misses_.load(apppaths::userFile("afoqt_misses.json"), deviceId_);
    weekly_.load(apppaths::userFile("afoqt_weekly.json"));
    cloud_.load();
    return true;
}

void GuiApp::reloadProgress() {
    progress_.reload();
    saved_.reload();  // another device may have saved questions
    for (const auto& q : saved_.all())
        if (bank_.byIds({q.id}).empty()) bank_.add(q);
    deck_.reloadProgress();
    repDeck_.reload(deck_);
    rootDeck_.reloadProgress();
    rootRep_.reload(rootDeck_);
    misses_.reload();
    weekly_.reload();
#ifndef CADET_NO_WARRIOR
    memDeck_.reloadProgress();
    wkRep_.reload();
#endif
}

namespace {

// "Key: Value" lines of a handbook card (key empty when a line has no colon).
std::vector<std::pair<std::string, std::string>> handbookLines(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find('\n', start), text.size());
        const std::string line = text.substr(start, end - start);
        if (!line.empty()) {
            const size_t colon = line.find(": ");
            if (colon == std::string::npos) out.emplace_back("", line);
            else out.emplace_back(line.substr(0, colon), line.substr(colon + 2));
        }
        start = end + 1;
    }
    return out;
}

std::string idPart(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (std::isalnum(static_cast<unsigned char>(ch))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}

Visual insignia(int scheme, int index, bool onFront) { return Visual{VisualKind::Insignia, {scheme, index, onFront ? 1 : 0}}; }

}  // namespace

// Warrior Knowledge blocks hold exactly the handbook items in data/warrior_cards.json:
// the title on the front, the handbook text on the back. The rank lists (Blocks 3-4)
// and the chain of command (Block 5) are split into one card per line instead, so you
// learn them one at a time: rank from its insignia, rank from its pay grade, and the
// person in each position. Nothing is added beyond what the handbook lists.
void GuiApp::addWarriorCards() {
    for (const Flashcard& c : memDeck_.cards()) {
        int block = 0;
        if (std::sscanf(c.partOfSpeech.c_str(), "BLOCK %d", &block) != 1) continue;
        const std::string subtest = fmt("wk_block%02d", block);
        if (!registry_.find(subtest)) continue;
        auto add = [&](const std::string& id, const std::string& prompt, const std::string& answer, Visual v = {}) {
            Question q;
            q.subtest = subtest;
            q.id = "WK-" + id;  // stable, so missed cards stay on the review list
            q.prompt = prompt;
            q.choices = {answer, "(not known)"};  // the second entry is only used to record "didn't know"
            q.correctIndex = 0;
            q.visual = std::move(v);
            bank_.add(std::move(q));
        };
        const std::string& title = c.word;
        const auto lines = handbookLines(c.definition);
        if (title.rfind("AFROTC Cadet Ranks", 0) == 0) {
            for (size_t i = 0; i < lines.size(); ++i)
                add(fmt("CDT-%d-PIC", static_cast<int>(i) + 1), "Name this AFROTC cadet rank.", lines[i].second,
                    insignia(0, static_cast<int>(i), true));
        } else if (title == "USAF & USSF Officer Ranks" || title == "USAF Enlisted Ranks") {
            const bool officer = title == "USAF & USSF Officer Ranks";
            const std::string kind = officer ? "officer" : "Air Force enlisted";
            for (const auto& [key, rank] : lines) {
                const int n = std::atoi(key.c_str() + 1);
                const std::string grade = key.substr(0, 1) + "-" + std::to_string(n);
                const std::string idBase = (officer ? "OFF-" : "AFE-") + idPart(key);
                add(idBase + "-PIC",
                    !officer && n == 1 ? "Which Air Force enlisted rank wears no insignia?" : "Name this " + kind + " rank.",
                    rank, insignia(officer ? 1 : 2, n, true));
                add(idBase + "-GRADE", "Which " + kind + " rank is " + grade + "?", rank, insignia(officer ? 1 : 2, n, false));
            }
        } else if (title == "USSF Enlisted Ranks") {
            std::vector<std::pair<std::string, std::string>> byGrade;  // E9 is listed twice (CMSgt and CMSSF)
            for (const auto& [key, rank] : lines) {
                const bool top = !byGrade.empty() && byGrade.back().first == key;
                const int n = top ? 10 : std::atoi(key.c_str() + 1);
                add("SFE-" + (top ? std::string("CMSSF") : idPart(key)) + "-PIC", "Name this Space Force enlisted rank.", rank,
                    insignia(3, n, true));
                if (top) byGrade.back().second += "\nor " + rank;
                else byGrade.emplace_back(key, rank);
            }
            for (const auto& [key, rank] : byGrade) {
                const int n = std::atoi(key.c_str() + 1);
                add("SFE-" + idPart(key) + "-GRADE", fmt("Which Space Force enlisted rank is E-%d?", n), rank, insignia(3, n, false));
            }
        } else if (title == "Chain of Command" || title == "Additional Personnel") {
            for (const auto& [position, person] : lines) add("COC-" + idPart(position), "Who is the " + position + "?", person);
        } else {
            add(idPart(title), title, c.definition);
        }
    }
}

void GuiApp::draw() {
    if (!themeApplied_) {
        setDarkMode(darkMode_);
        themeApplied_ = true;
    }
    if (cloud_.update()) reloadProgress();
    for (auto& n : atomicfile::takeNotices())  // damaged files set aside, saves that failed
        if (std::find(notices_.begin(), notices_.end(), n) == notices_.end()) notices_.push_back(std::move(n));
    touchScroll();
    passageLive_ = false;  // drawPassage sets it again this frame if the passage is showing in highlight mode
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
    const float innerW = sz.x - safe_.left - safe_.right;
    compact_ = innerW < kCompactBelow;
    ui::setDevice(compact_, touch_);
    if (!compact_) menuOpen_ = false;

    ImVec2 mainPos, mainSize, mainPad;
    if (!compact_) {
        // Sidebar (its navy also fills the space beside a notch)
        const float sideW = safe_.left + kSidebar;
        ImGui::GetWindowDrawList()->AddRectFilled(o, ImVec2(o.x + sideW, o.y + sz.y), u32(col::Sidebar));
        const float sideH = sz.y - safe_.top - safe_.bottom;
        ImGui::SetCursorScreenPos(ImVec2(o.x + safe_.left, o.y + safe_.top));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 30));
        ImGui::BeginChild("sidebar", ImVec2(kSidebar, sideH), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        drawSidebar(sideH);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        mainPos = ImVec2(o.x + sideW, o.y + safe_.top);
        mainSize = ImVec2(sz.x - sideW - safe_.right, sz.y - safe_.top);
        mainPad = ImVec2(46, 34);
    } else {
        drawTopBar(o, sz.x, kTopBar);
        mainPos = ImVec2(o.x + safe_.left, o.y + safe_.top + kTopBar);
        mainSize = ImVec2(innerW, sz.y - safe_.top - kTopBar);
        mainPad = innerW < 600 ? ImVec2(16, 18) : ImVec2(28, 26);
    }

    // Main area
    ImGui::SetCursorScreenPos(mainPos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, mainPad);
    ImGui::BeginChild("main", mainSize, ImGuiChildFlags_AlwaysUseWindowPadding,
                      touch_ ? ImGuiWindowFlags_NoScrollbar : 0);
    if (screen_ != lastScreen_) {  // new page: start at the top
        ImGui::SetScrollY(0.0f);
        lastScreen_ = screen_;
    }
    if (screen_ != Screen::Quiz) {  // not mid-question: the notice waits until the quiz is over
        int dismissed = -1;
        for (size_t i = 0; i < notices_.size(); ++i) {
            beginCallout();
            wrappedIn(fonts.strong, col::Ink, notices_[i].c_str());
            ImGui::Dummy(ImVec2(0, 2));
            if (button(fmt("Dismiss##notice%d", static_cast<int>(i)).c_str(), ButtonStyle::Outline)) dismissed = static_cast<int>(i);
            endCallout(col::TagSoft, col::TagStrong);
        }
        if (dismissed >= 0) notices_.erase(notices_.begin() + dismissed);
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
        case Screen::WeakSpots: drawWeakSpots(); break;
    }
    ImGui::Dummy(ImVec2(0, 12 + safe_.bottom));  // breathing room at the bottom (and above a home bar)
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    if (compact_ && menuOpen_) drawDrawer();
}

// Phone layout: navy bar with the menu button, logo, and the page name.
void GuiApp::drawTopBar(ImVec2 o, float width, float barH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = o.y + safe_.top;
    dl->AddRectFilled(o, ImVec2(o.x + width, top + barH), u32(col::Sidebar));
    float x = o.x + safe_.left + 8;
    if (screen_ != Screen::Quiz) {  // during a quiz, Quit is the way out
        ImGui::SetCursorScreenPos(ImVec2(x, top + (barH - 44) * 0.5f));
        if (ImGui::InvisibleButton("menu", ImVec2(44, 44))) {
            menuOpen_ = true;
            menuTime_ = ImGui::GetTime();
        }
        const ImVec2 c(x + 22, top + barH * 0.5f);
        for (int i = -1; i <= 1; ++i)
            dl->AddLine(c + ImVec2(-10, i * 6.5f), c + ImVec2(10, i * 6.5f), u32(col::White), 2.2f);
        x += 50;
    } else {
        x += 8;
    }
    x += 4;
    const float right = o.x + width - safe_.right - 12;
    dl->PushClipRect(ImVec2(x, top), ImVec2(right, top + barH), true);
    dl->AddText(fonts.heading, fontPx(fonts.heading), ImVec2(x, top + (barH - fontPx(fonts.heading)) * 0.5f),
                u32(col::White), pageName());
    dl->PopClipRect();
}

const char* GuiApp::pageName() const {
    switch (screen_) {
        case Screen::Home: return "Cadet Prep";
        case Screen::Settings: return "Settings";
        case Screen::Dashboard: return trackInfo(track_).name;
        case Screen::Practice: return "Practice tests";
        case Screen::Review: return "Review missed";
        case Screen::Flashcards: return track_ == "afoqt" ? "Vocabulary cards" : "Spaced repetition";
        case Screen::Progress: return "My progress";
        case Screen::Quiz: return sessionName_.c_str();
        case Screen::Results: return "Results";
        case Screen::WeakSpots: return "Weak spots";
    }
    return "Cadet Prep";
}

// Phone layout: the sidebar slides in from the left over a dimmed page. Tapping
// the page (or picking a page from the menu) closes it.
void GuiApp::drawDrawer() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float k = std::clamp(static_cast<float>(ImGui::GetTime() - menuTime_) / 0.2f, 0.0f, 1.0f);
    k = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
    const float dw = safe_.left + std::min(320.0f, vp->WorkSize.x * 0.86f);

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("drawer", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    const ImVec2 o = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, o + sz, IM_COL32(0, 0, 0, static_cast<int>(120 * k)));
    ImGui::SetCursorScreenPos(o);
    if (ImGui::InvisibleButton("scrim", sz)) menuOpen_ = false;

    const float x0 = o.x - dw * (1.0f - k);
    dl->AddRectFilled(ImVec2(x0, o.y), ImVec2(x0 + dw, o.y + sz.y), u32(col::Sidebar));
    const float sideH = sz.y - safe_.top - safe_.bottom;
    ImGui::SetCursorScreenPos(ImVec2(x0 + safe_.left, o.y + safe_.top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 24));
    ImGui::BeginChild("drawer_side", ImVec2(dw - safe_.left, sideH), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    drawSidebar(sideH);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

// Touch screens have no scroll wheel: dragging a finger scrolls whatever is under
// it (the innermost window that can scroll that way), and a quick flick keeps it
// gliding. Once a drag turns into a scroll, the button it started on won't click.
// Sideways drags that nothing can scroll are left alone (swipe cards use them).
void GuiApp::touchScroll() {
    if (!touch_) return;
    ImGuiContext& g = *GImGui;
    const ImGuiIO& io = g.IO;
    auto scrollable = [](ImGuiWindow* w, bool vertical) -> ImGuiWindow* {
        for (; w; w = w->ParentWindow) {
            if ((vertical ? w->ScrollMax.y : w->ScrollMax.x) > 0.5f) return w;
            if (!(w->Flags & ImGuiWindowFlags_ChildWindow)) break;
        }
        return nullptr;
    };
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {  // finger down: stops a fling, finds what can scroll
        scrollWinY_ = scrollable(g.HoveredWindow, true);
        scrollWinX_ = scrollable(g.HoveredWindow, false);
        if (passageLive_ && ImGui::IsMouseHoveringRect(passageMin_, passageMax_, false))
            scrollWinY_ = scrollWinX_ = nullptr;  // highlighting: a drag over the passage marks words, it doesn't scroll
        dragAxis_ = 0;
        flingWin_ = nullptr;
        flingVel_ = ImVec2(0, 0);
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
        if (dragAxis_ == 0 && d.x * d.x + d.y * d.y > 10.0f * 10.0f) {
            const bool vertical = std::fabs(d.y) >= std::fabs(d.x);
            dragAxis_ = (vertical ? scrollWinY_ : scrollWinX_) ? (vertical ? 2 : 1) : -1;
        }
        if (dragAxis_ > 0) {
            auto* w = static_cast<ImGuiWindow*>(dragAxis_ == 2 ? scrollWinY_ : scrollWinX_);
            const float delta = dragAxis_ == 2 ? io.MouseDelta.y : io.MouseDelta.x;
            if (dragAxis_ == 2) ImGui::SetScrollY(w, w->Scroll.y - delta);
            else ImGui::SetScrollX(w, w->Scroll.x - delta);
            const float v = io.DeltaTime > 0 ? -delta / io.DeltaTime : 0.0f;
            float& fv = dragAxis_ == 2 ? flingVel_.y : flingVel_.x;
            fv = fv * 0.6f + v * 0.4f;
            flingWin_ = w;
            if (g.ActiveId) ImGui::ClearActiveID();  // a scroll, not a tap
        }
        return;
    }
    dragAxis_ = 0;
    if (!flingWin_) return;
    auto* w = static_cast<ImGuiWindow*>(flingWin_);
    const float dt = io.DeltaTime;
    if (flingVel_.y != 0) ImGui::SetScrollY(w, w->Scroll.y + flingVel_.y * dt);
    if (flingVel_.x != 0) ImGui::SetScrollX(w, w->Scroll.x + flingVel_.x * dt);
    flingVel_ = flingVel_ * std::exp(-dt * 3.5f);
    const bool atEdgeY = (flingVel_.y < 0 && w->Scroll.y <= 0) || (flingVel_.y > 0 && w->Scroll.y >= w->ScrollMax.y);
    const bool atEdgeX = (flingVel_.x < 0 && w->Scroll.x <= 0) || (flingVel_.x > 0 && w->Scroll.x >= w->ScrollMax.x);
    if ((std::fabs(flingVel_.x) < 12 && std::fabs(flingVel_.y) < 12) || (flingVel_.y != 0 && atEdgeY) ||
        (flingVel_.x != 0 && atEdgeX)) {
        flingWin_ = nullptr;
        flingVel_ = ImVec2(0, 0);
    }
}

bool GuiApp::animating() const {
    const double now = ImGui::GetTime();
    return flingWin_ || dragAxis_ > 0 || (menuOpen_ && now - menuTime_ < 0.4) || swipe_.fly || swipe_.dragging ||
           swipe_.flipStart >= 0 || std::fabs(swipe_.x) > 0.5f || (swipe_.enterStart >= 0 && now - swipe_.enterStart < 0.4) ||
           (feedbackTime_ >= 0 && now - feedbackTime_ < 1.6);
}

// ================================================================ frame

void GuiApp::drawSidebar(float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;

    // Wordmark: the name, and the detachment underneath in mono caps
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(fonts.title, fontPx(fonts.title), p + ImVec2(2, 0), u32(col::White), "Cadet Prep");
    ImGui::SetCursorScreenPos(p + ImVec2(3, fontPx(fonts.title) + 6));
#ifdef CADET_NO_WARRIOR  // the website is for anyone taking the AFOQT
    spacedLabel("AFOQT PRACTICE", col::SidebarMuted, 1.4f);
#else
    spacedLabel("AFROTC \xC2\xB7 DET 105", col::SidebarMuted, 1.4f);
#endif
    ImGui::SetCursorScreenPos(p + ImVec2(0, fontPx(fonts.title) + fontPx(fonts.label) + 22));
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
        section = sessionTag_ == "REVIEW"    ? Screen::Review
                  : sessionTag_ == "WARM-UP" ? Screen::Dashboard
                  : warriorRepSession()      ? Screen::Flashcards
                                             : Screen::Practice;
    std::string badge = missedTotal ? std::to_string(missedTotal) : "";

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));
    if (navItem("n_dash", "Dashboard", Icon::Home, section == Screen::Dashboard, w)) goTo(Screen::Dashboard);
    if (navItem("n_prac", "Practice tests", Icon::Target, section == Screen::Practice, w)) goTo(Screen::Practice);
    if (navItem("n_rev", "Review missed", Icon::Review, section == Screen::Review, w, badge.c_str())) goTo(Screen::Review);
    if (track_ == "afoqt" && navItem("n_vocab", "Vocabulary cards", Icon::Cards, section == Screen::Flashcards, w))
        goTo(Screen::Flashcards);
    if (track_ == "afoqt" && navItem("n_weak", "Weak spots", Icon::Bolt, section == Screen::WeakSpots, w))
        goTo(Screen::WeakSpots);
    if (track_ == "warrior" && navItem("n_wkrep", "Spaced repetition", Icon::Cards, section == Screen::Flashcards, w))
        goTo(Screen::Flashcards);
    if (navItem("n_prog", "My progress", Icon::Chart, section == Screen::Progress, w)) goTo(Screen::Progress);
    ImGui::PopStyleVar();

    // Focus-area card pinned to the bottom
    const float cardH = 132.0f;
    const float top = std::max(ImGui::GetCursorPosY() + 20, height - cardH - 30);
    ImGui::SetCursorPosY(top);
    const ImVec2 c = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(c, c + ImVec2(w, cardH), u32(col::SidebarCard), kRadius);
    dl->AddRect(c, c + ImVec2(w, cardH), u32(col::SidebarLine), kRadius, 0, 1.0f);

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
    const ImU32 lc = u32(lh ? col::White : col::SidebarMark);
    dl->AddText(fonts.small, fontPx(fonts.small), lp, lc, link);
    dl->AddLine(lp + ImVec2(0, ls.y + 1), lp + ImVec2(ls.x, ls.y + 1), lc, 1.0f);
    if (beginPopup("focuspicker")) {
        focusMenuItems();
        ImGui::EndPopup();
    }
}


// ================================================================ dashboard

// ================================================================ tables, next up, figures

std::pair<int, int> GuiApp::drawTable(const char* id, const char* first, const std::vector<const char*>& headers,
                                      const std::vector<TableRow>& rows, float minActionsW) {
    std::pair<int, int> hit{-1, -1};
    beginAutoCard(id, ImVec2(20, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float btnH = touch_ ? 38.0f : 32.0f, gap = 8.0f;

    // Each button position is as wide as its widest label, so the buttons line up
    std::vector<float> bws;
    for (const auto& r : rows)
        for (size_t i = 0; i < r.buttons.size(); ++i) {
            if (bws.size() <= i) bws.push_back(0.0f);
            bws[i] = std::max(bws[i], textSize(fonts.strong, r.buttons[i].c_str()).x + 26.0f);
        }
    float actionsW = 0.0f;
    for (float b : bws) actionsW += b + gap;
    actionsW = std::max(actionsW, minActionsW);  // tables stacked on one page keep their columns lined up
    const float ncols = static_cast<float>(headers.size());
    const float colW = ncols > 0 ? std::clamp((w - actionsW - 220.0f) / ncols, 74.0f, 150.0f) : 0.0f;
    const float nameW = w - actionsW - colW * ncols - 8.0f;
    const bool stacked = compact_ || nameW < 160.0f;  // phones (and a narrow window): one row under another
    auto colX = [&](size_t i) { return o.x + nameW + 8.0f + colW * static_cast<float>(i); };

    float y = o.y + 12;
    auto label = [&](float x, const char* t) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        spacedLabel(t, col::Muted, 1.2f);
    };
    label(o.x, first);
    if (!stacked)
        for (size_t i = 0; i < headers.size(); ++i) label(colX(i), headers[i]);
    y += fontPx(fonts.label) + 12;

    for (size_t r = 0; r < rows.size(); ++r) {
        const TableRow& row = rows[r];
        dl->AddLine(ImVec2(o.x, y), ImVec2(o.x + w, y), u32(col::CardBorder), 1.0f);
        const float top = y;
        ImGui::PushID(static_cast<int>(r));
        auto nameAndTag = [&](float ny, float maxW) {
            const float nh = fontPx(fonts.strong);
            dl->PushClipRect(ImVec2(o.x, ny - 4), ImVec2(o.x + maxW, ny + nh + 4), true);
            dl->AddText(fonts.strong, nh, ImVec2(o.x, ny), u32(col::Ink), row.name.c_str());
            if (!row.tag.empty()) {
                const ImVec2 ts = textSize(fonts.label, row.tag.c_str());
                const ImVec2 a(o.x + textSize(fonts.strong, row.name.c_str()).x + 10, ny + (nh - ts.y) * 0.5f - 3);
                const ImVec2 b(a.x + ts.x + 12, a.y + ts.y + 6);
                dl->AddRectFilled(a, b, u32(col::TagSoft), 2.0f);
                dl->AddText(fonts.label, fontPx(fonts.label), a + ImVec2(6, 3), u32(col::TagStrong), row.tag.c_str());
            }
            dl->PopClipRect();
        };
        auto buttonsAt = [&](float x, float by, bool stretch) {
            const float n = static_cast<float>(row.buttons.size());
            const float sw = stretch && n > 0 ? (w - gap * (n - 1)) / n : 0.0f;
            for (size_t i = 0; i < row.buttons.size(); ++i) {
                ImGui::SetCursorScreenPos(ImVec2(x, by));
                const float bw = stretch ? sw : bws[i];
                if (button((row.buttons[i] + "##b" + std::to_string(i)).c_str(), ButtonStyle::Outline, ImVec2(bw, btnH)))
                    hit = {static_cast<int>(r), static_cast<int>(i)};
                x += bw + gap;
            }
        };
        // Last sessions as a small line: one point per session, 0-100 from bottom to top
        auto trendAt = [&](const std::vector<float>& v, ImVec2 a, ImVec2 b) {
            dl->AddLine(ImVec2(a.x, b.y), b, u32(col::CardBorder), 1.0f);
            if (v.empty()) return;
            const float step = v.size() > 1 ? (b.x - a.x) / static_cast<float>(v.size() - 1) : 0.0f;
            auto pt = [&](size_t i) { return ImVec2(a.x + step * static_cast<float>(i), b.y - (b.y - a.y) * std::clamp(v[i], 0.0f, 100.0f) / 100.0f); };
            for (size_t i = 1; i < v.size(); ++i) dl->AddLine(pt(i - 1), pt(i), u32(col::Accent), 1.5f);
            const ImVec2 last = pt(v.size() - 1);
            dl->AddRectFilled(last - ImVec2(2.5f, 2.5f), last + ImVec2(2.5f, 2.5f), u32(col::Accent));
        };
        const float noteW = stacked ? w : nameW - 16;
        const float noteH = row.note.empty() ? 0.0f : textSize(fonts.small, row.note.c_str(), noteW).y;
        if (!stacked) {
            const float nh = fontPx(fonts.strong);
            const float rowH = row.note.empty() ? 52.0f : std::max(52.0f, 14 + nh + 4 + noteH + 14);
            const float midY = row.note.empty() ? y + rowH * 0.5f : y + 14 + nh * 0.5f;  // with a note: line up with the name
            nameAndTag(midY - nh * 0.5f, nameW);
            if (!row.note.empty())
                dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(o.x, y + 14 + nh + 4), u32(col::Muted), row.note.c_str(),
                            nullptr, noteW);
            for (size_t i = 0; i < row.cells.size() && i < headers.size(); ++i) {
                const Cell& c = row.cells[i];
                if (!c.trend.empty()) trendAt(c.trend, ImVec2(colX(i), midY - 10), ImVec2(colX(i) + colW - 18, midY + 10));
                else
                    dl->AddText(fonts.mono, fontPx(fonts.mono), ImVec2(colX(i), midY - fontPx(fonts.mono) * 0.5f), u32(c.color),
                                c.text.c_str());
            }
            buttonsAt(o.x + w - actionsW + gap, midY - btnH * 0.5f, false);
            y += rowH;
        } else {
            y += 12;
            nameAndTag(y, w);
            y += fontPx(fonts.strong) + 4;
            std::string line;
            for (const auto& c : row.cells) {
                const std::string& t = c.phone.empty() ? c.text : c.phone;
                if (t.empty() || t == "\xE2\x80\x94") continue;  // nothing to say yet
                if (!line.empty()) line += "  \xC2\xB7  ";
                line += t;
            }
            dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(o.x, y), u32(col::Muted), line.c_str(), nullptr, w);
            y += textSize(fonts.small, line.c_str(), w).y + 6;
            if (!row.note.empty()) {
                dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(o.x, y), u32(col::InkSoft), row.note.c_str(), nullptr, noteW);
                y += noteH + 6;
            }
            for (const auto& c : row.cells)
                if (!c.trend.empty()) {
                    trendAt(c.trend, ImVec2(o.x, y + 2), ImVec2(o.x + std::min(w, 180.0f), y + 26));
                    y += 34;
                }
            y += 4;
            if (!row.buttons.empty()) {
                buttonsAt(o.x, y, true);
                y += btnH + 14;
            } else {
                y += 4;
            }
        }
        ImGui::PopID();
        if (row.focusMenu) cardFocusMenu(*row.focusMenu, ImVec2(o.x, top), ImVec2(o.x + w, y));
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    ImGui::Dummy(ImVec2(w, 2));
    ImGui::PopStyleVar();
    endAutoCard();
    return hit;
}

std::map<std::string, int> GuiApp::missedIn(const std::string& track) const {
    std::map<std::string, int> out;
    for (const auto& q : bank_.byIds(progress_.missedQuestionIds()))
        if (inTrackOf(q.subtest, track)) out[q.subtest]++;
    return out;
}

GuiApp::NextUp GuiApp::nextUpFor(const std::string& track) const {
    NextUp n;
    n.track = track;
    const bool afoqt = track == "afoqt";
    auto nameOf = [&](const std::string& id) {
        const SubtestInfo* s = registry_.find(id);
        return s ? s->name : id;
    };

    // 1. Missed questions waiting
    const auto missed = missedIn(track);
    int total = 0;
    for (const auto& [id, c] : missed) total += c;
    if (total > 0) {
        n.kind = NextUp::Review;
        n.urgent = true;
        n.title = "Review " + plural(total, afoqt ? "missed question" : "missed card");
        std::vector<std::pair<int, std::string>> bySize;
        for (const auto& [id, c] : missed) bySize.push_back({-c, nameOf(id)});
        std::sort(bySize.begin(), bySize.end());
        for (size_t i = 0; i < bySize.size() && i < 3; ++i)
            n.detail += (i ? "  \xC2\xB7  " : "") + bySize[i].second + fmt(" %d", -bySize[i].first);
        if (bySize.size() > 3) n.detail += fmt("  \xC2\xB7  %d more sections", static_cast<int>(bySize.size()) - 3);
        n.button = "Review";
        return n;
    }

    // 2. Today's spaced repetition deck
    const bool deckDone = afoqt ? repDeck_.doneToday() : wkRep_.doneToday();
    if (!deckDone) {
        const int streak = afoqt ? repDeck_.streak() : wkRep_.streak();
        n.kind = NextUp::Deck;
        n.urgent = streak > 0;  // a streak that ends tonight
        n.title = afoqt ? "Today's vocabulary deck" : "Today's spaced repetition deck";
        std::string size;
        if (afoqt) {
            size = plural(static_cast<int>(repDeck_.words().size()), "word");
        } else {
            size = wkRep_.blocks().size() == 1 ? "Block" : "Blocks";
            for (size_t i = 0; i < wkRep_.blocks().size(); ++i) size += (i ? ", " : " ") + std::to_string(wkRep_.blocks()[i]);
        }
        n.detail = size + "  \xC2\xB7  " +
                   (streak > 0 ? plural(streak, "day") + " streak, finish today to keep it" : std::string("Finish it to start a streak"));
        n.button = "Study deck";
        return n;
    }
    if (afoqt && !rootRep_.doneToday()) {
        const int streak = rootRep_.streak();
        n.kind = NextUp::Deck;
        n.subtest = "roots";
        n.urgent = streak > 0;
        n.title = "Today's word roots deck";
        n.detail = plural(static_cast<int>(rootRep_.words().size()), "root") + "  \xC2\xB7  " +
                   (streak > 0 ? plural(streak, "day") + " streak, finish today to keep it" : std::string("Finish it to start a streak"));
        n.button = "Study roots";
        return n;
    }

    // 3. Practice the focus area: pinned, else a section not tried yet, else the lowest accuracy
    const auto stats = progress_.stats();
    const auto secs = registry_.inTrack(track);
    std::string pick;
    auto pinned = pinnedByTrack_.find(track);
    if (pinned != pinnedByTrack_.end() && !pinned->second.empty() && registry_.find(pinned->second)) pick = pinned->second;
    if (pick.empty())
        for (const auto& s : secs) {
            auto it = stats.find(s.id);
            if (it == stats.end() || it->second.total == 0) {
                pick = s.id;
                break;
            }
        }
    if (pick.empty()) {
        double worst = 101.0;
        for (const auto& s : secs) {
            auto it = stats.find(s.id);
            if (it != stats.end() && it->second.total > 0 && it->second.accuracy() < worst) {
                worst = it->second.accuracy();
                pick = s.id;
            }
        }
    }
    if (pick.empty()) return n;
    auto it = stats.find(pick);
    const bool tried = it != stats.end() && it->second.total > 0;
    const bool isPinned = pinned != pinnedByTrack_.end() && pinned->second == pick;
    n.kind = NextUp::Practice;
    n.subtest = pick;
    n.title = (afoqt ? "Practice " : "Study ") + nameOf(pick);
    n.detail = isPinned ? (tried ? fmt("Your focus area  \xC2\xB7  %.0f%% %s", it->second.accuracy(), afoqt ? "accuracy" : "known")
                                 : std::string("Your focus area  \xC2\xB7  not started yet"))
               : tried ? fmt("Your lowest so far  \xC2\xB7  %.0f%% %s", it->second.accuracy(), afoqt ? "accuracy" : "known")
                       : std::string("Not started yet");
    n.button = afoqt ? "Practice" : "Study";
    return n;
}

void GuiApp::runNextUp(const NextUp& n) {
    if (track_ != n.track) switchTrack(n.track);
    switch (n.kind) {
        case NextUp::Review: startReview(""); break;
        case NextUp::Deck:
            if (n.track == "afoqt") {
                goTo(Screen::Flashcards);
                startRepetition(repMode_, n.subtest == "roots");
            } else {
                startWarriorRepetition();
            }
            break;
        case NextUp::Practice:
            if (const SubtestInfo* s = registry_.find(n.subtest)) startSubtest(*s, SessionMode::Practice);
            break;
        case NextUp::None: break;
    }
}

bool GuiApp::drawNextUp(const NextUp& n, bool showTrack, bool warmup) {
    if (n.kind == NextUp::None && !warmup) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, pad = compact_ ? 18.0f : 24.0f, btnH = 42.0f;
    const bool any = n.kind != NextUp::None;
    const std::string go = any ? n.button + "  \xE2\x86\x92" : "";
    const char* warmLabel = "Warm-up";
    const float goW = any ? textSize(fonts.strong, go.c_str()).x + 36 : 0.0f;
    const float warmW = warmup ? textSize(fonts.strong, warmLabel).x + 36 : 0.0f;
    const float buttonsW = goW + warmW + (any && warmup ? 10.0f : 0.0f);
    const float textW = compact_ ? w - pad * 2 : w - pad * 2 - buttonsW - 24;
    const std::string kicker = showTrack ? "Next up  \xC2\xB7  " + std::string(trackInfo(n.track).name) : "Next up";
    const std::string title = any ? n.title : "You're all caught up";
    const std::string detail = any ? n.detail : "Nothing to review and today's decks are done.";
    const float headH = textSize(fonts.heading, title.c_str(), textW).y;
    const float detH = textSize(fonts.body, detail.c_str(), textW).y;
    const float textH = fontPx(fonts.small) + 6 + headH + 4 + detH;
    const float h = compact_ ? pad + textH + 16 + btnH + pad : std::max(textH, btnH) + pad * 2;

    cardShape(dl, o, ImVec2(w, h));
    const ImVec4 mark = n.urgent ? col::TagStrong : col::Accent;
    dl->AddRectFilled(o, ImVec2(o.x + 3, o.y + h), u32(mark));
    float y = o.y + (compact_ ? pad : (h - textH) * 0.5f);
    dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(o.x + pad, y), u32(n.urgent ? col::TagStrong : col::Muted), kicker.c_str());
    y += fontPx(fonts.small) + 6;
    dl->AddText(fonts.heading, fontPx(fonts.heading), ImVec2(o.x + pad, y), u32(col::Ink), title.c_str(), nullptr, textW);
    y += headH + 4;
    dl->AddText(fonts.body, fontPx(fonts.body), ImVec2(o.x + pad, y), u32(col::Muted), detail.c_str(), nullptr, textW);

    bool started = false;
    float x, by, gw = goW, ww = warmW;
    if (compact_) {
        by = o.y + h - pad - btnH;
        x = o.x + pad;
        const float full = w - pad * 2;
        if (any && warmup) gw = ww = (full - 10) * 0.5f;
        else gw = ww = full;
    } else {
        by = o.y + (h - btnH) * 0.5f;
        x = o.x + w - pad - buttonsW;
    }
    if (warmup) {
        ImGui::SetCursorScreenPos(ImVec2(x, by));
        if (button("Warm-up##nextup", ButtonStyle::Outline, ImVec2(ww, btnH))) {
            startWarmup();
            started = true;
        }
        x += ww + 10;
    }
    if (any && !started) {
        ImGui::SetCursorScreenPos(ImVec2(x, by));
        if (button((go + "##nextup").c_str(), ButtonStyle::Primary, ImVec2(gw, btnH))) {
            runNextUp(n);
            started = true;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + h));
    ImGui::Dummy(ImVec2(w, 0));
    return started;
}

void GuiApp::drawFigures() {
    const Overall ov = overall();
    int missed = 0;
    for (const auto& [id, n] : missedIn(track_)) missed += n;
    const bool afoqt = track_ == "afoqt";
    std::vector<Fig> items = {
        {fmt("%d", ov.sessions), ov.sessions == 1 ? "session" : "sessions"},
        {fmt("%d", ov.total), afoqt ? "questions" : "cards"},
        {ov.total ? fmt("%d%%", ov.correct * 100 / ov.total) : "\xE2\x80\x94", afoqt ? "accuracy" : "known"},
        {fmt("%d", missed), "to review"},
    };
    if (afoqt) {
        items.push_back({fmt("%d/%d", deck_.masteredCount(), static_cast<int>(deck_.size())), "words mastered"});
        items.push_back({fmt("%d", repDeck_.streak()), repDeck_.streak() == 1 ? "day streak" : "day streak"});
    } else {
        items.push_back({fmt("%d/%d", static_cast<int>(wkRep_.blocks().size()), static_cast<int>(trackSections().size())),
                         "blocks in deck"});
        items.push_back({fmt("%d", wkRep_.streak()), "day streak"});
    }
    figuresLine(items);
}

// ================================================================ main menu and dashboards

void GuiApp::drawHome() {
    if (!compact_) pageTitle("Main menu");

    // The most urgent "next up" across the prep sections (the last one used wins a tie)
    NextUp best;
    for (const auto& t : tracks()) {
        NextUp n = nextUpFor(t.id);
        if (n.kind == NextUp::None) continue;
        if (best.kind == NextUp::None || n.kind < best.kind || (n.kind == best.kind && track_ == t.id && best.track != track_))
            best = n;
    }
    if (best.kind != NextUp::None && drawNextUp(best, true, false)) return;
    ImGui::Dummy(ImVec2(0, 8));

    std::vector<TableRow> rows;
    for (const auto& t : tracks()) {
        const auto secs = registry_.inTrack(t.id);
        const Overall ov = overallFor(t.id);
        const bool afoqt = std::string(t.id) == "afoqt";
        const std::string parts = plural(static_cast<int>(secs.size()), afoqt ? "section" : "block");
        const std::string acc = ov.total ? fmt("%d%%", ov.correct * 100 / ov.total) : "\xE2\x80\x94";
        TableRow r;
        r.name = t.name;
        if (track_ == t.id) r.tag = "LAST USED";
        r.cells = {{parts, parts},
                   {fmt("%d", ov.sessions), plural(ov.sessions, "session")},
                   {acc, ov.total ? acc + (afoqt ? " accuracy" : " known") : "Not started"}};
        r.buttons = {"Open  \xE2\x86\x92"};
        rows.push_back(r);
    }
    const auto hit = drawTable("home_tracks", "PREP SECTION", {"CONTENT", "SESSIONS", "ACCURACY"}, rows);
    if (hit.first >= 0) {
        const char* id = tracks()[static_cast<size_t>(hit.first)].id;
        if (track_ != id) switchTrack(id);
        else goTo(Screen::Dashboard);
    }
#if defined(__EMSCRIPTEN__)
    drawDownloads();
#endif
}

#if defined(__EMSCRIPTEN__)
// Starts a file download in the browser (the page stays where it is)
EM_JS(void, pageDownload, (const char* url), {
    var a = document.createElement('a');
    a.href = UTF8ToString(url);
    a.rel = 'noopener';
    document.body.appendChild(a);
    a.click();
    a.remove();
});

// The website's Download section, on the main menu: the Mac app, fetched through a
// short-lived link that only a signed-in account can get. Windows comes later.
void GuiApp::drawDownloads() {
    if (!cloud_.configured() || !cloud_.signedIn()) return;
    const std::string url = cloud_.takeDownloadUrl();
    if (!url.empty()) {
        pageDownload(url.c_str());
        downloadStarted_ = true;
    }
    ImGui::Dummy(ImVec2(0, 8));
    const bool busy = cloud_.downloadBusy();
    TableRow mac;
    mac.name = "Mac";
    mac.cells = {{"macOS 11 or later", "macOS 11 or later"}};
    mac.buttons = {busy ? "Getting it..." : "Download"};
    TableRow win;
    win.name = "Windows";
    win.cells = {{"Later", "Later", col::Muted}};
    const auto hit = drawTable("home_apps", "DOWNLOAD THE APP", {"NEEDS"}, {mac, win});
    if (hit.first == 0 && !busy) {
        downloadStarted_ = false;
        cloud_.requestDownload();
    }
    ImGui::Dummy(ImVec2(0, 6));
    if (!cloud_.downloadError().empty()) {
        wrappedIn(fonts.small, col::Bad, cloud_.downloadError().c_str());
    } else if (downloadStarted_) {
        beginCallout();
        textIn(fonts.strong, col::Ink, "CadetPrep.dmg is downloading");
        wrappedIn(fonts.small, col::InkSoft,
                  "Open it and drag Cadet Prep into Applications. The app isn't signed with Apple yet, so the "
                  "first time you open it macOS will say it can't be opened. Then go to System Settings > "
                  "Privacy & Security, scroll down, and choose Open Anyway. You only do this once.");
        endCallout(col::AccentSoft, col::Accent);
    } else {
        wrappedIn(fonts.small, col::Muted,
                  "The Mac app keeps the same progress as this site when you sign in to it.");
    }
}
#endif

void GuiApp::drawDashboard() {
    const TrackInfo& ti = trackInfo(track_);
    const bool afoqt = track_ == "afoqt";
    if (!compact_) pageTitle(ti.name);
    if (!message_.empty()) {
        beginCallout();
        textIn(fonts.strong, col::Ink, message_.c_str());
        endCallout(col::AccentSoft, col::Accent);
    }
    if (drawNextUp(nextUpFor(track_), false, true)) return;
    ImGui::Dummy(ImVec2(0, 4));
    drawFigures();
    ImGui::Dummy(ImVec2(0, 10));
    if (afoqt) {
        drawWeeklyStrip();
        if (screen_ != Screen::Dashboard) return;  // it started the set
        ImGui::Dummy(ImVec2(0, 8));
    }

    const auto stats = progress_.stats();
    const std::string focus = focusId();
    const char* dash = "\xE2\x80\x94";

    if (!afoqt) {
        // Warrior Knowledge: one row per block, with Study and the deck toggle
        const auto secs = trackSections();
        std::vector<TableRow> rows;
        for (const auto& s : secs) {
            const int block = std::atoi(s.id.c_str() + 8);
            auto it = stats.find(s.id);
            const bool tried = it != stats.end() && it->second.total > 0;
            const int n = static_cast<int>(bank_.size(s.id));
            const bool inDeck = wkRep_.has(block);
            TableRow r;
            r.name = s.name;
            if (s.id == focus) r.tag = "FOCUS";
            r.cells = {{fmt("%d", n), plural(n, "card")},
                       {tried ? fmt("%.0f%%", it->second.accuracy()) : dash,
                        tried ? fmt("%.0f%% known", it->second.accuracy()) : "Not started",
                        s.id == focus && tried ? col::TagStrong : col::Ink},
                       {tried ? whenText(it->second.last) : dash, tried ? whenText(it->second.last) : ""},
                       {inDeck ? "In deck" : dash, inDeck ? "In deck" : "", inDeck ? col::Accent : col::Muted}};
            r.buttons = {"Study"};
            if (!inDeck) r.buttons.push_back("Add to deck");
            else if (wkRep_.blocks().size() > 1) r.buttons.push_back("Remove");
            r.focusMenu = registry_.find(s.id);
            rows.push_back(r);
        }
        const auto hit = drawTable("wk_blocks", "BLOCK", {"CARDS", "KNOWN", "LAST", "DECK"}, rows);
        if (hit.first >= 0) {
            const SubtestInfo& s = secs[static_cast<size_t>(hit.first)];
            const int block = std::atoi(s.id.c_str() + 8);
            if (hit.second == 0) startSubtest(s, SessionMode::Practice);
            else if (wkRep_.has(block)) wkRep_.remove(block);
            else wkRep_.add(block);
        }
        return;
    }

    // AFOQT: knowledge and pilot sections, then the composite estimates. Both tables
    // leave room for three buttons so their columns line up.
    float actionsW = 0.0f;
    for (const char* l : {"Practice", "Timed", "Endless"}) actionsW += textSize(fonts.strong, l).x + 26.0f + 8.0f;
    struct Group { const char* id; const char* label; };
    for (const Group g : {Group{"knowledge", "KNOWLEDGE SECTIONS"}, Group{"pilot", "PILOT SECTIONS"}}) {
        const auto secs = registry_.inCategory(g.id);
        if (secs.empty()) continue;
        std::vector<TableRow> rows;
        for (const auto& s : secs) {
            auto it = stats.find(s.id);
            const bool tried = it != stats.end() && it->second.total > 0;
            const int n = static_cast<int>(bank_.size(s.id));
            TableRow r;
            r.name = s.name;
            if (s.id == focus) r.tag = "FOCUS";
            r.cells = {{n ? fmt("%d", n) : "Unlimited", n ? plural(n, "question") : "New questions each time"},
                       {tried ? fmt("%.0f%%", it->second.accuracy()) : dash,
                        tried ? fmt("%.0f%% accuracy", it->second.accuracy()) : "Not started",
                        s.id == focus && tried ? col::TagStrong : col::Ink},
                       {tried ? whenText(it->second.last) : dash, tried ? whenText(it->second.last) : ""}};
            r.buttons = {"Practice", "Timed"};
            if (s.id == "math_knowledge" || s.id == "arithmetic_reasoning") r.buttons.push_back("Endless");
            if (s.id == "verbal_analogies") r.buttons.push_back("Drill");
            r.focusMenu = registry_.find(s.id);
            rows.push_back(r);
        }
        const auto hit = drawTable((std::string("sec_") + g.id).c_str(), g.label, {"QUESTIONS", "ACCURACY", "LAST"}, rows,
                                   actionsW);
        if (hit.first >= 0) {
            const SubtestInfo& s = secs[static_cast<size_t>(hit.first)];
            if (hit.second == 0) startSubtest(s, SessionMode::Practice);
            else if (hit.second == 1) startSubtest(s, SessionMode::Timed);
            else if (s.id == "verbal_analogies") startRelationDrill("");
            else startEndless(s);
            return;
        }
        ImGui::Dummy(ImVec2(0, 8));
    }
    drawComposites();
}

// ================================================================ practice tests

void GuiApp::drawPractice() {
    const bool afoqt = track_ == "afoqt";
    const char* intro = "Practice is untimed and explains every answer. Timed tests use real pacing and show your score at the end.";
    if (!compact_) pageTitle("Practice tests", intro);
    else {
        wrappedIn(fonts.small, col::Muted, intro);
        ImGui::Dummy(ImVec2(0, 2));
    }
    if (!message_.empty()) {
        beginCallout();
        textIn(fonts.strong, col::Ink, message_.c_str());
        endCallout(col::AccentSoft, col::Accent);
    }

    // Every table on the page leaves room for three buttons, so the columns line up
    float actionsW = 0.0f;
    for (const char* l : {"Practice", "Timed", "Endless"}) actionsW += textSize(fonts.strong, l).x + 26.0f + 8.0f;

    // ---- Full batteries: every section in a group, timed and back to back
    struct Battery { std::string name; std::vector<SubtestInfo> secs; };
    std::vector<Battery> batteries;
    if (afoqt) {
        batteries.push_back({"Knowledge battery", registry_.inCategory("knowledge")});
        batteries.push_back({"Pilot battery", registry_.inCategory("pilot")});
    } else {
        batteries.push_back({"Full knowledge check", trackSections()});
    }
    std::vector<TableRow> brows;
    for (const auto& b : batteries) {
        int seconds = 0;
        std::string names;
        for (const auto& s : b.secs) {
            seconds += s.timeLimitSeconds;
            names += (names.empty() ? "" : ", ") + s.name;
        }
        TableRow r;
        r.name = b.name;
        r.note = afoqt ? names : "Every block, one after another.";
        const int n = static_cast<int>(b.secs.size());
        r.cells = {{fmt("%d", n), plural(n, afoqt ? "section" : "block")},
                   {fmt("%d min", (seconds + 30) / 60), fmt("About %d min", (seconds + 30) / 60)}};
        r.buttons = {"Start"};
        brows.push_back(r);
    }
    const auto bhit = drawTable("batteries", "FULL BATTERY", {afoqt ? "SECTIONS" : "BLOCKS", "TIME"}, brows, actionsW);
    if (bhit.first >= 0) {
        queue_ = batteries[static_cast<size_t>(bhit.first)].secs;
        startNextInQueue();
        return;
    }
    ImGui::Dummy(ImVec2(0, 8));

    // ---- One table per group, with each section's timed-test rules and tips
    const auto stats = progress_.stats();
    const std::string focus = focusId();
    const char* dash = "\xE2\x80\x94";
    struct Group { std::vector<SubtestInfo> secs; const char* label; };
    std::vector<Group> groups;
    if (afoqt) {
        groups.push_back({registry_.inCategory("knowledge"), "KNOWLEDGE SECTIONS"});
        groups.push_back({registry_.inCategory("pilot"), "PILOT SECTIONS"});
    } else {
        groups.push_back({trackSections(), "BLOCKS"});
    }
    for (const auto& g : groups) {
        if (g.secs.empty()) continue;
        std::vector<TableRow> rows;
        for (const auto& s : g.secs) {
            auto it = stats.find(s.id);
            const bool tried = it != stats.end() && it->second.total > 0;
            const int n = static_cast<int>(bank_.size(s.id));
            const int timedCount = s.questionCount ? s.questionCount : n;
            TableRow r;
            r.name = s.name;
            if (s.id == focus) r.tag = "FOCUS";
            r.note = s.tips;
            r.cells = {{n ? fmt("%d", n) : "Unlimited", n ? plural(n, afoqt ? "question" : "card") : "New questions each time"},
                       {fmt("%d in %s", timedCount, mmss(s.timeLimitSeconds).c_str()),
                        fmt("Timed: %d in %s", timedCount, mmss(s.timeLimitSeconds).c_str())},
                       {tried ? fmt("%.0f%%", it->second.accuracy()) : dash,
                        tried ? fmt("%.0f%% %s", it->second.accuracy(), afoqt ? "accuracy" : "known") : "Not started",
                        s.id == focus && tried ? col::TagStrong : col::Ink}};
            r.buttons = {afoqt ? "Practice" : "Study", "Timed"};
            if (s.id == "math_knowledge" || s.id == "arithmetic_reasoning") r.buttons.push_back("Endless");
            if (s.id == "verbal_analogies") r.buttons.push_back("Drill");
            r.focusMenu = registry_.find(s.id);
            rows.push_back(r);
        }
        const auto hit = drawTable((std::string("prac_") + g.label).c_str(), g.label,
                                   {afoqt ? "QUESTIONS" : "CARDS", "TIMED TEST", afoqt ? "ACCURACY" : "KNOWN"}, rows, actionsW);
        if (hit.first >= 0) {
            const SubtestInfo& s = g.secs[static_cast<size_t>(hit.first)];
            if (hit.second == 0) startSubtest(s, SessionMode::Practice);
            else if (hit.second == 1) startSubtest(s, SessionMode::Timed);
            else if (s.id == "verbal_analogies") startRelationDrill("");
            else startEndless(s);
            return;
        }
        ImGui::Dummy(ImVec2(0, 8));
    }
}

// ================================================================ review

void GuiApp::drawReview() {
    const auto missed = missedBySubtest();
    int total = 0;
    for (const auto& [id, n] : missed) total += n;
    const bool afoqt = track_ == "afoqt";
    const char* intro = afoqt ? "Every question you miss is saved here. Answer it correctly to clear it from the list."
                              : "Every card you swipe left is saved here. Swipe it right to clear it from the list.";

    const float ty = ImGui::GetCursorPosY();
    if (!compact_) pageTitle(afoqt ? "Review missed questions" : "Review missed cards", intro);
    else {
        wrappedIn(fonts.small, col::Muted, intro);
        ImGui::Dummy(ImVec2(0, 2));
    }
    if (total > 0) {
        const char* unit = afoqt ? "question" : "card";
        const std::string all = (total == 1 ? "Review " : "Review all ") + plural(total, unit);
        if (!compact_) {
            alignRight(textSize(fonts.strong, all.c_str()).x + 40);
            ImGui::SetCursorPosY(ty);
        }
        if (button(all.c_str(), ButtonStyle::Primary, ImVec2(compact_ ? ImGui::GetContentRegionAvail().x : 0, 40))) {
            startReview("");
            return;
        }
        if (compact_) ImGui::Dummy(ImVec2(0, 4));
    }

    std::vector<const SubtestInfo*> secs;
    std::vector<TableRow> rows;
    for (const auto& s : trackSections()) {
        if (bank_.size(s.id) == 0) continue;  // generated sections have nothing saved to review
        const int n = missed.count(s.id) ? missed.at(s.id) : 0;
        TableRow r;
        r.name = s.name;
        r.cells = {{n ? fmt("%d", n) : "\xE2\x80\x94", n ? fmt("%d to review", n) : "Nothing to review",
                    n ? col::TagStrong : col::Muted}};
        if (n) r.buttons = {"Review"};
        secs.push_back(registry_.find(s.id));
        rows.push_back(r);
    }
    const auto hit = drawTable("review", afoqt ? "SECTION" : "BLOCK", {"MISSED"}, rows,
                               textSize(fonts.strong, "Review").x + 34.0f);
    if (hit.first >= 0 && secs[static_cast<size_t>(hit.first)]) {
        startReview(secs[static_cast<size_t>(hit.first)]->id);
        return;
    }
    if (total == 0) {
        ImGui::Dummy(ImVec2(0, 6));
        wrappedIn(fonts.body, col::Muted, afoqt ? "Nothing to review. Questions you miss in practice show up here."
                                                : "Nothing to review. Cards you swipe left show up here.");
        ImGui::Dummy(ImVec2(0, 2));
        if (button("Go to practice tests", ButtonStyle::Outline)) goTo(Screen::Practice);
    }
}

// ================================================================ quiz

void GuiApp::drawQuiz() {
    topUpEndless();  // before taking a reference to the current question (adding may move them)
    engine_.update();
    if (engine_.finished()) {
        finishSession();
        return;
    }
    const Question& q = engine_.current();
    const bool feedback = engine_.showingFeedback();
    const SessionResult& r = engine_.result();
    const float anim = feedback && feedbackTime_ >= 0 ? static_cast<float>(ImGui::GetTime() - feedbackTime_) : -1.0f;

    // ---- Header: the session name, then the clock (mono figures) and Quit on the right.
    // Phone layout: the top bar has the name, so just the clock and Quit.
    const int left = engine_.secondsLeft();
    const std::string timer = left >= 0 ? mmss(left) : std::string("Untimed");
    const bool urgent = left >= 0 && left <= 30;
    bool quit = false;
    // Endless and AFOQT practice (which runs through the whole bank) end when you say so, and count.
    const bool openEnded = endless_ || relDrill_ || (sessionTag_ == "PRACTICE" && !cardMode_);
    const char* quitLabel = openEnded ? "Finish" : "Quit";
    const float quitW = textSize(fonts.strong, quitLabel).x + 40;
    {
        const float y0 = ImGui::GetCursorPosY();
        const float rowH = compact_ ? 40.0f : std::max(40.0f, fontPx(fonts.title));
        if (!compact_) {
            ImGui::SetCursorPosY(y0 + (rowH - fontPx(fonts.title)) * 0.5f);
            textIn(fonts.title, col::Ink, sessionName_.c_str());
        }
        ImFont* tf = left >= 0 ? fonts.big : fonts.strong;
        const float tw = textSize(tf, timer.c_str()).x;
        if (compact_) ImGui::SetCursorPosY(y0 + (rowH - fontPx(tf)) * 0.5f);
        else {
            alignRight(quitW + 18 + tw);
            ImGui::SetCursorPosY(y0 + (rowH - fontPx(tf)) * 0.5f);
        }
        textIn(tf, urgent ? col::Bad : left >= 0 ? col::Ink : col::Muted, timer.c_str());
        alignRight(quitW);
        ImGui::SetCursorPosY(y0);
        quit = button(quitLabel, openEnded ? ButtonStyle::Primary : ButtonStyle::Outline, ImVec2(quitW, 40));
        ImGui::SetCursorPosY(y0 + rowH + 6);
    }
    if (quit && openEnded && !r.answers.empty()) {
        engine_.finishNow();  // the question on screen, if unanswered, doesn't count
        finishSession();
        return;
    }
    if (quit) {
        queue_.clear();
        goTo(sessionTag_ == "REVIEW"    ? Screen::Review
             : sessionTag_ == "WARM-UP" ? Screen::Dashboard
             : warriorRepSession()      ? Screen::Flashcards
                                        : Screen::Practice);
        return;
    }

    // ---- Counts; timed tests also get a progress bar (practice, review and warm-up don't)
    const int answered = static_cast<int>(r.answers.size());
    if (sessionTag_ == "TIMED TEST")
        thinBar(static_cast<float>(answered) / engine_.total(), ImGui::GetContentRegionAvail().x, col::Accent, 8.0f);
    const char* mode = sessionTag_ == "TIMED TEST" ? "Timed test"
                       : sessionTag_ == "ENDLESS"    ? "Endless practice"
                       : sessionTag_ == "PRACTICE"   ? "Practice"
                       : warriorRepSession()         ? "Spaced repetition"
                                                     : nullptr;  // warm-up and review: the name says it
    const std::string count = openEnded && !relDrill_ ? fmt("Question %d", engine_.index() + 1)
                              : fmt(cardMode_ ? "Card %d of %d" : "Question %d of %d", engine_.index() + 1, engine_.total());
    textIn(fonts.strong, col::InkSoft, (mode ? std::string(mode) + "  \xC2\xB7  " + count : count).c_str());
    std::string score = sessionTag_ == "TIMED TEST" ? std::string("Score shown at the end")
                        : relDrill_ ? fmt("%d/%d named \xC2\xB7 %d/%d answered", relNamedRight_, relNamed_, r.correctCount(), answered)
                        : cardMode_ ? fmt("%d known \xC2\xB7 %d learning", r.correctCount(), answered - r.correctCount())
                                    : fmt("%d right \xC2\xB7 %d missed", r.correctCount(), answered - r.correctCount());
    ImFont* sf = sessionTag_ == "TIMED TEST" ? fonts.body : fonts.mono;
    alignRight(textSize(sf, score.c_str()).x);
    textIn(sf, col::Muted, score.c_str());
    ImGui::Dummy(ImVec2(0, 2));
    if (cardMode_) {
        drawCardQuestion(q);
        return;
    }
    if (relDrill_ && relStepFor_ != engine_.index()) {  // step 1: name the relationship first
        drawRelationStep(q);
        return;
    }

    // ---- The question card
    // Phone layout: the card is as tall as the question (the page scrolls instead)
    if (compact_) beginAutoCard("question", ImVec2(32, 30));
    else beginCard("question", ImVec2(0, std::max(420.0f, ImGui::GetContentRegionAvail().y - 26)), true, ImVec2(32, 30));
    const bool hasVisual = q.visual.kind != VisualKind::None;
    // Narrow card: the picture goes above the question, centered (a wide Table
    // Reading grid scrolls sideways), instead of beside it.
    const float cardW = ImGui::GetContentRegionAvail().x;
    const bool stackVisual = hasVisual && cardW < 760;
    const bool sideVisual = hasVisual && !stackVisual;
    if (stackVisual) {
        const float vw = visualWidth(q.visual, cardW, true);
        if (vw > cardW) {
            ImGui::BeginChild("visual", ImVec2(cardW, visualHeight(q.visual, vw) + 14), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar);
            drawVisual(q.visual, vw);
            ImGui::EndChild();
            textIn(fonts.small, col::Muted, touch_ ? "Drag the table sideways to see all of it." : "Scroll sideways to see all of the table.");
        } else {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cardW - vw) * 0.5f);
            drawVisual(q.visual, vw);
        }
        ImGui::Dummy(ImVec2(0, 4));
    } else if (sideVisual) {
        const float vw = visualWidth(q.visual, ImGui::GetContentRegionAvail().x);
        ImGui::BeginGroup();
        drawVisual(q.visual, vw);
        ImGui::EndGroup();
        ImGui::SameLine(0, 34);
        ImGui::BeginGroup();
    }
    if (!q.passage.empty()) drawPassage(q.passage);
    if (relDrill_) {  // step 2: the relationship stays in view
        const verbal::TypeInfo* t = verbal::findType(q.subtest, q.type);
        textIn(fonts.small, col::Muted, fmt("Step 2 of 2  \xC2\xB7  Relationship: %s", t ? t->name : "?").c_str());
        ImGui::Dummy(ImVec2(0, 2));
    }
    wrappedIn(fonts.prompt, col::Ink, q.prompt.c_str());
    ImGui::Dummy(ImVec2(0, 6));

    int clicked = -2;  // -2 = nothing, -1 = skip, 0+ = choice
    const float aw = ImGui::GetContentRegionAvail().x;
    const bool pictures = q.choiceVisuals.size() == q.choices.size();
    const int perRow = aw < 560 ? 2 : 3;
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
        if (button(touch_ ? "Skip" : "Skip   S", ButtonStyle::Outline) || ImGui::IsKeyPressed(ImGuiKey_S, false)) clicked = -1;
    } else {
        const int chosen = engine_.lastChoice();
        const bool right = chosen == q.correctIndex;
        beginCallout();
        textIn(fonts.heading, right ? col::Good : col::Bad, right ? "Correct" : chosen < 0 ? "Skipped" : "Not quite");
        wrappedIn(fonts.body, col::Ink, q.explanation.c_str());
        if (const verbal::TypeInfo* t = verbal::findType(q.subtest, q.type)) {  // name the type, to learn to spot it
            ImGui::Dummy(ImVec2(0, 2));
            wrappedIn(fonts.small, col::Muted,
                      fmt("%s: %s. %s", q.subtest == "verbal_analogies" ? "Relationship" : "Question type", t->name, t->hint).c_str());
        }
        if (!right) {
            const auto defs = definitionsFor(q, chosen, {&deck_, &vaWords_});
            if (!defs.empty()) {
                ImGui::Dummy(ImVec2(0, 4));
                textIn(fonts.small, col::Muted, defs.size() == 1 ? "Definition" : "Definitions");
                for (const Flashcard* c : defs) {
                    std::string word = c->word;
                    if (!word.empty()) word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
                    textIn(fonts.strong, col::Ink, (word + (c->partOfSpeech.empty() ? "" : "  (" + c->partOfSpeech + ")")).c_str());
                    wrappedIn(fonts.body, col::InkSoft, c->definition.c_str());
                }
            }
        }
        endCallout(right ? col::GoodSoft : col::BadSoft, right ? col::Good : col::Bad);
        const bool nextHit = button(touch_ ? "Next  \xE2\x86\x92" : "Next  \xE2\x86\x92   Enter",
                                    ButtonStyle::Primary, ImVec2(compact_ ? ImGui::GetContentRegionAvail().x : 0, compact_ ? 48 : 0)) ||
                             ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        saveButton(q, compact_, !compact_);  // generated math questions: keep this one for review
        if (nextHit) {
            engine_.next();
            feedbackTime_ = -1.0;
        }
    }
    if (sideVisual) ImGui::EndGroup();
    if (!sessionTips_.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        divider();
        const std::string keys = std::string("A\xE2\x80\x93") + static_cast<char>('A' + q.choices.size() - 1);
        wrappedIn(fonts.small, col::Muted, ("Tip: " + sessionTips_ +
                                            (touch_ ? "" : "   Keys: " + keys + " to answer, S to skip, Enter for next.")).c_str());
    }
    if (compact_) endAutoCard();
    else endCard();

    if (clicked != -2) {
        engine_.submit(clicked);
        if (engine_.showingFeedback()) {  // practice and review show the answer right away
            feedbackTime_ = ImGui::GetTime();
            if (clicked == q.correctIndex) sound_.correct();
            else sound_.wrong();
        }
    }
}

// Reading Comprehension: the passage, with a text size control and word highlighting.
// ImGui has no text selection, so the words are laid out and drawn here: wrapped by hand
// at the zoomed size, with a flag per word. Press on a word and drag to another to mark
// everything between them (or, if the first word was already marked, to erase it); the
// range follows the pointer, so dragging back shrinks it. A click on one word toggles it.
// Touch: a drag scrolls the page, so the Highlight button must be on first.
void GuiApp::drawPassage(const std::string& passage) {
    static const float kZoom[] = {0.85f, 1.0f, 1.3f, 1.6f};
    constexpr int kSteps = static_cast<int>(sizeof kZoom / sizeof kZoom[0]);
    passageZoom_ = std::clamp(passageZoom_, 0, kSteps - 1);

    struct Word { size_t begin, end; };
    std::vector<Word> words;
    for (size_t i = 0; i < passage.size();) {
        while (i < passage.size() && std::isspace(static_cast<unsigned char>(passage[i]))) ++i;
        const size_t b = i;
        while (i < passage.size() && !std::isspace(static_cast<unsigned char>(passage[i]))) ++i;
        if (i > b) words.push_back({b, i});
    }
    std::vector<char>& marks = highlights_[passage];
    marks.resize(words.size(), 0);
    const bool any = std::find(marks.begin(), marks.end(), 1) != marks.end();
    const bool active = !touch_ || highlightMode_;  // on a mouse, dragging always highlights

    // ---- Toolbar: text size, (touch) the highlight switch, clear
    textIn(fonts.small, col::Muted, "Passage");
    const float bh = touch_ ? 38.0f : 30.0f;
    if (button("A-##pz_out", passageZoom_ > 0 ? ButtonStyle::Outline : ButtonStyle::Disabled, ImVec2(48, bh)) && passageZoom_ > 0) {
        --passageZoom_;
        saveSettings();
    }
    ImGui::SameLine(0, 8);
    if (button("A+##pz_in", passageZoom_ < kSteps - 1 ? ButtonStyle::Outline : ButtonStyle::Disabled, ImVec2(48, bh)) &&
        passageZoom_ < kSteps - 1) {
        ++passageZoom_;
        saveSettings();
    }
    if (touch_) {
        ImGui::SameLine(0, 8);
        if (button("Highlight##pz_hl", highlightMode_ ? ButtonStyle::Primary : ButtonStyle::Outline, ImVec2(0, bh)))
            highlightMode_ = !highlightMode_;
    }
    if (any) {
        sameLineIfFits(buttonWidth("Clear"), 8);
        if (button("Clear##pz_clear", ButtonStyle::Outline, ImVec2(0, bh))) {
            std::fill(marks.begin(), marks.end(), 0);
            hlDrag_.active = false;
        }
    }
    if (!any || (touch_ && !highlightMode_))
        wrappedIn(fonts.small, col::Muted,
                  touch_ ? (highlightMode_ ? "Drag across words to highlight them. Drag over highlighted words to erase."
                                           : "Turn on Highlight, then drag across words to mark them.")
                         : "Drag across words to highlight them. Drag over highlighted words to erase.");

    // ---- Lay the words out at the zoomed size
    beginCallout();
    ImFont* font = fonts.body;
    const float width = std::max(80.0f, ImGui::GetContentRegionAvail().x - 24.0f);
    const float size = fontPx(font) * kZoom[passageZoom_], lineH = std::round(size * 1.5f);
    const float space = font->CalcTextSizeA(size, FLT_MAX, 0.0f, " ").x;
    struct Placed { float x, w; int line; };
    std::vector<Placed> at(words.size());
    std::vector<int> lineFirst;  // index of each line's first word
    float x = 0.0f;
    int line = 0;
    for (size_t i = 0; i < words.size(); ++i) {
        const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, passage.c_str() + words[i].begin, passage.c_str() + words[i].end).x;
        if (x > 0.0f && x + w > width) {
            x = 0.0f;
            ++line;
        }
        if (lineFirst.size() <= static_cast<size_t>(line)) lineFirst.push_back(static_cast<int>(i));
        at[i] = {x, w, line};
        x += w + space;
    }
    const int lines = static_cast<int>(lineFirst.size());
    const float height = std::max(1.0f, static_cast<float>(lines) * lineH);

    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##passage", ImVec2(width, height));
    passageMin_ = o;
    passageMax_ = o + ImVec2(width, height);
    passageLive_ = touch_ && highlightMode_;

    // The word under (or nearest to) a point: pick the line by height, then the word by position
    auto wordAt = [&](ImVec2 m) {
        const int ln = std::clamp(static_cast<int>(std::floor((m.y - o.y) / lineH)), 0, std::max(0, lines - 1));
        const int first = lineFirst[static_cast<size_t>(ln)];
        const int last = ln + 1 < lines ? lineFirst[static_cast<size_t>(ln) + 1] - 1 : static_cast<int>(words.size()) - 1;
        for (int i = first; i < last; ++i)
            if (m.x <= o.x + at[static_cast<size_t>(i)].x + at[static_cast<size_t>(i)].w + space * 0.5f) return i;
        return last;
    };
    if (active && !words.empty()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActivated()) {
            const int a = wordAt(mouse);
            hlDrag_ = {true, a, !marks[static_cast<size_t>(a)], marks};
        }
        if (hlDrag_.active && (!ImGui::IsItemActive() || hlDrag_.base.size() != marks.size())) hlDrag_.active = false;
        if (hlDrag_.active) {
            const int c = wordAt(mouse);
            marks = hlDrag_.base;
            for (int i = std::min(hlDrag_.anchor, c); i <= std::max(hlDrag_.anchor, c); ++i)
                marks[static_cast<size_t>(i)] = hlDrag_.paint ? 1 : 0;
        }
    }

    // ---- Draw: highlights under the words, one bar per run of marked words on a line
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < words.size(); ++i) {
        if (!marks[i]) continue;
        const bool joinNext = i + 1 < words.size() && marks[i + 1] && at[i + 1].line == at[i].line;
        const float top = o.y + static_cast<float>(at[i].line) * lineH + 1.0f;
        dl->AddRectFilled(ImVec2(o.x + at[i].x - 1.0f, top), ImVec2(o.x + at[i].x + at[i].w + (joinNext ? space : 1.0f), top + lineH - 2.0f),
                          u32(col::Highlight), 2.0f);
    }
    for (size_t i = 0; i < words.size(); ++i)
        dl->AddText(font, size, ImVec2(o.x + at[i].x, o.y + static_cast<float>(at[i].line) * lineH + (lineH - size) * 0.5f),
                    u32(marks[i] ? col::Ink : col::InkSoft), passage.c_str() + words[i].begin, passage.c_str() + words[i].end);
    endCallout(col::Bg, col::CalloutEdge);
    ImGui::Dummy(ImVec2(0, 4));
}

// Warrior Knowledge: the question on the front, the answer on the back. You grade
// yourself: swipe right if you knew it, left if you didn't. Right counts as correct;
// left counts as missed, so the card goes on the review list.
void GuiApp::drawCardQuestion(const Question& q) {
    ImGui::Dummy(ImVec2(0, 8));
    const float minH = std::clamp(ImGui::GetContentRegionAvail().y - 170, 320.0f, 520.0f);
    const std::string& answer = q.choices[static_cast<size_t>(q.correctIndex)];
    const std::string blockLabel = fmt("BLOCK %d", std::atoi(q.subtest.c_str() + std::strlen("wk_block")));
    const bool rank = q.visual.kind == VisualKind::Insignia && q.visual.data.size() >= 3;
    const int swiped = swipeCard(
        "wkcard", swipe_, minH,
        [&](float left, float width) {
            cardText(fonts.label, col::Muted, blockLabel.c_str(), left, width);
            ImGui::Dummy(ImVec2(0, 10));
            if (rank && q.visual.data[2]) {
                drawInsignia(q.visual, 150, left, width);
                ImGui::Dummy(ImVec2(0, 14));
            }
            cardText(fonts.title, col::Ink, q.prompt.c_str(), left, width);
        },
        [&](float left, float width) {
            cardText(fonts.label, col::Muted, upper(q.prompt).c_str(), left, width);
            ImGui::Dummy(ImVec2(0, 12));
            if (rank) {
                drawInsignia(q.visual, 110, left, width);
                ImGui::Dummy(ImVec2(0, 12));
            }
            cardText(answer.size() < 90 ? fonts.title : fonts.prompt, col::Accent, answer.c_str(), left, width);
        });

    // Buttons for people who'd rather click than drag
    ImGui::Dummy(ImVec2(0, 10));
    swipeButtons(swipe_.flipped ? "Show question" : "Flip card");
    ImGui::Dummy(ImVec2(0, 6));
    divider();
    wrappedIn(fonts.small, col::Muted,
              ((sessionTips_.empty() ? std::string() : "Tip: " + sessionTips_ + "   ") +
               (touch_ ? "Tap the card to flip it. Swipe it right if you knew it, left if you didn't."
                       : "Click the card or press Space to flip. Drag or swipe it right if you knew it, left if you didn't "
                         "(or use the arrow keys).")).c_str());

    if (swiped != 0) {
        const bool knew = swiped > 0;
        // "Didn't know" is recorded as a wrong choice so it counts as answered but missed.
        const int wrong = (q.correctIndex + 1) % static_cast<int>(q.choices.size());
        // Spaced repetition: a missed card comes back at the end until you know it
        if (!knew && warriorRepSession()) engine_.append({Question(q)});  // a copy: appending can move `q`
        const int correct = engine_.current().correctIndex;
        engine_.submit(knew ? correct : wrong);
        if (engine_.showingFeedback()) engine_.next();  // no feedback step: the back of the card was the feedback
        if (knew) sound_.correct();
    }
}

// "Still learning", flip, and "Know it" under a swipe card. Phones: the two answers
// side by side, flip underneath.
void GuiApp::swipeButtons(const char* flipLabel) {
    const float gap = 14.0f, w = ImGui::GetContentRegionAvail().x;
    if (!compact_ || w >= 3 * 150 + 2 * gap) {
        const float bw = std::min(210.0f, (w - gap * 2) / 3);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (w - bw * 3 - gap * 2) * 0.5f));
        if (button("\xE2\x86\x90  Still learning", ButtonStyle::Danger, ImVec2(bw, 50))) swipe_.send(-1);
        ImGui::SameLine(0, gap);
        if (button(flipLabel, ButtonStyle::Outline, ImVec2(bw, 50))) swipe_.flip();
        ImGui::SameLine(0, gap);
        if (button("Know it  \xE2\x86\x92", ButtonStyle::Primary, ImVec2(bw, 50))) swipe_.send(1);
        return;
    }
    const float bw = (w - gap) / 2;
    if (button("\xE2\x86\x90  Still learning", ButtonStyle::Danger, ImVec2(bw, 50))) swipe_.send(-1);
    ImGui::SameLine(0, gap);
    if (button("Know it  \xE2\x86\x92", ButtonStyle::Primary, ImVec2(bw, 50))) swipe_.send(1);
    if (button(flipLabel, ButtonStyle::Outline, ImVec2(w, 46))) swipe_.flip();
}

// ================================================================ results

void GuiApp::drawResults() {
    const SessionResult& r = engine_.result();
    const int total = static_cast<int>(r.answers.size());
    const int pct = total ? r.correctCount() * 100 / total : 0;

    pageTitle(("Results: " + sessionName_).c_str());
    std::vector<Fig> figs = {
        {fmt("%d%%", pct), "score"},
        {fmt("%d/%d", r.correctCount(), total), cardMode_ ? "known" : "right"},
        {mmss(static_cast<int>(r.totalSeconds)), "time"},
    };
    if (r.answeredCount())
        figs.push_back({fmt("%.0f s", r.totalSeconds / r.answeredCount()), cardMode_ ? "per card" : "per answer"});
    if (!cardMode_ && r.answeredCount() < total) figs.push_back({fmt("%d", total - r.answeredCount()), "unanswered"});
    if (relDrill_) figs.push_back({fmt("%d/%d", relNamedRight_, relNamed_), "relationships named"});
    figuresLine(figs);
    ImGui::Dummy(ImVec2(0, 10));

    if (r.timeExpired) {
        beginCallout();
        textIn(fonts.strong, col::Ink, cardMode_ ? "Time's up. Cards you didn't get to count as missed, so you'll see them again."
                                                 : "Time's up. Questions you didn't get to count as missed, so they're waiting in review.");
        endCallout(col::BadSoft, col::Bad);
    }

    if (weeklySession_) {
        const int streak = weekly_.streak();
        beginCallout();
        textIn(fonts.heading, col::Ink, "This week's reading is done");
        wrappedIn(fonts.body, col::Ink,
                  fmt("Your weekly streak is %s (best: %s). A new set of passages comes every Monday.",
                      plural(streak, "week").c_str(), plural(weekly_.bestStreak(), "week").c_str()).c_str());
        endCallout(col::GoodSoft, col::Good);
    }
    if (warriorRepSession()) {
        const int streak = wkRep_.streak();
        beginCallout();
        textIn(fonts.heading, col::Ink, "Deck complete");
        wrappedIn(fonts.body, col::Ink,
                  wkRepCounted_ ? fmt("You went through your whole deck today. Your streak is now %s (best: %s).",
                                      plural(streak, "day").c_str(), plural(wkRep_.bestStreak(), "day").c_str()).c_str()
                                : "You already finished your deck today, so the streak stays the same. Come back tomorrow "
                                  "to add another day.");
        endCallout(col::GoodSoft, col::Good);
        if (button("Back to spaced repetition")) {
            goTo(Screen::Flashcards);
            return;
        }
        sameLineIfFits(buttonWidth("Back to dashboard"));
    }
    if (!queue_.empty()) {
        if (button(fmt("Next section: %s  \xE2\x86\x92", queue_.front().name.c_str()).c_str())) {
            startNextInQueue();
            return;
        }
        sameLineIfFits(buttonWidth("Back to dashboard"));
    }
    if (button("Back to dashboard", queue_.empty() && !warriorRepSession() ? ButtonStyle::Primary : ButtonStyle::Outline)) {
        queue_.clear();
        goTo(Screen::Dashboard);
        return;
    }
    ImGui::Dummy(ImVec2(0, 8));

    beginAutoCard("missed");
    spacedLabel(cardMode_ ? "STILL LEARNING" : "MISSED QUESTIONS", col::Muted, 1.2f);
    ImGui::Dummy(ImVec2(0, 2));
    const auto& qs = engine_.questions();
    bool any = false;
    for (size_t i = 0; i < r.answers.size(); ++i) {
        if (r.answers[i].correct) continue;
        if (any) divider();
        any = true;
        const Question& q = qs[i];
        wrappedIn(fonts.strong, col::Ink, q.prompt.c_str());
        const int chosen = r.answers[i].chosenIndex;
        if (cardMode_) {
            if (q.visual.kind == VisualKind::Insignia) drawInsignia(q.visual, 64, ImGui::GetCursorScreenPos().x, 64 * 2.1f);
            wrappedIn(fonts.body, col::Good, fmt("Answer:  %s", q.choices[q.correctIndex].c_str()).c_str());
        } else {
            textIn(fonts.body, col::Bad, chosen >= 0 ? fmt("Your answer:  %s", q.choices[chosen].c_str()).c_str() : "Not answered");
            textIn(fonts.body, col::Good, fmt("Correct:  %s", q.choices[q.correctIndex].c_str()).c_str());
        }
        wrappedIn(fonts.small, col::Muted, q.explanation.c_str());
        saveButton(q, false, false);
    }
    if (!any) wrappedIn(fonts.body, col::Muted, cardMode_ ? "None. You knew every card." : "None. You got every one right.");
    endAutoCard();
}

// ================================================================ flashcards

void GuiApp::drawFlashcards() {
    if (track_ != "afoqt") {
        drawWarriorRepetition();
        return;
    }
    // AFOQT: vocabulary cards (and word roots). Warrior Knowledge: memorize cards (title
    // on the front, the full text on the back; recite it, then flip to check).
    if (!flashActive_) repRoots_ = false;  // the start page is about the vocabulary deck
    FlashcardDeck& deck = activeDeck();
    const bool vocab = track_ == "afoqt";
    const bool roots = vocab && repRoots_;
    const char* deckName = roots ? "Word roots" : vocab ? "Vocabulary cards" : "Warrior Knowledge cards";
    const int mastered = deck.masteredCount();
    const int total = static_cast<int>(deck.size());

    // ---- Start screen
    if (!flashActive_) {
        const char* intro = vocab ? "Word Knowledge flashcards with spaced repetition: words you miss come back "
                                    "sooner, and a word is mastered after 3 correct answers in a row."
                                  : "Everything in the handbook you need word for word. Recite it from memory, then flip to "
                                    "check. Cards you miss come back sooner; 3 in a row and it's mastered.";
        if (!compact_) pageTitle(deckName, intro);
        else {  // the top bar names the page
            wrappedIn(fonts.small, col::Muted, intro);
            ImGui::Dummy(ImVec2(0, 2));
        }
        if (!message_.empty()) {
            beginCallout();
            textIn(fonts.strong, col::Ink, message_.c_str());
            endCallout(col::AccentSoft, col::Accent);
        }
        figuresLine({{fmt("%d/%d", mastered, total), vocab ? "words mastered" : "cards mastered"},
                     {fmt("%d", total - mastered), "still learning"}});
        ImGui::Dummy(ImVec2(0, 8));
        if (vocab) {
            if (toggle("deffirst", &definitionFirst_)) saveSettings();
            ImGui::SameLine(0, 12);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
            textIn(fonts.body, col::InkSoft, "Definition first: guess the word from its meaning");
            ImGui::Dummy(ImVec2(0, 14));
            drawRepetition(false);
            if (flashActive_) return;  // a session just started
            ImGui::Dummy(ImVec2(0, 18));
            divider();
            ImGui::Dummy(ImVec2(0, 10));
            drawRepetition(true);
            if (flashActive_) return;
            ImGui::Dummy(ImVec2(0, 18));
        }

        struct Opt { const char* title; const char* desc; std::string cards; size_t count; bool learning; };
        const Opt opts[3] = {
            {vocab ? "Quick 20" : "Quick 10", vocab ? "Your weakest words first." : "The cards you know least first.",
             vocab ? "20" : "10", vocab ? 20u : 10u, false},
            {"Still learning", vocab ? "Only words you haven't mastered yet." : "Only cards you haven't mastered yet.",
             fmt("%d", std::min(30, total - mastered)), 30, true},
            {"Whole deck", "Every card once, in random order.", fmt("%d", total), 0, false},
        };
        std::vector<TableRow> rows;
        for (const Opt& o : opts) {
            TableRow r;
            r.name = o.title;
            r.note = o.desc;
            r.cells = {{o.cards, o.cards + " cards"}};
            if (!(o.learning && mastered == total)) r.buttons = {"Start"};
            rows.push_back(r);
        }
        const auto hit = drawTable("fc_sessions", vocab ? "OTHER SESSIONS" : "SESSIONS", {"CARDS"}, rows,
                                   textSize(fonts.strong, "Start").x + 34.0f);
        if (hit.first >= 0) {
            const Opt& o = opts[hit.first];
            startFlashcards(o.count, o.learning);
        }
        return;
    }

    // ---- Session finished
    if (deck.sessionDone() && repSession_) {
        const int streak = activeRep().streak();
        pageTitle(roots ? "Roots deck complete" : "Deck complete",
                  repCounted_ ? fmt("You went through your whole deck today. Your streak is now %s.",
                                    plural(streak, "day").c_str()).c_str()
                              : "You already finished your deck today, so the streak stays the same. Come back tomorrow "
                                "to add another day.");
        figuresLine({{fmt("%d", streak), "day streak"},
                     {fmt("%d", activeRep().bestStreak()), "best"},
                     {fmt("%d", deck.sessionGotIt()), "knew it"},
                     {fmt("%d", deck.sessionMissed()), "repeated"}});
        ImGui::Dummy(ImVec2(0, 8));
        if (button("Back to vocabulary cards")) flashActive_ = false;
        return;
    }
    if (deck.sessionDone()) {
        pageTitle("Session complete", roots ? "Nice work. Roots you marked \"still learning\" will come back sooner next time."
                                      : vocab ? "Nice work. Words you marked \"still learning\" will come back sooner next time."
                                            : "Nice work. Cards you marked \"still learning\" will come back sooner next time.");
        figuresLine({{fmt("%d", deck.sessionGotIt()), "knew it"},
                     {fmt("%d", deck.sessionMissed()), "still learning", deck.sessionMissed() ? col::TagStrong : col::Ink},
                     {fmt("%d/%d", mastered, total), roots ? "roots mastered" : vocab ? "words mastered" : "cards mastered"}});
        ImGui::Dummy(ImVec2(0, 8));
        if (button(vocab ? "Back to vocabulary cards" : "Back to Warrior Knowledge")) flashActive_ = false;
        return;
    }

    // ---- A card
    const Flashcard& c = deck.current();
    const float ty = ImGui::GetCursorPosY();
    const bool written = repSession_ && repMode_ != RepMode::Swipe;  // quiz or describe instead of a swipe card
    const char* sessionTitle = !repSession_                     ? deckName
                               : repMode_ == RepMode::Quiz     ? (roots ? "Word roots: quiz" : "Spaced repetition: quiz")
                               : repMode_ == RepMode::Describe ? (roots ? "Word roots: describe" : "Spaced repetition: describe")
                                                               : (roots ? "Word roots" : "Spaced repetition");
    textIn(compact_ ? fonts.heading : fonts.title, col::Ink, sessionTitle);
    alignRight(textSize(fonts.strong, "End session").x + 40);
    ImGui::SetCursorPosY(ty);
    if (button("End session", ButtonStyle::Outline)) {
        flashActive_ = false;
        return;
    }
    wrappedIn(fonts.body, col::Muted, fmt("%d %s left  \xC2\xB7  %d known  \xC2\xB7  %d still learning",
                                          static_cast<int>(deck.remaining()), written ? (roots ? "roots" : "words") : "cards",
                                          deck.sessionGotIt(), deck.sessionMissed()).c_str());
    ImGui::Dummy(ImVec2(0, 4));
    if (written) {
        if (repMode_ == RepMode::Quiz) drawDeckQuiz(c);
        else drawDeckDescribe(c);
        return;
    }

    const float minH = std::clamp(ImGui::GetContentRegionAvail().y - 170, 300.0f, vocab ? 420.0f : 520.0f);
    auto posName = [&] { return partName(c.partOfSpeech); };
    const char* thing = roots ? "part" : "word";
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
                cardText(fonts.body, col::Muted, fmt("Which %s has this meaning?", thing).c_str(), left, width);
            } else {
                cardText(fonts.label, col::Muted, posName(), left, width);
                ImGui::Dummy(ImVec2(0, 6));
                cardText(fonts.hero, col::Accent, c.word.c_str(), left, width);
                ImGui::Dummy(ImVec2(0, 14));
                cardText(fonts.body, col::Muted, fmt("What does this %s mean?", thing).c_str(), left, width);
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
            cardText(fonts.label, col::Muted, roots ? "EXAMPLES" : "SYNONYM", left, width);
            ImGui::Dummy(ImVec2(0, 4));
            cardText(fonts.strong, col::Good, c.synonym.c_str(), left, width);
            if (!c.example.empty()) {
                ImGui::Dummy(ImVec2(0, 10));
                cardText(fonts.body, col::InkSoft, ("\"" + c.example + "\"").c_str(), left, width);
            }
        });

    ImGui::Dummy(ImVec2(0, 10));
    swipeButtons(swipe_.flipped ? "Flip back" : "Flip card");
    ImGui::Dummy(ImVec2(0, 6));
    divider();
    wrappedIn(fonts.small, col::Muted, touch_ ? "Tap the card to flip it. Swipe it right if you knew it, left if you're still learning."
                                              : "Click the card or press Space to flip. Drag or swipe it right if you knew it, "
                                                "left if you're still learning (or use the arrow keys).");
    if (swiped != 0) {
        if (swiped > 0) sound_.correct();
        if (vocab) recordCard(c, swiped > 0);
        deck.mark(swiped > 0);
        if (repSession_ && deck.sessionDone()) repCounted_ = activeRep().completePass();
    }
}

// The spaced repetition decks on the vocabulary cards page: a small deck that grows
// 3 at a time, and a streak for going through all of it once a day. There are two,
// each with its own streak: vocabulary words, and word roots (prefixes, roots and
// suffixes, for decoding unfamiliar words).
void GuiApp::drawRepetition(bool roots) {
    const FlashcardDeck& cards = roots ? rootDeck_ : deck_;
    RepetitionDeck& rep = roots ? rootRep_ : repDeck_;
    const auto& words = rep.words();
    const int n = static_cast<int>(words.size());
    const int streak = rep.streak();
    const bool done = rep.doneToday();
    ImGui::PushID(roots ? "roots" : "vocab");

    textIn(fonts.heading, col::Ink, roots ? "Word roots" : "Spaced repetition");
    wrappedIn(fonts.small, col::Muted,
              roots ? "Prefixes, roots and suffixes, so you can take apart a word you've never seen on test day "
                      "(bene + vol = wishing good). Same rules as your word deck: it starts with 3, you go through "
                      "it once a day for its own streak, and you add 3 more whenever you're ready."
                    : "Your own deck, starting with 3 words. Go through the whole deck once a day to keep your streak, "
                      "with swipe cards, a multiple choice quiz, or by describing each word in your own words. Words you "
                      "miss come back until you know them. Add 3 more words whenever you're ready.");
    ImGui::Dummy(ImVec2(0, 6));
    int mastered = 0;
    for (const auto& w : words) mastered += cards.box(w) >= FlashcardDeck::kMasteredBox ? 1 : 0;
    figuresLine({{fmt("%d", streak), "day streak"},
                 {done ? "Done" : "Not yet", "today", done ? col::Ink : streak > 0 ? col::TagStrong : col::Ink},
                 {fmt("%d", n), roots ? (n == 1 ? "root in deck" : "roots in deck") : (n == 1 ? "word in deck" : "words in deck")},
                 {fmt("%d", mastered), "mastered"}});
    ImGui::Dummy(ImVec2(0, 4));
    std::string list;
    for (const auto& w : words) list += (list.empty() ? "" : ", ") + w;
    wrappedIn(fonts.small, col::Muted, list.c_str());
    ImGui::Dummy(ImVec2(0, 6));

    // Three ways to go through the deck; the last one used stands out until today is done
    struct Way { const char* label; RepMode mode; };
    const Way ways[3] = {{"Swipe cards", RepMode::Swipe}, {"Quiz", RepMode::Quiz}, {"Describe", RepMode::Describe}};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) sameLineIfFits(buttonWidth(ways[i].label));
        const bool strong = !done && ways[i].mode == repMode_;
        if (button(ways[i].label, strong ? ButtonStyle::Primary : ButtonStyle::Outline)) {
            startRepetition(ways[i].mode, roots);
            ImGui::PopID();
            return;
        }
    }
    const int canAdd = rep.canAdd(cards);
    const char* unit = roots ? (canAdd == 1 ? "root" : "roots") : (canAdd == 1 ? "word" : "words");
    if (canAdd > 0) {
        const std::string add = fmt("Add %d %s", canAdd, unit);
        sameLineIfFits(buttonWidth(add.c_str()));
        if (button(add.c_str(), ButtonStyle::Outline)) rep.addMore(cards);
    }
    const int last = rep.lastBatchSize();
    if (last > 0) {
        const std::string remove = fmt("Remove the last %d", last);
        sameLineIfFits(buttonWidth(remove.c_str()));
        if (button(remove.c_str(), ButtonStyle::Outline)) rep.removeLastBatch();
    }
    if (canAdd == 0) wrappedIn(fonts.small, col::Muted, roots ? "Every root is in your deck." : "Every vocabulary word is in your deck.");
    ImGui::PopID();
}

// ---- Spaced repetition: quiz and describe

namespace {
std::string lowerCopy(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}
}  // namespace

// A Word Knowledge style question with five choices. The wrong choices come from
// the whole vocabulary file (words of the same part of speech first, so the right
// one doesn't stand out), never the same text twice. A word that comes back in the
// same session is asked one of the other two ways.
void GuiApp::buildDeckQuestion(const Flashcard& c) {
    int kind = std::uniform_int_distribution<int>(0, 2)(rng_);
    const auto last = repLastKind_.find(c.word);
    if (last != repLastKind_.end()) kind = (last->second + 1 + std::uniform_int_distribution<int>(0, 1)(rng_)) % 3;
    repLastKind_[c.word] = kind;

    // Roots deck: the "synonym" field holds example words, so the third way asks which
    // part a few words share (the choices are other roots)
    const bool roots = repRoots_;
    auto field = [kind, roots](const Flashcard& f) -> const std::string& {
        return kind == 0 ? f.definition : kind == 1 || roots ? f.word : f.synonym;
    };
    std::vector<const Flashcard*> same, other;
    for (const Flashcard& f : activeDeck().cards()) {
        if (f.word == c.word || field(f).empty()) continue;
        (f.partOfSpeech == c.partOfSpeech ? same : other).push_back(&f);
    }
    std::shuffle(same.begin(), same.end(), rng_);
    std::shuffle(other.begin(), other.end(), rng_);
    same.insert(same.end(), other.begin(), other.end());

    DeckQuestion q;
    q.kind = kind;
    q.choices.push_back(field(c));
    std::set<std::string> used{lowerCopy(field(c)), lowerCopy(c.word)};
    for (const Flashcard* f : same) {
        if (q.choices.size() == 5) break;
        if (!used.insert(lowerCopy(field(*f))).second) continue;
        q.choices.push_back(field(*f));
    }
    std::shuffle(q.choices.begin(), q.choices.end(), rng_);
    q.correct = static_cast<int>(std::find(q.choices.begin(), q.choices.end(), field(c)) - q.choices.begin());
    if (roots)
        q.prompt = kind == 0 ? "What does " + upper(c.word) + " mean?"
                 : kind == 1 ? "Which part means: \"" + c.definition + "\""
                             : "Which part do these words share: " + c.synonym + "?";
    else
        q.prompt = kind == 0 ? "What does " + upper(c.word) + " mean?"
                 : kind == 1 ? "Which word means: \"" + c.definition + "\""
                             : upper(c.word) + " most nearly means:";
    repQ_ = q;
}

void GuiApp::drawDeckQuiz(const Flashcard& c) {
    if (repQ_.kind < 0) buildDeckQuestion(c);
    const DeckQuestion& q = repQ_;
    const bool answered = q.chosen >= 0;
    const float anim = feedbackTime_ < 0 ? -1.0f : static_cast<float>(ImGui::GetTime() - feedbackTime_);

    beginAutoCard("deckquiz", ImVec2(32, 30));
    textIn(fonts.label, col::Muted, q.kind == 0 ? "MEANING" : q.kind == 1 ? (repRoots_ ? "PART" : "WORD") : (repRoots_ ? "EXAMPLES" : "SYNONYM"));
    ImGui::Dummy(ImVec2(0, 2));
    wrappedIn(fonts.prompt, col::Ink, q.prompt.c_str());
    ImGui::Dummy(ImVec2(0, 6));

    int clicked = -1;
    const float aw = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 10));
    for (int i = 0; i < static_cast<int>(q.choices.size()); ++i) {
        AnswerState st = AnswerState::Normal;
        if (answered) st = i == q.correct ? AnswerState::Correct : i == q.chosen ? AnswerState::Wrong : AnswerState::Dimmed;
        if (st == AnswerState::Wrong && anim >= 0 && anim < 0.4f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::sin(anim * 48.0f) * 7.0f * (1.0f - anim / 0.4f));
        ImGui::PushID(i);
        const bool animated = st == AnswerState::Correct || st == AnswerState::Wrong;
        if (answerCard("ans", static_cast<char>('A' + i), q.choices[static_cast<size_t>(i)].c_str(), st, aw,
                       animated ? anim : -1.0f))
            clicked = i;
        ImGui::PopID();
        if (!answered && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + i), false)) clicked = i;
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 4));

    bool next = false;
    if (answered) {
        const bool right = q.chosen == q.correct;
        beginCallout();
        textIn(fonts.heading, right ? col::Good : col::Bad, right ? "Correct" : "Not quite");
        textIn(fonts.strong, col::Ink, (c.word + "  (" + c.partOfSpeech + ")").c_str());
        wrappedIn(fonts.body, col::InkSoft, c.definition.c_str());
        if (!c.synonym.empty()) wrappedIn(fonts.body, col::InkSoft, ((repRoots_ ? "Examples: " : "Synonym: ") + c.synonym).c_str());
        if (!c.example.empty()) wrappedIn(fonts.body, col::InkSoft, ("\"" + c.example + "\"").c_str());
        if (!right) wrappedIn(fonts.small, col::Muted, "It comes back at the end, asked a different way.");
        endCallout(right ? col::GoodSoft : col::BadSoft, right ? col::Good : col::Bad);
        next = button(touch_ ? "Next  \xE2\x86\x92" : "Next  \xE2\x86\x92   Enter", ButtonStyle::Primary,
                      ImVec2(compact_ ? ImGui::GetContentRegionAvail().x : 0, compact_ ? 48 : 0)) ||
               ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    }
    if (!touch_) {
        ImGui::Dummy(ImVec2(0, 4));
        divider();
        wrappedIn(fonts.small, col::Muted, "Keys: A\xE2\x80\x93" "E to answer, Enter for next.");
    }
    endAutoCard();

    if (!answered && clicked >= 0) {
        repQ_.chosen = clicked;
        feedbackTime_ = ImGui::GetTime();
        if (clicked == q.correct) sound_.correct();
        else sound_.wrong();
    } else if (next) {
        finishDeckCard(repQ_.chosen == repQ_.correct);
    }
}

// Describe: type what the word means, then compare it with the real definition and
// say whether it matched. (Grading is the user's call; a grader could later take
// repText_ and the card and give its own verdict before the Got it / Missed step.)
void GuiApp::drawDeckDescribe(const Flashcard& c) {
    beginAutoCard("deckdescribe", ImVec2(32, 30));
    textIn(fonts.label, col::Muted, partName(c.partOfSpeech));
    ImGui::Dummy(ImVec2(0, 2));
    textIn(fonts.hero, col::Accent, c.word.c_str());
    ImGui::Dummy(ImVec2(0, 8));

    int verdict = 0;  // +1 got it, -1 missed
    if (!repRevealed_) {
        wrappedIn(fonts.body, col::InkSoft, repRoots_ ? "Describe what this part means in your own words."
                                                      : "Describe what this word means in your own words.");
        ImGui::Dummy(ImVec2(0, 4));
        const float w = ImGui::GetContentRegionAvail().x;
        textArea("##describe", "Type your answer", repText_, sizeof repText_, ImVec2(w, compact_ ? 130.0f : 120.0f),
                 repFocus_ && !touch_);
        repFocus_ = false;
        ImGui::Dummy(ImVec2(0, 4));
        bool typed = false;
        for (const char* p = repText_; *p && !typed; ++p) typed = !std::isspace(static_cast<unsigned char>(*p));
        if (button("Show answer", typed ? ButtonStyle::Primary : ButtonStyle::Disabled)) repRevealed_ = true;
        sameLineIfFits(buttonWidth("I don't know"));
        if (button("I don't know", ButtonStyle::Outline)) repRevealed_ = repDontKnow_ = true;
        if (!typed) wrappedIn(fonts.small, col::Muted, "Type something first, then compare it with the real definition.");
    } else {
        if (!repDontKnow_) {
            textIn(fonts.label, col::Muted, "YOUR ANSWER");
            ImGui::Dummy(ImVec2(0, 2));
            std::string mine = repText_;
            std::replace(mine.begin(), mine.end(), '\n', ' ');  // the box's own line breaks
            wrappedIn(fonts.body, col::Ink, mine.c_str());
            ImGui::Dummy(ImVec2(0, 10));
        }
        textIn(fonts.label, col::Muted, "DEFINITION");
        ImGui::Dummy(ImVec2(0, 2));
        wrappedIn(fonts.prompt, col::Ink, c.definition.c_str());
        if (!c.synonym.empty()) {
            ImGui::Dummy(ImVec2(0, 8));
            textIn(fonts.label, col::Muted, repRoots_ ? "EXAMPLES" : "SYNONYM");
            ImGui::Dummy(ImVec2(0, 2));
            wrappedIn(fonts.strong, col::Good, c.synonym.c_str());
        }
        if (!c.example.empty()) {
            ImGui::Dummy(ImVec2(0, 8));
            wrappedIn(fonts.body, col::InkSoft, ("\"" + c.example + "\"").c_str());
        }
        ImGui::Dummy(ImVec2(0, 10));
        divider();
        if (repDontKnow_) {
            wrappedIn(fonts.body, col::InkSoft, "Counted as still learning. It comes back at the end.");
            ImGui::Dummy(ImVec2(0, 4));
            if (button(touch_ ? "Next  \xE2\x86\x92" : "Next  \xE2\x86\x92   Enter") || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
                verdict = -1;
        } else {
            wrappedIn(fonts.body, col::InkSoft, "Did your answer match the meaning?");
            ImGui::Dummy(ImVec2(0, 4));
            if (button("Got it")) verdict = 1;
            sameLineIfFits(buttonWidth("Missed"));
            if (button("Missed", ButtonStyle::Outline)) verdict = -1;
        }
    }
    endAutoCard();
    if (verdict > 0) sound_.correct();
    if (verdict != 0) finishDeckCard(verdict > 0);
}

void GuiApp::finishDeckCard(bool knewIt) {
    recordCard(activeDeck().current(), knewIt);
    activeDeck().mark(knewIt);
    repQ_ = DeckQuestion{};
    repText_[0] = 0;
    repRevealed_ = repDontKnow_ = false;
    repFocus_ = true;
    feedbackTime_ = -1.0;
    if (activeDeck().sessionDone()) repCounted_ = activeRep().completePass();
}

// Warrior Knowledge spaced repetition: the user picks which handbook blocks are in
// the deck (a whole block at a time) and goes through all of it once a day for the
// streak. Cards swiped left come back at the end of the session until known.
void GuiApp::drawWarriorRepetition() {
    const char* intro = "Your own deck, built a block at a time. Go through the whole deck once a day to keep your streak; "
              "cards you swipe left come back at the end until you know them. Add the next block whenever you're ready.";
    if (!compact_) pageTitle("Spaced repetition", intro);
    else {  // the top bar names the page
        wrappedIn(fonts.small, col::Muted, intro);
        ImGui::Dummy(ImVec2(0, 2));
    }
    if (!message_.empty()) {
        beginCallout();
        textIn(fonts.strong, col::Ink, message_.c_str());
        endCallout(col::AccentSoft, col::Accent);
    }

    const auto blocks = trackSections();
    auto cardsIn = [&](int b) { return static_cast<int>(bank_.size(fmt("wk_block%02d", b))); };
    int cards = 0;
    for (int b : wkRep_.blocks()) cards += cardsIn(b);
    const int streak = wkRep_.streak();
    const bool done = wkRep_.doneToday();

    const int nb = static_cast<int>(wkRep_.blocks().size());
    figuresLine({{fmt("%d", streak), "day streak"},
                 {fmt("%d", wkRep_.bestStreak()), "best"},
                 {done ? "Done" : "Not yet", "today", done ? col::Ink : streak > 0 ? col::TagStrong : col::Ink},
                 {fmt("%d", cards), cards == 1 ? "card in deck" : "cards in deck"},
                 {fmt("%d/%d", nb, static_cast<int>(blocks.size())), "blocks"}});
    ImGui::Dummy(ImVec2(0, 10));

    if (button(fmt("Practice my deck (%s)", plural(cards, "card").c_str()).c_str(),
               done ? ButtonStyle::Outline : ButtonStyle::Primary,
               ImVec2(compact_ ? ImGui::GetContentRegionAvail().x : 0, 40))) {
        startWarriorRepetition();
        return;
    }
    ImGui::Dummy(ImVec2(0, 14));

    // One row per block: add it to the deck or take it out (the deck keeps at least one)
    std::vector<TableRow> rows;
    for (const auto& s : blocks) {
        const int b = std::atoi(s.id.c_str() + std::strlen("wk_block"));
        const bool in = wkRep_.has(b);
        TableRow r;
        r.name = s.name;
        r.cells = {{fmt("%d", cardsIn(b)), plural(cardsIn(b), "card")},
                   {in ? "In deck" : "\xE2\x80\x94", in ? "In deck" : "", in ? col::Accent : col::Muted}};
        if (!in) r.buttons = {"Add"};
        else if (nb > 1) r.buttons = {"Remove"};
        rows.push_back(r);
    }
    const auto hit = drawTable("wk_rep_blocks", "BLOCK", {"CARDS", "DECK"}, rows, textSize(fonts.strong, "Remove").x + 34.0f);
    if (hit.first >= 0) {
        const int b = std::atoi(blocks[static_cast<size_t>(hit.first)].id.c_str() + std::strlen("wk_block"));
        if (wkRep_.has(b)) wkRep_.remove(b);
        else wkRep_.add(b);
    }
    if (nb == 1) {
        ImGui::Dummy(ImVec2(0, 2));
        wrappedIn(fonts.small, col::Muted, "The last block in your deck can't be removed.");
    }
}

// ================================================================ progress

void GuiApp::drawProgress() {
    const bool afoqt = track_ == "afoqt";
    if (!compact_) pageTitle("My progress");
    drawFigures();
    ImGui::Dummy(ImVec2(0, 10));

    const auto& sessions = progress_.sessions();
    const auto stats = progress_.stats();
    const std::string focus = focusId();
    const char* dash = "\xE2\x80\x94";

    // ---- By section: accuracy, pace, and the last 10 sessions as a line
    const auto secs = trackSections();
    std::vector<TableRow> rows;
    for (const auto& s : secs) {
        auto it = stats.find(s.id);
        const bool tried = it != stats.end() && it->second.total > 0;
        std::vector<float> trend;
        for (auto e = sessions.rbegin(); e != sessions.rend() && trend.size() < 10; ++e)
            if (!e->review && e->subtestId == s.id && e->total > 0)
                trend.push_back(100.0f * static_cast<float>(e->correct) / static_cast<float>(e->total));
        std::reverse(trend.begin(), trend.end());
        TableRow r;
        r.name = s.name;
        if (s.id == focus) r.tag = "FOCUS";
        r.cells = {{tried ? fmt("%d", it->second.sessions) : dash, tried ? plural(it->second.sessions, "session") : "Not started"},
                   {tried ? fmt("%.0f%%", it->second.accuracy()) : dash,
                    tried ? fmt("%.0f%% %s", it->second.accuracy(), afoqt ? "accuracy" : "known") : "",
                    s.id == focus && tried ? col::TagStrong : col::Ink},
                   {tried ? fmt("%.0f s", it->second.avgSecondsPerQuestion()) : dash,
                    tried ? fmt("%.0f sec per %s", it->second.avgSecondsPerQuestion(), afoqt ? "question" : "card") : ""},
                   {"", "", col::Ink, trend}};
        r.focusMenu = registry_.find(s.id);
        rows.push_back(r);
    }
    drawTable("prog_sections", afoqt ? "SECTION" : "BLOCK", {"SESSIONS", afoqt ? "ACCURACY" : "KNOWN", "PACE", "LAST 10"}, rows);
    ImGui::Dummy(ImVec2(0, 8));

    // ---- Session log, newest first
    auto belongs = [&](const std::string& id) {
        if (id == "review") return true;  // missed-question reviews aren't tied to one section
        if (id == "repetition_warrior") return !afoqt;
        return inTrack(id);
    };
    auto label = [&](const std::string& id) -> std::string {
        if (id == "review") return "Missed-question review";
        if (id == "repetition_warrior") return "Spaced repetition deck";
        if (id.rfind("warmup", 0) == 0) return "Warm-up";
        const SubtestInfo* s = registry_.find(id);
        return s ? s->name : id;
    };
    std::vector<const ProgressStore::SessionEntry*> log;
    for (auto e = sessions.rbegin(); e != sessions.rend(); ++e)
        if (belongs(e->subtestId)) log.push_back(&*e);
    const size_t kShown = 20;
    const size_t shown = showAllSessions_ ? log.size() : std::min(log.size(), kShown);
    std::vector<TableRow> lrows;
    for (size_t i = 0; i < shown; ++i) {
        const auto& e = *log[i];
        // "Today 17:57", "Yesterday 17:57", or the date ("Nov 3 17:57"): short enough for its column
        std::string day = whenText(e.date);
        if (day != "Today" && day != "Yesterday" && e.date.size() >= 10) {
            static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            day = fmt("%s %d", months[std::clamp(std::atoi(e.date.c_str() + 5) - 1, 0, 11)], std::atoi(e.date.c_str() + 8));
        }
        const std::string when = day + (e.date.size() >= 16 ? " " + e.date.substr(11, 5) : "");
        const char* type = e.subtestId == "repetition_warrior" ? "Deck"
                           : e.review                         ? "Review"
                           : e.subtestId.rfind("warmup", 0) == 0 ? "Warm-up"
                           : e.timed                          ? "Timed"
                                                              : "Practice";
        TableRow r;
        r.name = label(e.subtestId);
        r.cells = {{when, when},
                   {type, type},
                   {fmt("%d/%d", e.correct, e.total), fmt("%d of %d right", e.correct, e.total)},
                   {mmss(static_cast<int>(e.seconds)), mmss(static_cast<int>(e.seconds))}};
        lrows.push_back(r);
    }
    if (lrows.empty()) {
        wrappedIn(fonts.body, col::Muted, "No sessions yet. Practice a section and it shows up here.");
        return;
    }
    drawTable("prog_log", "SESSION LOG", {"WHEN", "TYPE", "SCORE", "TIME"}, lrows);
    if (log.size() > kShown) {
        ImGui::Dummy(ImVec2(0, 2));
        const std::string more = showAllSessions_ ? std::string("Show the latest 20") : fmt("Show all %d sessions", static_cast<int>(log.size()));
        if (button(more.c_str(), ButtonStyle::Outline)) showAllSessions_ = !showAllSessions_;
    }
}

// ================================================================ settings

void GuiApp::drawSettings() {
    if (!compact_) pageTitle("Settings", "Changes save right away. Settings stay on this device.");
    if (compact_) {  // Account first on phones, so the keyboard doesn't cover the fields
        drawAccount();
        ImGui::Dummy(ImVec2(0, kGap - 12));
    }

    beginAutoCard("settings", ImVec2(20, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    float y = o.y;
    bool firstGroup = true;
    auto group = [&](const char* label) {
        y += firstGroup ? 12 : 26;
        firstGroup = false;
        ImGui::SetCursorScreenPos(ImVec2(o.x, y));
        spacedLabel(label, col::Muted, 1.2f);
        y += fontPx(fonts.label) + 12;
    };
    // One setting: the name and a line about it on the left, its control on the right
    auto row = [&](const char* title, const std::string& sub, float controlW, float controlH, const auto& control) {
        dl->AddLine(ImVec2(o.x, y), ImVec2(o.x + w, y), u32(col::CardBorder), 1.0f);
        y += 14;
        const float textW = w - controlW - 24;
        dl->AddText(fonts.strong, fontPx(fonts.strong), ImVec2(o.x, y), u32(col::Ink), title);
        float th = fontPx(fonts.strong);
        if (!sub.empty()) {
            dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(o.x, y + th + 3), u32(col::Muted), sub.c_str(), nullptr, textW);
            th += 3 + textSize(fonts.small, sub.c_str(), textW).y;
        }
        const float rowH = std::max(th, controlH);
        ImGui::SetCursorScreenPos(ImVec2(o.x + w - controlW, y + (rowH - controlH) * 0.5f));
        control();
        y += rowH + 14;
    };

    group("APPEARANCE");
    row("Dark mode", "A near-black background for studying at night.", 42, 22, [&] {
        if (toggle("dark", &darkMode_)) {
            setDarkMode(darkMode_);
            saveSettings();
        }
    });

    group("STUDY");
    row("Definition first", "AFOQT vocabulary cards show the meaning, and you guess the word.", 42, 22, [&] {
        if (toggle("def", &definitionFirst_)) saveSettings();
    });
    const SubtestInfo* fi = registry_.find(focusId());
    const std::string focusLine = std::string(trackInfo(track_).name) + ": " +
                                  (fi ? fi->name + (pinnedFocus_.empty() ? " (automatic, lowest accuracy)" : " (pinned)")
                                      : std::string("not set yet"));
    const float changeW = textSize(fonts.strong, "Change").x + 26;
    row("Focus area", focusLine, changeW, 32, [&] {
        if (button("Change##focus", ButtonStyle::Outline, ImVec2(changeW, 32))) ImGui::OpenPopup("focuspicker3");
    });

    group("SOUND");
    row("Sound effects", "A chime when you get an answer right, and a low tone when you miss.", 42, 22, [&] {
        if (toggle("snd", &sound_.enabled)) {
            saveSettings();
            if (sound_.enabled) sound_.correct();  // let them hear it
        }
    });
    const float rightW = textSize(fonts.strong, "Right").x + 26, wrongW = textSize(fonts.strong, "Wrong").x + 26;
    row("Test the sounds", sound_.enabled ? "" : "Turn sound effects on to hear them.", rightW + 8 + wrongW, 32, [&] {
        if (button("Right##snd", ButtonStyle::Outline, ImVec2(rightW, 32)) && sound_.enabled) sound_.correct();
        ImGui::SameLine(0, 8);
        if (button("Wrong##snd", ButtonStyle::Outline, ImVec2(wrongW, 32)) && sound_.enabled) sound_.wrong();
    });

    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    ImGui::Dummy(ImVec2(w, 4));
    ImGui::PopStyleVar();
    if (beginPopup("focuspicker3")) {
        focusMenuItems();
        ImGui::EndPopup();
    }
    endAutoCard();

    if (!compact_) {
        ImGui::Dummy(ImVec2(0, kGap - 12));
        drawAccount();
    }
}

void GuiApp::drawAccount() {
    beginAutoCard("set_account", ImVec2(20, 16));  // same padding as the settings list
    spacedLabel("ACCOUNT", col::Muted, 1.2f);
    ImGui::Dummy(ImVec2(0, 2));
    const float fieldW = std::min(360.0f, ImGui::GetContentRegionAvail().x);

    if (!cloud_.configured()) {
        textIn(fonts.strong, col::Ink, "Online sync isn't set up");
        wrappedIn(fonts.small, col::Muted,
                  "This copy was built without a server address, so progress stays on this device. "
                  "SYNC.md in the project folder explains how to turn it on.");
        endAutoCard();
        return;
    }

    if (!cloud_.signedIn()) {
        textIn(fonts.strong, col::Ink, "Sign in to sync your progress");
        wrappedIn(fonts.small, col::Muted,
                  "Practice history, missed questions, and flashcards stay the same on your Mac, iPhone, and iPad.");
        ImGui::Dummy(ImVec2(0, 4));
        const bool busy = cloud_.busy();
        if (busy) ImGui::BeginDisabled();
        bool go = textField("##email", "Email", email_, sizeof email_, ui::TextKind::Email, fieldW);
        go |= textField("##password", "Password", password_, sizeof password_, ui::TextKind::Password, fieldW);
        ImGui::Dummy(ImVec2(0, 2));
        go |= button(busy ? "Signing in..." : "Sign in");
        if (busy) ImGui::EndDisabled();
        if (go && !busy && email_[0] && password_[0]) {
            std::string email = email_;
            for (char& c : email) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            cloud_.signIn(email, password_);
            std::fill(std::begin(password_), std::end(password_), '\0');
            ImGui::ClearActiveID();  // drop focus so the keyboard goes away
        }
        if (cloud_.failed()) wrappedIn(fonts.small, col::Bad, cloud_.status().c_str());
        endAutoCard();
        return;
    }

    textIn(fonts.strong, col::Ink, fmt("Signed in as %s", cloud_.email().c_str()).c_str());
    wrappedIn(fonts.small, cloud_.failed() ? col::Bad : col::Muted, cloud_.status().c_str());
    wrappedIn(fonts.small, col::Muted,
              "Progress syncs on its own: when the app opens, after you practice, and every 10 minutes.");
    ImGui::Dummy(ImVec2(0, 4));
    if (cloud_.busy()) ImGui::BeginDisabled();
#if defined(__EMSCRIPTEN__)  // the website: this goes back to the login page
    if (button("Log out", ButtonStyle::Outline)) cloud_.signOut();
#else
    if (button("Sign out", ButtonStyle::Outline)) cloud_.signOut();
#endif
    if (cloud_.busy()) ImGui::EndDisabled();
    endAutoCard();
}

void GuiApp::drawComposites() {
    const auto estimates = estimateComposites(progress_.stats());
    auto nameOf = [&](const std::string& id) {
        const SubtestInfo* s = registry_.find(id);
        return s ? s->name : id;
    };
    beginAutoCard("composites", ImVec2(20, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool stacked = compact_ || w < 640.0f;
    const float estX = o.x + std::min(300.0f, w * 0.34f);
    const float barW = std::min(160.0f, w * 0.18f), barX = estX + 70.0f, noteX = barX + barW + 28.0f;

    float y = o.y + 12;
    auto label = [&](float x, const char* t) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        spacedLabel(t, col::Muted, 1.2f);
    };
    label(o.x, "ESTIMATED COMPOSITE");
    if (!stacked) {
        label(estX, "SCORE");
        label(noteX, "HOLDING IT BACK");
    }
    y += fontPx(fonts.label) + 12;

    const char* dash = "\xE2\x80\x94";
    for (const CompositeEstimate& e : estimates) {
        dl->AddLine(ImVec2(o.x, y), ImVec2(o.x + w, y), u32(col::CardBorder), 1.0f);
        // The score shows only once every section in the composite has been practiced;
        // an average of one or two sections would look like a real score.
        const bool full = e.hasData() && e.complete();
        const int nSections = static_cast<int>(e.info->sections.size());
        std::string note1, note2;
        if (!e.hasData()) {
            note1 = fmt("%d sections, none practiced yet", nSections);
        } else if (full) {
            note1 = "Weakest: " + nameOf(e.weakest);
        } else {
            note1 = fmt("%d of %d sections practiced", e.practiced, nSections);
            note2 = "Not practiced: ";
            for (size_t k = 0; k < e.missing.size(); ++k) note2 += (k ? ", " : "") + nameOf(e.missing[k]);
        }
        const std::string score = full ? fmt("%.0f%%", e.score) : dash;
        const float sh = fontPx(fonts.strong), smh = fontPx(fonts.small);
        auto bar = [&](float x, float by, float bw) {
            dl->AddRectFilled(ImVec2(x, by), ImVec2(x + bw, by + 5), u32(col::Track));
            if (full) dl->AddRectFilled(ImVec2(x, by), ImVec2(x + bw * static_cast<float>(e.score / 100.0), by + 5), u32(col::Accent));
        };
        auto notes = [&](float x, float ny, float nw) {
            dl->AddText(fonts.small, smh, ImVec2(x, ny), u32(col::InkSoft), note1.c_str(), nullptr, nw);
            ny += textSize(fonts.small, note1.c_str(), nw).y;
            if (!note2.empty()) {
                ny += 3;
                dl->AddText(fonts.small, smh, ImVec2(x, ny), u32(col::TagStrong), note2.c_str(), nullptr, nw);
                ny += textSize(fonts.small, note2.c_str(), nw).y;
            }
            return ny;
        };
        y += 14;
        if (!stacked) {
            const float descW = estX - o.x - 20;
            dl->AddText(fonts.strong, sh, ImVec2(o.x, y), u32(col::Ink), e.info->name.c_str());
            dl->AddText(fonts.small, smh, ImVec2(o.x, y + sh + 3), u32(col::Muted), e.info->description.c_str(), nullptr, descW);
            const float leftH = sh + 3 + textSize(fonts.small, e.info->description.c_str(), descW).y;
            dl->AddText(fonts.monoStrong, fontPx(fonts.monoStrong), ImVec2(estX, y), u32(full ? col::Ink : col::Muted), score.c_str());
            bar(barX, y + (sh - 5) * 0.5f, barW);
            const float rightH = notes(noteX, y + 1, o.x + w - noteX) - y;
            y += std::max(leftH, rightH) + 14;
        } else {
            dl->AddText(fonts.strong, sh, ImVec2(o.x, y), u32(col::Ink), e.info->name.c_str());
            const float sw = textSize(fonts.monoStrong, score.c_str()).x;
            dl->AddText(fonts.monoStrong, fontPx(fonts.monoStrong), ImVec2(o.x + w - sw, y), u32(full ? col::Ink : col::Muted), score.c_str());
            y += sh + 3;
            dl->AddText(fonts.small, smh, ImVec2(o.x, y), u32(col::Muted), e.info->description.c_str(), nullptr, w);
            y += textSize(fonts.small, e.info->description.c_str(), w).y + 8;
            bar(o.x, y, w);
            y = notes(o.x, y + 13, w) + 14;
        }
    }
    dl->AddLine(ImVec2(o.x, y), ImVec2(o.x + w, y), u32(col::CardBorder), 1.0f);
    y += 10;
    const char* disclaimer = "Estimates from your practice accuracy, not an official AFOQT score.";
    dl->AddText(fonts.small, fontPx(fonts.small), ImVec2(o.x, y), u32(col::Muted), disclaimer, nullptr, w);
    y += textSize(fonts.small, disclaimer, w).y + 12;
    ImGui::SetCursorScreenPos(ImVec2(o.x, y));
    ImGui::Dummy(ImVec2(w, 0));
    ImGui::PopStyleVar();
    endAutoCard();
}

// ================================================================ actions

void GuiApp::goTo(Screen s) {
    menuOpen_ = false;
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
    endless_ = false;
    isReview_ = false;
    feedbackTime_ = -1.0;
    relDrill_ = weeklySession_ = false;
    cardMode_ = track_ == "warrior";
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
    engine_.start(s.id, std::move(questions), mode, s.timeLimitSeconds);
    screen_ = Screen::Quiz;
}

void GuiApp::startEndless(const SubtestInfo& s) {
    auto questions = QuestionGenerator::generate(s.id, 40, rng_);
    if (questions.empty()) {
        message_ = "No questions can be generated for " + s.name + ".";
        return;
    }
    for (auto& q : questions) QuestionBank::shuffleChoices(q, rng_);  // generated: the answer is always first
    message_.clear();
    sessionName_ = s.name;
    sessionTag_ = "ENDLESS";
    sessionTips_ = s.tips;
    sessionTimeLimit_ = 0;
    isReview_ = false;
    endless_ = true;
    feedbackTime_ = -1.0;
    relDrill_ = weeklySession_ = false;
    cardMode_ = false;
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
    engine_.start(s.id, std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::topUpEndless() {
    if (!endless_ || engine_.finished() || engine_.total() - engine_.index() > 2) return;
    auto more = QuestionGenerator::generate(engine_.result().subtestId, 40, rng_);
    for (auto& q : more) QuestionBank::shuffleChoices(q, rng_);
    engine_.append(std::move(more));
}

void GuiApp::saveGenerated(const Question& q) {
    const Question copy = saved_.add(q);  // stored under its own id, so it can be found again
    bank_.add(copy);
    progress_.markMissed(copy.id);        // shows up in Review missed until you get it right
    savedGen_.insert(q.id);
}

void GuiApp::saveButton(const Question& q, bool fullWidth, bool sameLine) {
    // Only generated questions need saving (written ones go on the review list by themselves),
    // and only plain text ones: pictures (pilot sections) aren't stored.
    if (q.id.rfind("GEN-", 0) != 0 || q.visual.kind != VisualKind::None || !q.choiceVisuals.empty()) return;
    const bool done = savedGen_.count(q.id) > 0;
    if (sameLine) ImGui::SameLine(0, 10);
    ImGui::PushID(q.id.c_str());
    if (button(done ? "Saved for review" : "Save for review", ButtonStyle::Outline,
               ImVec2(fullWidth ? ImGui::GetContentRegionAvail().x : 0, fullWidth ? 44 : 0)) &&
        !done)
        saveGenerated(q);
    ImGui::PopID();
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
    endless_ = false;
    isReview_ = false;
    feedbackTime_ = -1.0;
    relDrill_ = weeklySession_ = false;
    cardMode_ = track_ == "warrior";
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
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
    endless_ = false;
    isReview_ = true;
    feedbackTime_ = -1.0;
    relDrill_ = weeklySession_ = false;
    cardMode_ = track_ == "warrior";
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
    engine_.start("review", std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::startFlashcards(size_t count, bool learningOnly) {
    repRoots_ = false;
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

void GuiApp::startRepetition(RepMode mode, bool roots) {
    repRoots_ = roots;
    activeDeck().startSession(activeRep().words(), rng_);
    repMode_ = mode;
    repQ_ = DeckQuestion{};
    repLastKind_.clear();
    repText_[0] = 0;
    repRevealed_ = repDontKnow_ = false;
    repFocus_ = true;
    feedbackTime_ = -1.0;
    swipe_ = SwipeCard{};
    repSession_ = true;
    repCounted_ = false;
    message_.clear();
    flashActive_ = !activeDeck().sessionDone();
}

void GuiApp::startWarriorRepetition() {
    std::vector<Question> questions;
    for (int b : wkRep_.blocks()) {
        auto all = bank_.draw(fmt("wk_block%02d", b), 0, rng_);
        questions.insert(questions.end(), all.begin(), all.end());
    }
    if (questions.empty()) {
        message_ = "No cards in your deck yet. Add a block below.";
        return;
    }
    std::shuffle(questions.begin(), questions.end(), rng_);
    message_.clear();
    sessionName_ = "My deck";
    sessionTag_ = "SPACED REPETITION";
    sessionTips_ = "";
    sessionTimeLimit_ = 0;
    endless_ = false;
    isReview_ = true;  // missed cards repeat, so the session doesn't count toward accuracy
    wkRepCounted_ = false;
    feedbackTime_ = -1.0;
    relDrill_ = weeklySession_ = false;
    cardMode_ = true;
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
    engine_.start("repetition_warrior", std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::finishSession() {
    progress_.record(engine_.result(), isReview_, sessionTag_ == "TIMED TEST");
    recordMisses();
    if (weeklySession_) weekly_.complete(engine_.result().correctCount(), static_cast<int>(engine_.questions().size()));
    if (warriorRepSession()) wkRepCounted_ = wkRep_.completePass();  // the whole deck, so the day counts
    feedbackTime_ = -1.0;
    screen_ = Screen::Results;
}

// ================================================================ verbal: drills, weekly reading, weak spots

namespace {
std::string lowerWord(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// "YYYY-MM-DD HH:MM" for a time in seconds since 1970 (whenText reads it)
std::string epochDate(long long t) {
    if (t <= 0) return "";
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}
}  // namespace

void GuiApp::recordCard(const Flashcard& c, bool knewIt) {
    if (track_ != "afoqt") return;
    if (repRoots_) misses_.record("root", c.word, !knewIt);
    else misses_.record("word", lowerWord(c.word), !knewIt);
    misses_.save();
}

// Counts every answered question by what it tests: the word (Word Knowledge), the
// relationship (Verbal Analogies), or the question type (Reading Comprehension).
// Questions left blank when time ran out don't count; a skip does.
void GuiApp::recordMisses() {
    const SessionResult& r = engine_.result();
    const auto& qs = engine_.questions();
    bool any = false;
    for (size_t i = 0; i < r.answers.size() && i < qs.size(); ++i) {
        if (r.answers[i].chosenIndex < 0 && r.timeExpired) continue;
        const Question& q = qs[i];
        const bool missed = !r.answers[i].correct;
        if (q.subtest == "verbal_analogies" && !q.type.empty()) misses_.record("relation", q.type, missed);
        else if (q.subtest == "reading_comprehension" && !q.type.empty()) misses_.record("reading", q.type, missed);
        else if (q.subtest == "word_knowledge") misses_.record("word", verbal::wordKnowledgeWord(q.prompt), missed);
        else continue;
        any = true;
    }
    if (any) misses_.save();
}

void GuiApp::startCardDrill(const std::vector<std::string>& words, bool roots) {
    goTo(Screen::Flashcards);  // first: arriving on the page resets it
    repRoots_ = roots;
    activeDeck().startSession(words, rng_);
    swipe_ = SwipeCard{};
    repSession_ = repCounted_ = false;
    flashActive_ = !activeDeck().sessionDone();
}

void GuiApp::startRelationDrill(const std::string& type) {
    std::vector<Question> pool;
    for (const Question& q : bank_.all("verbal_analogies"))
        if (type.empty() || q.type == type) pool.push_back(q);
    if (pool.empty()) {
        message_ = "No analogy questions of that kind.";
        return;
    }
    std::shuffle(pool.begin(), pool.end(), rng_);
    if (pool.size() > 20) pool.resize(20);
    for (auto& q : pool) QuestionBank::shuffleChoices(q, rng_);
    const SubtestInfo* va = registry_.find("verbal_analogies");
    const verbal::TypeInfo* t = verbal::findType("verbal_analogies", type);
    message_.clear();
    sessionName_ = t ? std::string("Drill: ") + t->name : "Relationship drill";
    sessionTag_ = "RELATION DRILL";
    sessionTips_ = "Say the relationship as a sentence (\"a pilot commands an aircraft\"), then find the pair that fits "
                   "the same sentence.";
    sessionTimeLimit_ = 0;
    endless_ = false;
    isReview_ = false;
    weeklySession_ = false;
    relDrill_ = true;
    relStepFor_ = relChoicesFor_ = -1;
    relPick_ = -1;
    relNamed_ = relNamedRight_ = 0;
    feedbackTime_ = -1.0;
    cardMode_ = false;
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
    engine_.start(va ? va->id : "verbal_analogies", std::move(pool), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

// Step 1 of the drill: the first pair alone, and six relationships to pick from.
void GuiApp::drawRelationStep(const Question& q) {
    const auto& types = verbal::relationTypes();
    int correct = -1;
    for (size_t i = 0; i < types.size(); ++i)
        if (q.type == types[i].id) correct = static_cast<int>(i);
    if (relChoicesFor_ != engine_.index()) {  // new question: the right type and five others
        relChoices_.clear();
        std::vector<int> others;
        for (int i = 0; i < static_cast<int>(types.size()); ++i)
            if (i != correct) others.push_back(i);
        std::shuffle(others.begin(), others.end(), rng_);
        if (correct >= 0) relChoices_.push_back(correct);
        for (int i = 0; i < 5 && i < static_cast<int>(others.size()); ++i) relChoices_.push_back(others[static_cast<size_t>(i)]);
        std::shuffle(relChoices_.begin(), relChoices_.end(), rng_);
        relChoicesFor_ = engine_.index();
        relPick_ = -1;
        feedbackTime_ = -1.0;
    }
    const auto pair = verbal::firstPair(q.prompt);
    const bool answered = relPick_ >= 0;
    const float anim = answered && feedbackTime_ >= 0 ? static_cast<float>(ImGui::GetTime() - feedbackTime_) : -1.0f;

    beginAutoCard("relstep", ImVec2(32, 30));
    textIn(fonts.small, col::Muted, "Step 1 of 2  \xC2\xB7  Name the relationship");
    ImGui::Dummy(ImVec2(0, 4));
    wrappedIn(fonts.title, col::Accent, (pair.first + "  :  " + pair.second).c_str());
    ImGui::Dummy(ImVec2(0, 2));
    wrappedIn(fonts.prompt, col::Ink, "How are these two words related?");
    ImGui::Dummy(ImVec2(0, 6));

    int clicked = -1;
    const float aw = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 10));
    for (int i = 0; i < static_cast<int>(relChoices_.size()); ++i) {
        const int t = relChoices_[static_cast<size_t>(i)];
        AnswerState st = AnswerState::Normal;
        if (answered) st = t == correct ? AnswerState::Correct : i == relPick_ ? AnswerState::Wrong : AnswerState::Dimmed;
        if (st == AnswerState::Wrong && anim >= 0 && anim < 0.4f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::sin(anim * 48.0f) * 7.0f * (1.0f - anim / 0.4f));
        ImGui::PushID(i);
        const bool animated = st == AnswerState::Correct || st == AnswerState::Wrong;
        if (answerCard("rel", static_cast<char>('A' + i), types[static_cast<size_t>(t)].name, st, aw, animated ? anim : -1.0f))
            clicked = i;
        ImGui::PopID();
        if (!answered && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + i), false)) clicked = i;
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 4));

    bool next = false;
    if (answered && correct >= 0) {
        const bool right = relChoices_[static_cast<size_t>(relPick_)] == correct;
        beginCallout();
        textIn(fonts.heading, right ? col::Good : col::Bad, right ? "Correct" : "Not quite");
        textIn(fonts.strong, col::Ink, types[static_cast<size_t>(correct)].name);
        wrappedIn(fonts.body, col::InkSoft, types[static_cast<size_t>(correct)].hint);
        if (!right) {
            const auto& picked = types[static_cast<size_t>(relChoices_[static_cast<size_t>(relPick_)])];
            ImGui::Dummy(ImVec2(0, 2));
            wrappedIn(fonts.small, col::Muted, fmt("You picked %s: %s", picked.name, picked.hint).c_str());
        }
        endCallout(right ? col::GoodSoft : col::BadSoft, right ? col::Good : col::Bad);
        next = button(touch_ ? "Answer the analogy  \xE2\x86\x92" : "Answer the analogy  \xE2\x86\x92   Enter", ButtonStyle::Primary,
                      ImVec2(compact_ ? ImGui::GetContentRegionAvail().x : 0, compact_ ? 48 : 0)) ||
               ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    }
    ImGui::Dummy(ImVec2(0, 4));
    divider();
    wrappedIn(fonts.small, col::Muted, ("Tip: " + sessionTips_ +
                                        (touch_ ? "" : "   Keys: A\xE2\x80\x93" "F to answer, Enter for next.")).c_str());
    endAutoCard();

    if (!answered && clicked >= 0) {
        relPick_ = clicked;
        feedbackTime_ = ImGui::GetTime();
        const bool right = relChoices_[static_cast<size_t>(clicked)] == correct;
        ++relNamed_;
        if (right) ++relNamedRight_;
        if (right) sound_.correct();
        else sound_.wrong();
        misses_.record("relname", q.type, !right);
        misses_.save();
    } else if (next) {
        relStepFor_ = engine_.index();
        relPick_ = -1;
        feedbackTime_ = -1.0;
    }
}

void GuiApp::startReadingType(const std::string& type) {
    std::vector<Question> pool;
    for (const Question& q : bank_.all("reading_comprehension"))
        if (q.type == type) pool.push_back(q);
    const verbal::TypeInfo* t = verbal::findType("reading_comprehension", type);
    if (pool.empty() || !t) return;
    std::shuffle(pool.begin(), pool.end(), rng_);
    for (auto& q : pool) QuestionBank::shuffleChoices(q, rng_);
    message_.clear();
    sessionName_ = std::string("Reading: ") + t->name;
    sessionTag_ = "PRACTICE";
    sessionTips_ = t->hint;
    sessionTimeLimit_ = 0;
    endless_ = false;
    isReview_ = false;
    relDrill_ = weeklySession_ = false;
    feedbackTime_ = -1.0;
    cardMode_ = false;
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
    engine_.start("reading_comprehension", std::move(pool), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

std::vector<Question> GuiApp::weeklyQuestions() const {
    const auto& all = bank_.all("reading_comprehension");
    std::vector<std::string> passages;  // in file order, once each
    for (const Question& q : all)
        if (!q.passage.empty() && std::find(passages.begin(), passages.end(), q.passage) == passages.end())
            passages.push_back(q.passage);
    std::vector<Question> out;
    for (size_t p : WeeklyReading::passagesFor(WeeklyReading::weekNumber(), passages.size()))
        for (const Question& q : all)
            if (q.passage == passages[p]) out.push_back(q);
    return out;
}

void GuiApp::startWeeklyReading() {
    auto questions = weeklyQuestions();
    const SubtestInfo* rc = registry_.find("reading_comprehension");
    if (questions.empty() || !rc) return;
    for (auto& q : questions) QuestionBank::shuffleChoices(q, rng_);  // passages stay together, in order
    // The same pace as the timed Reading Comprehension test
    const int count = rc->questionCount > 0 ? rc->questionCount : static_cast<int>(questions.size());
    const int seconds = rc->timeLimitSeconds * static_cast<int>(questions.size()) / count;
    message_.clear();
    sessionName_ = "This week's reading";
    sessionTag_ = "TIMED TEST";
    sessionTips_ = rc->tips;
    sessionTimeLimit_ = seconds;
    endless_ = false;
    isReview_ = false;
    relDrill_ = false;
    weeklySession_ = true;
    feedbackTime_ = -1.0;
    cardMode_ = false;
    swipe_ = SwipeCard{};
    highlights_.clear();  // highlights last only while a session is open
    hlDrag_.active = false;
    engine_.start(rc->id, std::move(questions), SessionMode::Timed, seconds);
    screen_ = Screen::Quiz;
}

void GuiApp::drawWeeklyStrip() {
    const SubtestInfo* rc = registry_.find("reading_comprehension");
    const int n = static_cast<int>(weeklyQuestions().size());
    if (!rc || n == 0) return;
    const int count = rc->questionCount > 0 ? rc->questionCount : n;
    const int seconds = rc->timeLimitSeconds * n / count;
    const WeeklyReading::Result* res = weekly_.thisWeekResult();
    const int streak = weekly_.streak();
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::string monday = WeeklyReading::thisWeek();
    TableRow r;
    r.name = fmt("Week of %s %d", months[std::clamp(std::atoi(monday.c_str() + 5) - 1, 0, 11)], std::atoi(monday.c_str() + 8));
    r.tag = res ? "" : "THIS WEEK";
    r.note = fmt("%d passages, timed like the real test. A new set every Monday.", WeeklyReading::kPassages);
    r.cells = {{fmt("%d in %s", n, mmss(seconds).c_str()), fmt("%d questions in %s", n, mmss(seconds).c_str())},
               {res ? fmt("Done %d/%d", res->correct, res->total) : "Not yet",
                res ? fmt("Done this week, %d of %d right", res->correct, res->total) : "Not done this week",
                res ? col::Ink : streak > 0 ? col::TagStrong : col::Ink},
               {streak ? plural(streak, "week") : "\xE2\x80\x94", streak ? plural(streak, "week") + " in a row" : ""}};
    r.buttons = {res ? "Retake" : "Start"};
    // Room for three buttons, like the section tables under it, so the columns line up
    float actionsW = 0.0f;
    for (const char* l : {"Practice", "Timed", "Endless"}) actionsW += textSize(fonts.strong, l).x + 26.0f + 8.0f;
    const auto hit = drawTable("weekly", "WEEKLY READING", {"SET", "THIS WEEK", "STREAK"}, {r}, actionsW);
    if (hit.first == 0) startWeeklyReading();
}

void GuiApp::drawWeakSpots() {
    const char* intro = "What you miss most, counted from every quiz, drill and card. Drill a list to work on it.";
    if (!compact_) pageTitle("Weak spots", intro);
    else {
        wrappedIn(fonts.small, col::Muted, intro);
        ImGui::Dummy(ImVec2(0, 2));
    }
    if (!message_.empty()) {
        beginCallout();
        textIn(fonts.strong, col::Ink, message_.c_str());
        endCallout(col::AccentSoft, col::Accent);
    }
    const char* dash = "\xE2\x80\x94";
    auto pct = [](int right, int seen) { return seen ? fmt("%d%%", right * 100 / seen) : std::string("\xE2\x80\x94"); };

    // ---- Words and roots: the ten missed most (ties: the higher miss rate first)
    auto missedList = [&](const char* kind) {
        auto all = misses_.counts(kind);
        all.erase(std::remove_if(all.begin(), all.end(), [](const MissLog::Count& c) { return c.missed == 0; }), all.end());
        std::sort(all.begin(), all.end(), [](const MissLog::Count& a, const MissLog::Count& b) {
            if (a.missed != b.missed) return a.missed > b.missed;
            if (a.missRate() != b.missRate()) return a.missRate() > b.missRate();
            return a.name < b.name;
        });
        if (all.size() > 10) all.resize(10);
        return all;
    };
    auto cardList = [&](const char* kind, const FlashcardDeck& cards, const char* label, const char* unit,
                        const char* empty, bool roots) {
        const auto list = missedList(kind);
        std::vector<TableRow> rows;
        std::vector<std::string> names;
        for (const auto& c : list) {
            const Flashcard* card = nullptr;
            for (const Flashcard& f : cards.cards())
                if (lowerWord(f.word) == lowerWord(c.name)) card = &f;
            if (!card) continue;  // no longer in the file
            TableRow r;
            r.name = card->word;
            r.note = card->definition;
            r.cells = {{fmt("%d", c.missed), c.missed == 1 ? std::string("1 miss") : fmt("%d misses", c.missed), col::TagStrong},
                       {fmt("%d of %d", c.missed, c.seen), fmt("missed %d of %d times", c.missed, c.seen)},
                       {whenText(epochDate(c.lastMiss)), "last missed " + whenText(epochDate(c.lastMiss))}};
            rows.push_back(r);
            names.push_back(card->word);
        }
        const float ty = ImGui::GetCursorPosY();
        textIn(fonts.heading, col::Ink, label);
        if (!names.empty()) {
            const std::string drill = fmt("Drill these %s", plural(static_cast<int>(names.size()), unit).c_str());
            if (!compact_) {
                alignRight(buttonWidth(drill.c_str()));
                ImGui::SetCursorPosY(ty - 4);
            }
            if (button(drill.c_str(), ButtonStyle::Outline, ImVec2(compact_ ? ImGui::GetContentRegionAvail().x : 0, 0))) {
                startCardDrill(names, roots);
                return true;
            }
        }
        ImGui::Dummy(ImVec2(0, 2));
        if (rows.empty()) wrappedIn(fonts.body, col::Muted, empty);
        else drawTable(kind, roots ? "ROOT" : "WORD", {"MISSED", "RATE", "LAST"}, rows);
        ImGui::Dummy(ImVec2(0, 14));
        return false;
    };
    if (cardList("word", deck_, "Most-missed words", "word",
                 "No missed words yet. Word Knowledge questions and vocabulary cards count here.", false))
        return;
    if (cardList("root", rootDeck_, "Most-missed roots", "root",
                 "No missed roots yet. Cards from the word roots deck count here.", true))
        return;

    // ---- Analogy relationships: naming it (drill step 1) and answering the analogy
    {
        const auto& types = verbal::relationTypes();
        struct Row { size_t i; int missed; };
        std::vector<Row> order;
        for (size_t i = 0; i < types.size(); ++i) {
            const auto named = misses_.count("relname", types[i].id), answered = misses_.count("relation", types[i].id);
            order.push_back({i, named.missed + answered.missed});
        }
        std::stable_sort(order.begin(), order.end(), [](const Row& a, const Row& b) { return a.missed > b.missed; });
        std::vector<TableRow> rows;
        for (const Row& o : order) {
            const auto& t = types[o.i];
            const auto named = misses_.count("relname", t.id), answered = misses_.count("relation", t.id);
            TableRow r;
            r.name = t.name;
            r.note = t.hint;
            r.cells = {{pct(named.seen - named.missed, named.seen),
                        named.seen ? fmt("named %s", pct(named.seen - named.missed, named.seen).c_str()) : ""},
                       {pct(answered.seen - answered.missed, answered.seen),
                        answered.seen ? fmt("answered %s", pct(answered.seen - answered.missed, answered.seen).c_str()) : ""},
                       {o.missed ? fmt("%d", o.missed) : dash,
                        o.missed ? (o.missed == 1 ? std::string("1 miss") : fmt("%d misses", o.missed)) : "Not missed yet",
                        o.missed ? col::TagStrong : col::Muted}};
            r.buttons = {"Drill"};
            rows.push_back(r);
        }
        const float ty = ImGui::GetCursorPosY();
        textIn(fonts.heading, col::Ink, "Analogy relationships");
        const char* all = "Drill every type";
        if (!compact_) {
            alignRight(buttonWidth(all));
            ImGui::SetCursorPosY(ty - 4);
        }
        if (button(all, ButtonStyle::Outline, ImVec2(compact_ ? ImGui::GetContentRegionAvail().x : 0, 0))) {
            startRelationDrill("");
            return;
        }
        wrappedIn(fonts.small, col::Muted, "Named: picking the relationship in step 1 of a drill. Answered: Verbal Analogies "
                                           "questions of that type, anywhere in the app.");
        ImGui::Dummy(ImVec2(0, 4));
        const auto hit = drawTable("weak_rel", "RELATIONSHIP", {"NAMED", "ANSWERED", "MISSED"}, rows);
        if (hit.first >= 0) {
            startRelationDrill(types[order[static_cast<size_t>(hit.first)].i].id);
            return;
        }
        ImGui::Dummy(ImVec2(0, 14));
    }

    // ---- Reading question types
    {
        const auto& types = verbal::readingTypes();
        std::vector<TableRow> rows;
        for (const auto& t : types) {
            const auto c = misses_.count("reading", t.id);
            int n = 0;
            for (const Question& q : bank_.all("reading_comprehension")) n += q.type == t.id ? 1 : 0;
            TableRow r;
            r.name = t.name;
            r.note = t.hint;
            r.cells = {{pct(c.seen - c.missed, c.seen), c.seen ? pct(c.seen - c.missed, c.seen) + " right" : "Not tried yet"},
                       {c.missed ? fmt("%d", c.missed) : dash, c.missed ? (c.missed == 1 ? std::string("1 miss") : fmt("%d misses", c.missed)) : "",
                        c.missed ? col::TagStrong : col::Muted},
                       {fmt("%d", n), plural(n, "question")}};
            r.buttons = {"Practice"};
            rows.push_back(r);
        }
        textIn(fonts.heading, col::Ink, "Reading question types");
        ImGui::Dummy(ImVec2(0, 2));
        const auto hit = drawTable("weak_read", "QUESTION TYPE", {"RIGHT", "MISSED", "IN BANK"}, rows);
        if (hit.first >= 0) startReadingType(types[static_cast<size_t>(hit.first)].id);
    }
}

// ================================================================ focus area

void GuiApp::focusMenuItems() {
    spacedLabel("CHOOSE YOUR FOCUS AREA", col::Muted, 1.6f);
    ImGui::Dummy(ImVec2(0, 2));
    const ImVec2 row(std::min(310.0f, ImGui::GetMainViewport()->WorkSize.x - 60), touch_ ? 44.0f : 34.0f);
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
    if (in) try {  // no file: first run
        nlohmann::json j;
        in >> j;
        pinnedByTrack_["afoqt"] = j.value("focus", "");  // older files only had the AFOQT focus
        if (j.contains("focus_by_track"))
            for (auto& [t, id] : j["focus_by_track"].items()) pinnedByTrack_[t] = id.get<std::string>();
        sound_.enabled = j.value("sound", true);
        darkMode_ = j.value("dark", false);
        definitionFirst_ = j.value("definition_first", false);
        track_ = j.value("track", "afoqt");
        deviceId_ = j.value("device", "");
        passageZoom_ = std::clamp(j.value("passage_zoom", 1), 0, 3);
        if (track_ != trackInfo(track_).id) track_ = "afoqt";  // unknown track
    } catch (...) {
        pinnedByTrack_.clear();  // damaged file: set a copy aside before it is saved over, use defaults
        track_ = "afoqt";
        in.close();
        atomicfile::setAside(apppaths::userFile("afoqt_settings.json"));
    }
    pinnedFocus_ = pinnedByTrack_[track_];
    if (deviceId_.empty()) {  // first run (or an older settings file): this device's id for the miss counts
        deviceId_ = cloudsync::newId();
        saveSettings();
    }
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
    j["device"] = deviceId_;
    j["passage_zoom"] = passageZoom_;
    atomicfile::write(apppaths::userFile("afoqt_settings.json"), j.dump(2));
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
