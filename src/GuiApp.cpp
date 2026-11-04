#define IMGUI_DEFINE_MATH_OPERATORS  // lets us write ImVec2 + ImVec2
#include "GuiApp.h"
#include "Widgets.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace ui;

namespace {

constexpr float kTopBar = 68.0f;   // height of the top bar
constexpr float kRail = 76.0f;     // width of the icon rail on the left
constexpr float kSide = 340.0f;    // width of the side panel on the right

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

// "Word Knowledge" -> "WK"
std::string initials(const std::string& name) {
    std::string out;
    bool start = true;
    for (char c : name) {
        if (start && std::isalpha(static_cast<unsigned char>(c)))
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        start = c == ' ';
    }
    return out.substr(0, 3);
}

// Puts the next item flush with the right edge of the current window.
void alignRight(float itemWidth) {
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - itemWidth);
}

// A screen title with an optional pill button beside it. Returns true when clicked.
bool titleRow(const char* title, const char* button = nullptr, bool filled = true,
              bool buttonOnRight = false) {
    const float y = ImGui::GetCursorPosY();
    textIn(fonts.title, col::Text, title);
    bool clicked = false;
    if (button) {
        const float titleH = ImGui::GetItemRectSize().y;
        const float pillW = textSize(fonts.body, button).x + 40;
        const float pillH = textSize(fonts.body, button).y + 20;
        if (buttonOnRight) alignRight(pillW);
        else ImGui::SameLine(0, 28);
        ImGui::SetCursorPosY(y + (titleH - pillH) * 0.5f);
        clicked = pill(button, filled, ImVec2(0, 0), filled ? col::Accent : col::Border);
        ImGui::SetCursorPosY(y + titleH + ImGui::GetStyle().ItemSpacing.y);
    }
    ImGui::Dummy(ImVec2(0, 10));
    return clicked;
}

// "LABEL ............. VALUE" on one line, value in a color.
void labelWithValue(const char* label, const std::string& value, ImVec4 color = col::Accent) {
    sectionLabel(label);
    alignRight(textSize(fonts.label, value.c_str()).x);
    textIn(fonts.label, color, value.c_str());
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
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    const ImVec2 o = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Frame: side panel background and the two separator lines
    dl->AddRectFilled(ImVec2(o.x + sz.x - kSide, o.y + kTopBar), o + sz, u32(col::SidePanel));
    dl->AddLine(ImVec2(o.x, o.y + kTopBar), ImVec2(o.x + sz.x, o.y + kTopBar),
                u32(col::Border, 0.8f), 1.0f);
    dl->AddLine(ImVec2(o.x + kRail, o.y), ImVec2(o.x + kRail, o.y + sz.y), u32(col::Border, 0.8f),
                1.0f);

    drawTopBar();
    drawRail();

    // Center
    ImGui::SetCursorScreenPos(ImVec2(o.x + kRail, o.y + kTopBar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40, 30));
    ImGui::BeginChild("center", ImVec2(sz.x - kRail - kSide, sz.y - kTopBar),
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    switch (screen_) {
        case Screen::Dashboard: drawDashboard(); break;
        case Screen::Quiz: drawQuiz(); break;
        case Screen::Results: drawResults(); break;
        case Screen::Flashcards: drawFlashcards(); break;
        case Screen::Progress: drawProgress(); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Side panel
    ImGui::SetCursorScreenPos(ImVec2(o.x + sz.x - kSide, o.y + kTopBar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32, 30));
    ImGui::BeginChild("side", ImVec2(kSide, sz.y - kTopBar), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (screen_) {
        case Screen::Quiz: drawQuizSide(); break;
        case Screen::Flashcards: drawFlashcardsSide(); break;
        default: drawOverviewSide(); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
}

// ================================================================ frame

void GuiApp::drawTopBar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();
    const float midY = o.y + kTopBar * 0.5f;

    // Logo: "AS" in cyan, drawn twice 1px apart for a bolder look
    const float fs = fonts.title->FontSize * ImGui::GetIO().FontGlobalScale;
    ImVec2 ls = textSize(fonts.title, "AS");
    ImVec2 lp(o.x + (kRail - ls.x) * 0.5f, midY - fs * 0.5f);
    dl->AddText(fonts.title, fs, lp, u32(col::Accent), "AS");
    dl->AddText(fonts.title, fs, lp + ImVec2(0.8f, 0), u32(col::Accent), "AS");

    // Breadcrumb
    const char* where = "Dashboard";
    switch (screen_) {
        case Screen::Quiz: where = sessionName_.c_str(); break;
        case Screen::Results: where = "Results"; break;
        case Screen::Flashcards: where = "Flashcards"; break;
        case Screen::Progress: where = "Progress"; break;
        default: break;
    }
    const float bh = fonts.body->FontSize * ImGui::GetIO().FontGlobalScale;
    std::string crumb = std::string("AFOQT Study   /   ") + where;
    dl->AddText(fonts.body, bh, ImVec2(o.x + kRail + 40, midY - bh * 0.5f), u32(col::Muted),
                crumb.c_str());

    // Right side: quick stats and an avatar circle with the user's initial
    const Overall ov = overall();
    std::string stats = ov.total
        ? fmt("%d%% accuracy   \xC2\xB7   %d / %d words mastered", ov.correct * 100 / ov.total,
              deck_.masteredCount(), static_cast<int>(deck_.size()))
        : fmt("%d / %d words mastered", deck_.masteredCount(), static_cast<int>(deck_.size()));
    ImVec2 ss = textSize(fonts.small, stats.c_str());
    const float avatarX = o.x + sz.x - 46;
    const float sh = fonts.small->FontSize * ImGui::GetIO().FontGlobalScale;
    dl->AddText(fonts.small, sh, ImVec2(avatarX - 40 - ss.x, midY - sh * 0.5f), u32(col::Muted),
                stats.c_str());

    const char* user = std::getenv("USER");
    char initial[2] = {user && *user ? static_cast<char>(std::toupper(static_cast<unsigned char>(*user))) : 'A', 0};
    dl->AddCircleFilled(ImVec2(avatarX, midY), 20, u32(col::Panel));
    dl->AddCircle(ImVec2(avatarX, midY), 20, u32(col::Accent), 0, 2.0f);
    ImVec2 is = textSize(fonts.heading, initial);
    const float hh = fonts.heading->FontSize * ImGui::GetIO().FontGlobalScale;
    dl->AddText(fonts.heading, hh, ImVec2(avatarX - is.x * 0.5f, midY - hh * 0.5f),
                u32(col::Text), initial);
}

void GuiApp::drawRail() {
    struct Item { Icon icon; Screen screen; const char* tip; };
    const Item items[] = {
        {Icon::Grid, Screen::Dashboard, "Dashboard"},
        {Icon::Cards, Screen::Flashcards, "Flashcards"},
        {Icon::Chart, Screen::Progress, "Progress"},
    };
    const ImVec2 o = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Quizzes and results belong to the Dashboard section
    Screen section = screen_;
    if (section == Screen::Quiz || section == Screen::Results) section = Screen::Dashboard;

    for (int i = 0; i < 3; ++i) {
        const ImVec2 p(o.x + (kRail - 46) * 0.5f, o.y + kTopBar + 28 + i * 64.0f);
        const ImVec2 size(46, 46);
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        bool clicked = ImGui::InvisibleButton("rail", size);
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", items[i].tip);
        }
        const bool selected = section == items[i].screen;
        if (selected) dl->AddRectFilled(p, p + size, u32(col::Accent), 10.0f);
        else if (hov) dl->AddRectFilled(p, p + size, u32(col::CardHover), 10.0f);
        drawIcon(dl, items[i].icon, p + size * 0.5f, 22.0f,
                 u32(selected ? col::OnAccent : hov ? col::Text : col::Muted));
        if (clicked) goTo(items[i].screen);
    }
}

// ================================================================ dashboard

void GuiApp::drawDashboard() {
    if (titleRow("Study dashboard", "+  Full battery")) {
        startBattery();
        return;
    }
    if (!message_.empty()) {
        beginCallout();
        textIn(fonts.body, col::Text, message_.c_str());
        endCallout(col::Accent);
    }

    const float h = ImGui::GetContentRegionAvail().y;
    const auto missed = missedBySubtest();
    int missedTotal = 0;
    for (const auto& [id, n] : missed) missedTotal += n;

    // ---- Left panel: study mode
    beginPanel("modes", ImVec2(270, h), true);
    sectionLabel("STUDY MODE");
    ImGui::Dummy(ImVec2(0, 4));
    const float w = ImGui::GetContentRegionAvail().x;
    if (navItem("m_practice", "Practice", "Untimed, instant feedback", Icon::Target,
                mode_ == Mode::Practice, w)) { mode_ = Mode::Practice; message_.clear(); }
    if (navItem("m_timed", "Timed test", "Real test pacing", Icon::Clock, mode_ == Mode::Timed, w)) {
        mode_ = Mode::Timed;
        message_.clear();
    }
    std::string rsub = missedTotal ? fmt("%d questions to redo", missedTotal) : "Nothing to review";
    if (navItem("m_review", "Review missed", rsub.c_str(), Icon::Review, mode_ == Mode::Review, w)) {
        mode_ = Mode::Review;
        message_.clear();
    }
    if (navItem("m_flash", "Flashcards", "Vocabulary deck", Icon::Cards, false, w))
        goTo(Screen::Flashcards);
    endPanel();

    ImGui::SameLine(0, 24);

    // ---- Right panel: one card per subtest
    beginPanel("subtests", ImVec2(0, h), false);
    const char* label = mode_ == Mode::Practice ? "PRACTICE  \xC2\xB7  CHOOSE A SUBTEST"
                      : mode_ == Mode::Timed    ? "TIMED TEST  \xC2\xB7  CHOOSE A SUBTEST"
                                                : "REVIEW  \xC2\xB7  MISSED QUESTIONS BY SECTION";
    sectionLabel(label);
    ImGui::Dummy(ImVec2(0, 4));

    const auto stats = progress_.stats();
    const std::string focus = focusId();
    const auto& subs = registry_.all();
    const int count = static_cast<int>(subs.size()) + 1;  // + one extra card at the end
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int cols = avail.x > 700 ? 3 : 2;
    const int rows = (count + cols - 1) / cols;
    const float gap = 16.0f;
    const float cw = (avail.x - gap * (cols - 1)) / cols;
    const float ch = std::clamp((avail.y - gap * (rows - 1)) / rows, 140.0f, 210.0f);

    for (int i = 0; i < count; ++i) {
        if (i % cols != 0) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        const bool extra = i == count - 1;

        if (!extra) {
            const SubtestInfo& s = subs[i];
            auto it = stats.find(s.id);
            std::string ab = initials(s.name), sub, status;
            bool highlight = false, disabled = false;
            const char* tag = nullptr;

            if (mode_ == Mode::Review) {
                int n = missed.count(s.id) ? missed.at(s.id) : 0;
                sub = n ? fmt("%d missed", n) : "All clear";
                status = n ? "Redo these questions" : "";
                highlight = n > 0;
                disabled = n == 0;
            } else {
                sub = mode_ == Mode::Timed
                    ? fmt("%d questions \xC2\xB7 %d min", s.questionCount, s.timeLimitSeconds / 60)
                    : fmt("%zu questions", bank_.size(s.id));
                status = it != stats.end() ? fmt("%.0f%% accuracy", it->second.accuracy())
                                           : "Not started";
                highlight = s.id == focus;
                if (highlight) tag = "FOCUS";
            }
            if (card("card", ImVec2(cw, ch), ab.c_str(), Icon::Grid, s.name.c_str(), sub.c_str(),
                     status.c_str(), highlight, disabled, tag)) {
                if (mode_ == Mode::Review) startReview(s.id);
                else startSubtest(s, mode_ == Mode::Timed ? SessionMode::Timed : SessionMode::Practice);
            }
            if (mode_ != Mode::Review) cardFocusMenu(s);  // right-click the card
        } else if (mode_ == Mode::Practice) {
            std::string sub = fmt("%d words", static_cast<int>(deck_.size()));
            std::string status = fmt("%d mastered", deck_.masteredCount());
            if (card("card", ImVec2(cw, ch), nullptr, Icon::Cards, "Flashcards", sub.c_str(),
                     status.c_str(), false))
                goTo(Screen::Flashcards);
        } else if (mode_ == Mode::Timed) {
            int secs = 0;
            for (const auto& s : registry_.inCategory("knowledge")) secs += s.timeLimitSeconds;
            std::string status = fmt("About %d min", secs / 60);
            if (card("card", ImVec2(cw, ch), nullptr, Icon::Battery, "Full battery",
                     "All sections, back to back", status.c_str(), false))
                startBattery();
        } else {
            std::string sub = missedTotal ? fmt("%d missed", missedTotal) : "All clear";
            if (card("card", ImVec2(cw, ch), nullptr, Icon::Review, "All sections", sub.c_str(),
                     missedTotal ? "Mixed review" : "", false, missedTotal == 0))
                startReview("");
        }
        ImGui::PopID();
    }
    endPanel();
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

    if (titleRow(sessionName_.c_str(), "Quit", false, true)) {
        queue_.clear();
        goTo(Screen::Dashboard);
        return;
    }

    beginPanel("question", ImVec2(0, 0), false, true);
    sectionLabel(fmt("%s  \xC2\xB7  QUESTION %d OF %d", sessionTag_.c_str(), engine_.index() + 1,
                     engine_.total()).c_str());
    ImGui::Dummy(ImVec2(0, 2));

    if (!q.passage.empty()) {
        float wrap = ImGui::GetContentRegionAvail().x - 52;
        float ph = std::min(240.0f, textSize(fonts.body, q.passage.c_str(), wrap).y + 50);
        beginPanel("passage", ImVec2(0, ph), true, true);
        wrappedIn(fonts.body, col::Text, q.passage.c_str());
        endPanel();
        ImGui::Dummy(ImVec2(0, 4));
    }

    wrappedIn(fonts.heading, col::Text, q.prompt.c_str());
    ImGui::Dummy(ImVec2(0, 6));

    int clicked = -2;  // -2 = nothing, -1 = skip, 0+ = choice
    const float width = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(q.choices.size()); ++i) {
        AnswerState st = AnswerState::Normal;
        if (feedback) {
            if (i == q.correctIndex) st = AnswerState::Correct;
            else if (i == engine_.lastChoice()) st = AnswerState::Wrong;
            else st = AnswerState::Dimmed;
        }
        ImGui::PushID(i);
        if (answerCard("ans", static_cast<char>('A' + i), q.choices[i].c_str(), st, width))
            clicked = i;
        ImGui::PopID();
        if (!feedback && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + i), false))
            clicked = i;
    }
    ImGui::Dummy(ImVec2(0, 4));

    if (!feedback) {
        if (pill("Skip   S", false, ImVec2(0, 0), col::Border) || ImGui::IsKeyPressed(ImGuiKey_S, false))
            clicked = -1;
    } else {
        const int chosen = engine_.lastChoice();
        const bool right = chosen == q.correctIndex;
        beginCallout();
        textIn(fonts.heading, right ? col::Good : col::Bad,
               right ? "Correct" : chosen < 0 ? "Skipped" : "Not quite");
        wrappedIn(fonts.body, col::Text, q.explanation.c_str());
        endCallout(right ? col::Good : col::Bad);
        if (pill("Next   Enter") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
            engine_.next();
    }
    endPanel();

    if (clicked != -2) engine_.submit(clicked);
}

void GuiApp::drawQuizSide() {
    if (engine_.finished()) return;
    const float w = ImGui::GetContentRegionAvail().x;

    textIn(fonts.heading, col::Text, "Session");
    textIn(fonts.small, col::Muted, sessionTag_ == "TIMED TEST" ? "Timed test" :
                                    sessionTag_ == "REVIEW" ? "Reviewing missed questions" :
                                                              "Practice");
    divider();

    const int left = engine_.secondsLeft();
    sectionLabel("TIME LEFT");
    if (left >= 0)
        statBox(ImVec2(w, 108), mmss(left).c_str(),
                fmt("of %s", mmss(sessionTimeLimit_).c_str()).c_str(),
                left <= 30 ? col::Bad : col::Text);
    else
        statBox(ImVec2(w, 108), "Untimed", "Take as long as you need", col::Text);
    divider();

    labelWithValue("PROGRESS", fmt("%d / %d", engine_.index() + 1, engine_.total()));
    thinBar(static_cast<float>(engine_.index()) / engine_.total(), w);
    divider();

    const SessionResult& r = engine_.result();
    if (sessionTag_ != "TIMED TEST") {
        sectionLabel("SCORE SO FAR");
        int correct = r.correctCount();
        int missedN = static_cast<int>(r.answers.size()) - correct;
        listRow("c", "Correct", fmt("%d", correct).c_str(), col::Good, false);
        listRow("m", "Missed", fmt("%d", missedN).c_str(), missedN ? col::Bad : col::Muted, false);
    } else {
        sectionLabel("SCORE");
        wrappedIn(fonts.small, col::Muted, "Shown at the end, like the real test.");
    }
    divider();

    if (!sessionTips_.empty()) {
        sectionLabel("TIP");
        wrappedIn(fonts.small, col::Muted, sessionTips_.c_str());
        divider();
    }
    sectionLabel("KEYBOARD");
    textIn(fonts.small, col::Muted, "A \xE2\x80\x93 E   choose an answer");
    textIn(fonts.small, col::Muted, "S   skip       Enter   next");
}

// ================================================================ results

void GuiApp::drawResults() {
    const SessionResult& r = engine_.result();
    const int total = static_cast<int>(r.answers.size());
    const int pct = total ? r.correctCount() * 100 / total : 0;

    titleRow(fmt("%s results", sessionName_.c_str()).c_str());

    const float gap = 16.0f;
    const float bw = (ImGui::GetContentRegionAvail().x - gap * 2) / 3;
    statBox(ImVec2(bw, 112), fmt("%d%%", pct).c_str(), "score",
            pct >= 80 ? col::Good : pct >= 60 ? col::Accent : col::Bad);
    ImGui::SameLine(0, gap);
    statBox(ImVec2(bw, 112), fmt("%d / %d", r.correctCount(), total).c_str(), "correct");
    ImGui::SameLine(0, gap);
    statBox(ImVec2(bw, 112), mmss(static_cast<int>(r.totalSeconds)).c_str(), "time used");
    ImGui::Dummy(ImVec2(0, 4));

    if (r.timeExpired) {
        beginCallout();
        textIn(fonts.body, col::Text, "Time ran out before you finished. Unanswered questions count as missed.");
        endCallout(col::Bad);
    }

    if (!queue_.empty()) {
        if (pill(fmt("Next section:  %s", queue_.front().name.c_str()).c_str())) {
            startNextInQueue();
            return;
        }
        ImGui::SameLine(0, 12);
    }
    if (pill("Back to dashboard", queue_.empty(), ImVec2(0, 0), col::Border)) {
        queue_.clear();
        goTo(Screen::Dashboard);
        return;
    }
    ImGui::Dummy(ImVec2(0, 8));

    beginPanel("missed", ImVec2(0, 0), false, true);
    sectionLabel("MISSED QUESTIONS");
    ImGui::Dummy(ImVec2(0, 2));
    const auto& qs = engine_.questions();
    bool any = false;
    for (size_t i = 0; i < r.answers.size(); ++i) {
        if (r.answers[i].correct) continue;
        any = true;
        const Question& q = qs[i];
        wrappedIn(fonts.body, col::Text, q.prompt.c_str());
        const int chosen = r.answers[i].chosenIndex;
        textIn(fonts.small, col::Bad,
               chosen >= 0 ? fmt("Your answer:  %s", q.choices[chosen].c_str()).c_str() : "Not answered");
        textIn(fonts.small, col::Good, fmt("Correct:  %s", q.choices[q.correctIndex].c_str()).c_str());
        wrappedIn(fonts.small, col::Muted, q.explanation.c_str());
        divider();
    }
    if (!any) textIn(fonts.heading, col::Good, "None. Perfect score!");
    endPanel();
}

// ================================================================ flashcards

void GuiApp::drawFlashcards() {
    // ---- Start screen
    if (!flashActive_) {
        titleRow("Word Knowledge flashcards");
        if (!message_.empty()) {
            beginCallout();
            textIn(fonts.body, col::Text, message_.c_str());
            endCallout(col::Accent);
        }
        beginPanel("start", ImVec2(0, 0), false);
        sectionLabel("START A SESSION");
        ImGui::Dummy(ImVec2(0, 4));
        const float gap = 16.0f;
        const float cw = (ImGui::GetContentRegionAvail().x - gap * 2) / 3;
        const int learning = static_cast<int>(deck_.size()) - deck_.masteredCount();
        std::string all = fmt("%d words", static_cast<int>(deck_.size()));
        std::string left = fmt("%d not mastered yet", learning);

        if (card("c20", ImVec2(cw, 190), nullptr, Icon::Cards, "20 words", "A quick session",
                 "Weakest words first", true))
            startFlashcards(20, false);
        ImGui::SameLine(0, gap);
        if (card("cl", ImVec2(cw, 190), nullptr, Icon::Target, "Still learning", left.c_str(),
                 "Up to 30 cards", false, learning == 0))
            startFlashcards(30, true);
        ImGui::SameLine(0, gap);
        if (card("ca", ImVec2(cw, 190), nullptr, Icon::Battery, "Whole deck", all.c_str(),
                 "Every word once", false))
            startFlashcards(0, false);

        ImGui::Dummy(ImVec2(0, 16));
        sectionLabel("HOW IT WORKS");
        wrappedIn(fonts.body, col::Muted,
                  "Read the word, say the meaning to yourself, then flip the card. Mark it "
                  "\"Got it\" only if you were right. Words you miss come back a few cards later "
                  "and show up more often in future sessions. A word counts as mastered after "
                  "3 correct answers in a row.");
        endPanel();
        return;
    }

    // ---- Session finished
    if (deck_.sessionDone()) {
        titleRow("Session complete");
        const float gap = 16.0f;
        const float bw = (ImGui::GetContentRegionAvail().x - gap * 2) / 3;
        statBox(ImVec2(bw, 112), fmt("%d", deck_.sessionGotIt()).c_str(), "knew it", col::Good);
        ImGui::SameLine(0, gap);
        statBox(ImVec2(bw, 112), fmt("%d", deck_.sessionMissed()).c_str(), "still learning", col::Bad);
        ImGui::SameLine(0, gap);
        statBox(ImVec2(bw, 112), fmt("%d", deck_.masteredCount()).c_str(),
                fmt("of %d mastered", static_cast<int>(deck_.size())).c_str(), col::Accent);
        ImGui::Dummy(ImVec2(0, 8));
        if (pill("Back to flashcards")) flashActive_ = false;
        return;
    }

    // ---- A card
    const Flashcard& c = deck_.current();
    if (titleRow("Flashcards", "End session", false, true)) {
        flashActive_ = false;
        return;
    }

    const float cardH = std::min(380.0f, ImGui::GetContentRegionAvail().y - 90);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(p - ImVec2(3, 3), p + ImVec2(w + 3, cardH + 3), u32(col::Accent, 0.18f), 15.0f, 0, 3.0f);
    beginPanel("flash", ImVec2(0, cardH), true, true);

    const bool showWord = !definitionFirst_ || cardFlipped_;
    const bool showMeaning = definitionFirst_ || cardFlipped_;
    if (showWord) {
        sectionLabel(fmt("WORD  \xC2\xB7  %s", c.partOfSpeech == "adj" ? "ADJECTIVE"
                                          : c.partOfSpeech == "verb" ? "VERB" : "NOUN").c_str());
        textIn(fonts.title, col::Accent, c.word.c_str());
        ImGui::Dummy(ImVec2(0, 4));
    }
    if (showMeaning) {
        if (showWord) divider();
        sectionLabel("DEFINITION");
        wrappedIn(fonts.heading, col::Text, c.definition.c_str());
        if (cardFlipped_) {
            ImGui::Dummy(ImVec2(0, 6));
            sectionLabel("SYNONYM");
            textIn(fonts.body, col::Good, c.synonym.c_str());
            if (!c.example.empty()) {
                ImGui::Dummy(ImVec2(0, 6));
                sectionLabel("EXAMPLE");
                wrappedIn(fonts.body, col::Muted, ("\"" + c.example + "\"").c_str());
            }
        }
    }
    if (!cardFlipped_) {
        ImGui::Dummy(ImVec2(0, 10));
        textIn(fonts.small, col::Muted,
               definitionFirst_ ? "Which word has this meaning?" : "What does this word mean?");
    }
    endPanel();
    ImGui::Dummy(ImVec2(0, 8));

    if (!cardFlipped_) {
        if (pill("Flip card    Space", true, ImVec2(240, 50)) ||
            ImGui::IsKeyPressed(ImGuiKey_Space, false))
            cardFlipped_ = true;
    } else {
        int answer = -1;
        if (pill("Still learning    1", false, ImVec2(240, 50), col::Bad)) answer = 0;
        ImGui::SameLine(0, 14);
        if (pill("Got it    2", true, ImVec2(240, 50))) answer = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) answer = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) answer = 1;
        if (answer >= 0) {
            deck_.mark(answer == 1);
            cardFlipped_ = false;
        }
    }
}

void GuiApp::drawFlashcardsSide() {
    const float w = ImGui::GetContentRegionAvail().x;
    const int mastered = deck_.masteredCount();
    const int total = static_cast<int>(deck_.size());

    textIn(fonts.heading, col::Text, "Vocabulary");
    textIn(fonts.small, col::Muted, "Word Knowledge flashcards");
    divider();

    sectionLabel("MASTERED");
    statBox(ImVec2(w, 108), fmt("%d", mastered).c_str(), fmt("of %d words", total).c_str(), col::Accent);
    ImGui::Dummy(ImVec2(0, 2));
    thinBar(total ? static_cast<float>(mastered) / total : 0.0f, w);
    divider();

    sectionLabel("OPTIONS");
    const float rowY = ImGui::GetCursorPosY();
    textIn(fonts.body, col::Text, "Definition first");
    alignRight(52);
    ImGui::SetCursorPosY(rowY - 4);
    toggle("deffirst", &definitionFirst_);
    textIn(fonts.small, col::Muted, "Guess the word from its meaning.");
    divider();

    if (flashActive_ && !deck_.sessionDone()) {
        sectionLabel("THIS SESSION");
        listRow("l", "Cards left", fmt("%d", static_cast<int>(deck_.remaining())).c_str(), col::Muted, false);
        listRow("k", "Knew it", fmt("%d", deck_.sessionGotIt()).c_str(), col::Good, false);
        listRow("s", "Still learning", fmt("%d", deck_.sessionMissed()).c_str(),
                deck_.sessionMissed() ? col::Bad : col::Muted, false);
        sectionLabel("KEYBOARD");
        textIn(fonts.small, col::Muted, "Space   flip card");
        textIn(fonts.small, col::Muted, "1   still learning       2   got it");
    }
}

// ================================================================ progress

void GuiApp::drawProgress() {
    titleRow("Progress");
    const auto stats = progress_.stats();
    const std::string focusSid = focusId();

    beginPanel("bysection", ImVec2(0, 0), false, true);
    sectionLabel("ACCURACY BY SECTION");
    ImGui::Dummy(ImVec2(0, 4));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;

    for (const auto& s : registry_.all()) {
        auto it = stats.find(s.id);
        const bool has = it != stats.end();
        const bool focus = s.id == focusSid;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 size(w, 88);
        ImGui::Dummy(size);

        dl->AddRectFilled(p, p + size, u32(col::CardFill), 10.0f);
        dl->AddRect(p, p + size, u32(focus ? col::Accent : col::Border), 10.0f, 0, focus ? 2.0f : 1.2f);

        const float bh = fonts.heading->FontSize * ImGui::GetIO().FontGlobalScale;
        const float sh = fonts.small->FontSize * ImGui::GetIO().FontGlobalScale;
        dl->AddText(fonts.heading, bh, ImVec2(p.x + 22, p.y + 16), u32(focus ? col::Accent : col::Text),
                    s.name.c_str());
        std::string sub = has ? fmt("%d session%s  \xC2\xB7  %.1f s per question", it->second.sessions,
                                    it->second.sessions == 1 ? "" : "s",
                                    it->second.avgSecondsPerQuestion())
                              : "Not started yet";
        dl->AddText(fonts.small, sh, ImVec2(p.x + 22, p.y + 20 + bh), u32(col::Muted), sub.c_str());

        if (has) {
            const double acc = it->second.accuracy();
            const ImVec4 c = acc >= 80 ? col::Good : acc >= 60 ? col::Accent : col::Bad;
            std::string pct = fmt("%.0f%%", acc);
            ImVec2 ps = textSize(fonts.heading, pct.c_str());
            dl->AddText(fonts.heading, bh, ImVec2(p.x + w - 22 - ps.x, p.y + 16), u32(c), pct.c_str());
            // accuracy bar along the bottom
            const float x0 = p.x + 22, x1 = p.x + w - 22, y = p.y + size.y - 18;
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 5), u32(col::Line, 1.6f), 3.0f);
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + (x1 - x0) * static_cast<float>(acc / 100.0), y + 5),
                              u32(c), 3.0f);
        }
        if (focus) {
            ImVec2 ts = textSize(fonts.label, "FOCUS AREA");
            ImVec2 a(p.x + w - 22 - ts.x - 14 - 70, p.y + 18);
            dl->AddRectFilled(a, a + ImVec2(ts.x + 14, ts.y + 8), u32(col::Accent), 5.0f);
            const float lh = fonts.label->FontSize * ImGui::GetIO().FontGlobalScale;
            dl->AddText(fonts.label, lh, a + ImVec2(7, 4), u32(col::OnAccent), "FOCUS AREA");
        }
    }
    endPanel();
}

void GuiApp::drawOverviewSide() {
    const float w = ImGui::GetContentRegionAvail().x;
    const Overall ov = overall();
    const auto stats = progress_.stats();
    const auto missed = missedBySubtest();
    int missedTotal = 0;
    for (const auto& [id, n] : missed) missedTotal += n;

    textIn(fonts.heading, col::Text, "Your progress");
    divider();

    sectionLabel("OVERALL ACCURACY");
    const ImVec2 boxSize(150, 96);
    statBox(boxSize, ov.total ? fmt("%d%%", ov.correct * 100 / ov.total).c_str() : "\xE2\x80\x94",
            ov.total ? fmt("%d answered", ov.total).c_str() : "No sessions yet", col::Text);
    ImGui::SameLine(0, 18);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, 4));
    textIn(fonts.big, col::Accent, fmt("%d", ov.sessions).c_str());
    textIn(fonts.small, col::Muted, "sessions");
    ImGui::EndGroup();
    divider();

    labelWithValue("FLASHCARDS", fmt("%d / %d MASTERED", deck_.masteredCount(),
                                     static_cast<int>(deck_.size())));
    thinBar(deck_.size() ? static_cast<float>(deck_.masteredCount()) / deck_.size() : 0.0f, w);
    labelWithValue("REVIEW", fmt("%d QUESTIONS", missedTotal), missedTotal ? col::Bad : col::Muted);
    divider();

    // Focus area: click to choose a section, or let the app pick the weakest
    sectionLabel("FOCUS AREA");
    const SubtestInfo* fi = registry_.find(focusId());
    std::string fname = fi ? fi->name : "Not set yet";
    if (listRow("focusrow", fname.c_str(), pinnedFocus_.empty() ? "Auto" : "Pinned",
                pinnedFocus_.empty() ? col::Muted : col::Accent, true))
        ImGui::OpenPopup("focuspicker");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    const bool open = ImGui::BeginPopup("focuspicker");
    ImGui::PopStyleVar();
    if (open) {
        focusMenuItems();
        ImGui::EndPopup();
    }

    sectionLabel("SECTIONS");
    for (const auto& s : registry_.all()) {
        auto it = stats.find(s.id);
        std::string right = it != stats.end() ? fmt("%.0f%%", it->second.accuracy()) : "\xE2\x80\x94";
        ImVec4 rc = it == stats.end() ? col::Muted
                  : it->second.accuracy() >= 80 ? col::Good
                  : it->second.accuracy() >= 60 ? col::Accent : col::Bad;
        ImGui::PushID(s.id.c_str());
        if (listRow("row", s.name.c_str(), right.c_str(), rc, true))
            startSubtest(s, SessionMode::Practice);
        ImGui::PopID();
    }
}

// ================================================================ actions

void GuiApp::goTo(Screen s) {
    message_.clear();
    if (s != Screen::Results) queue_.clear();
    screen_ = s;
}

void GuiApp::startSubtest(const SubtestInfo& s, SessionMode mode) {
    const int count = mode == SessionMode::Timed ? s.questionCount : 0;
    auto questions = bank_.draw(s.id, count, rng_);
    if (questions.empty()) {
        message_ = "No questions loaded for " + s.name + ".";
        screen_ = Screen::Dashboard;
        return;
    }
    message_.clear();
    sessionName_ = s.name;
    sessionTag_ = mode == SessionMode::Timed ? "TIMED TEST" : "PRACTICE";
    sessionTips_ = s.tips;
    sessionTimeLimit_ = s.timeLimitSeconds;
    isReview_ = false;
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

void GuiApp::startReview(const std::string& subtestId) {
    auto questions = bank_.byIds(progress_.missedQuestionIds());
    if (!subtestId.empty()) {
        questions.erase(std::remove_if(questions.begin(), questions.end(),
                                       [&](const Question& q) { return q.subtest != subtestId; }),
                        questions.end());
    }
    if (questions.empty()) {
        message_ = "No missed questions to review. Nice work!";
        return;
    }
    std::shuffle(questions.begin(), questions.end(), rng_);
    message_.clear();
    const SubtestInfo* info = subtestId.empty() ? nullptr : registry_.find(subtestId);
    sessionName_ = info ? "Review: " + info->name : "Review missed questions";
    sessionTag_ = "REVIEW";
    sessionTips_ = info ? info->tips : "";
    sessionTimeLimit_ = 0;
    isReview_ = true;
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
    screen_ = Screen::Results;
}

// ================================================================ focus area

void GuiApp::focusMenuItems() {
    textIn(fonts.label, col::Muted, "CHOOSE YOUR FOCUS AREA");
    ImGui::Dummy(ImVec2(0, 2));
    const ImVec2 row(300, 32);
    const SubtestInfo* w = registry_.find(weakestId());
    std::string autoLabel = w ? "Automatic  (now: " + w->name + ")" : "Automatic  (lowest accuracy)";
    if (ImGui::Selectable(autoLabel.c_str(), pinnedFocus_.empty(), 0, row)) setFocus("");
    ImGui::Separator();
    for (const auto& s : registry_.all())
        if (ImGui::Selectable(s.name.c_str(), pinnedFocus_ == s.id, 0, row)) setFocus(s.id);
}

void GuiApp::cardFocusMenu(const SubtestInfo& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    const bool open = ImGui::BeginPopupContextItem("focusmenu");
    ImGui::PopStyleVar();
    if (!open) return;
    const ImVec2 row(300, 32);
    textIn(fonts.label, col::Muted, "FOCUS AREA");
    if (pinnedFocus_ == s.id) {
        if (ImGui::Selectable("Unpin  (go back to automatic)", false, 0, row)) setFocus("");
    } else {
        if (ImGui::Selectable(("Set " + s.name + " as focus").c_str(), false, 0, row)) setFocus(s.id);
        if (!pinnedFocus_.empty() && ImGui::Selectable("Use automatic focus", false, 0, row))
            setFocus("");
    }
    ImGui::EndPopup();
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
    } catch (...) {
        pinnedFocus_.clear();  // unreadable file: fall back to automatic
    }
}

void GuiApp::saveSettings() const {
    nlohmann::json j;
    j["focus"] = pinnedFocus_;
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
