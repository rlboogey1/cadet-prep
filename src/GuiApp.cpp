#include "GuiApp.h"
#include "imgui.h"
#include <algorithm>

namespace {
const ImVec4 kGreen(0.25f, 0.70f, 0.35f, 1.0f);
const ImVec4 kRed(0.85f, 0.30f, 0.30f, 1.0f);
const ImVec4 kGray(0.65f, 0.65f, 0.70f, 1.0f);
const ImVec2 kMenuButton(380, 44);
// Draws text in the larger heading font if it was loaded.
void heading(const char* text) {
    ImGuiIO& io = ImGui::GetIO();
    ImFont* f = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : nullptr;
    if (f) ImGui::PushFont(f);
    ImGui::TextUnformatted(text);
    if (f) ImGui::PopFont();
}
}  // namespace

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
    return true;
}

void GuiApp::draw() {
    // One window that fills the whole app window.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("AFOQT Study", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    // NEW: a centered column, at most 780 pixels wide
    const float colWidth = std::min(780.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - colWidth) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("column", ImVec2(colWidth, 0));
    ImGui::PopStyleColor();

    switch (screen_) {
        case Screen::Menu: drawMenu(); break;
        case Screen::PickSubtest: drawPickSubtest(); break;
        case Screen::Quiz: drawQuiz(); break;
        case Screen::Results: drawResults(); break;
        case Screen::Progress: drawProgress(); break;
        case Screen::FlashcardMenu: drawFlashcardMenu(); break;
        case Screen::Flashcard: drawFlashcard(); break;
    }

    ImGui::EndChild();  // NEW: closes the centered column
    ImGui::End();
}

// ---------------------------------------------------------------- screens

void GuiApp::drawMenu() {
    heading("AFOQT Study");
    ImGui::TextColored(kGray, "Knowledge sections");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Practice a subtest (untimed)", kMenuButton)) {
        pickMode_ = SessionMode::Practice;
        screen_ = Screen::PickSubtest;
    }
    if (ImGui::Button("Timed test (one subtest)", kMenuButton)) {
        pickMode_ = SessionMode::Timed;
        screen_ = Screen::PickSubtest;
    }
    if (ImGui::Button("Full knowledge battery (timed)", kMenuButton)) {
        queue_ = registry_.inCategory("knowledge");
        startNextInQueue();
    }
    if (ImGui::Button("Word Knowledge flashcards", kMenuButton)) {
        message_.clear();
        screen_ = Screen::FlashcardMenu;
    }
    if (ImGui::Button("Review missed questions", kMenuButton)) startReview();
    if (ImGui::Button("View progress", kMenuButton)) screen_ = Screen::Progress;

    if (!message_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", message_.c_str());
    }
}

void GuiApp::drawPickSubtest() {
    heading(pickMode_ == SessionMode::Timed ? "Timed test: choose a subtest"
                                        : "Practice: choose a subtest");
    ImGui::Separator();
    ImGui::Spacing();

    for (const auto& s : registry_.all()) {
        std::string label = s.name + "  (" + std::to_string(bank_.size(s.id)) + " questions)";
        if (ImGui::Button(label.c_str(), kMenuButton)) {
            startSubtest(s, pickMode_);
            return;
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Back")) screen_ = Screen::Menu;
}

void GuiApp::drawQuiz() {
    engine_.update();
    if (engine_.finished()) {
        finishSession();
        return;
    }

    const Question& q = engine_.current();
    const bool feedback = engine_.showingFeedback();

    // Header: name, question number, timer
    heading((sessionName_ + " results").c_str());
    ImGui::SameLine();
    ImGui::TextColored(kGray, "   Question %d of %d", engine_.index() + 1, engine_.total());
    const int left = engine_.secondsLeft();
    if (left >= 0) {
        ImGui::SameLine();
        ImGui::TextColored(left <= 30 ? kRed : ImVec4(1, 1, 1, 1), "   Time left %d:%02d",
                           left / 60, left % 60);
    }
    if (!sessionTips_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kGray);
        ImGui::TextWrapped("Tip: %s", sessionTips_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Passage (Reading Comprehension) in its own scrolling box
    if (!q.passage.empty()) {
        ImGui::BeginChild("passage", ImVec2(0, 220), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", q.passage.c_str());
        ImGui::EndChild();
        ImGui::Spacing();
    }

    ImGui::TextWrapped("%s", q.prompt.c_str());
    ImGui::Spacing();

    // Answer buttons. After answering in practice mode, color right/wrong.
    int clicked = -2;  // -2 = nothing, -1 = skip, 0+ = choice
    const float width = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(q.choices.size()); ++i) {
        int colors = 0;
        if (feedback && i == q.correctIndex) {
            ImGui::PushStyleColor(ImGuiCol_Button, kGreen);
            colors = 1;
        } else if (feedback && i == engine_.lastChoice()) {
            ImGui::PushStyleColor(ImGuiCol_Button, kRed);
            colors = 1;
        }
        std::string label = std::string(1, static_cast<char>('A' + i)) + ")  " + q.choices[i];
        ImGui::PushID(i);
        if (ImGui::Button(label.c_str(), ImVec2(width, 48)) && !feedback) clicked = i;
        ImGui::PopID();
        ImGui::PopStyleColor(colors);

        // Keyboard shortcut: press the letter
        if (!feedback && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + i), false))
            clicked = i;
    }
    ImGui::Spacing();

    if (!feedback) {
        if (ImGui::Button("Skip (S)") || ImGui::IsKeyPressed(ImGuiKey_S, false)) clicked = -1;
    } else {
        const int chosen = engine_.lastChoice();
        if (chosen == q.correctIndex) ImGui::TextColored(kGreen, "Correct!");
        else if (chosen < 0) ImGui::TextColored(kRed, "Skipped");
        else ImGui::TextColored(kRed, "Incorrect");
        ImGui::TextWrapped("%s", q.explanation.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Next (Enter)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            engine_.next();
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Quit to menu")) {
        queue_.clear();
        screen_ = Screen::Menu;
        return;
    }

    if (clicked != -2) engine_.submit(clicked);
}

void GuiApp::drawResults() {
    const SessionResult& r = engine_.result();
    const int total = static_cast<int>(r.answers.size());
    const double pct = total ? 100.0 * r.correctCount() / total : 0.0;

    ImGui::Text("%s results", sessionName_.c_str());
    ImGui::Separator();
    ImGui::Text("Score:      %d / %d  (%.0f%%)", r.correctCount(), total, pct);
    ImGui::Text("Answered:   %d / %d", r.answeredCount(), total);
    ImGui::Text("Time used:  %d s", static_cast<int>(r.totalSeconds));
    if (r.timeExpired) ImGui::TextColored(kRed, "Time expired before you finished.");
    ImGui::Spacing();

    if (!queue_.empty() && ImGui::Button("Next section", kMenuButton)) {
        startNextInQueue();
        return;
    }
    if (ImGui::Button("Back to menu", kMenuButton)) {
        queue_.clear();
        screen_ = Screen::Menu;
        return;
    }

    ImGui::Spacing();
    heading("Missed questions");
    ImGui::BeginChild("missed", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const auto& qs = engine_.questions();
    bool any = false;
    for (size_t i = 0; i < r.answers.size(); ++i) {
        if (r.answers[i].correct) continue;
        any = true;
        const Question& q = qs[i];
        ImGui::TextWrapped("%s", q.prompt.c_str());
        const int chosen = r.answers[i].chosenIndex;
        if (chosen >= 0) ImGui::TextColored(kRed, "Your answer: %s", q.choices[chosen].c_str());
        else ImGui::TextColored(kRed, "Not answered");
        ImGui::TextColored(kGreen, "Correct: %s", q.choices[q.correctIndex].c_str());
        ImGui::TextWrapped("%s", q.explanation.c_str());
        ImGui::Separator();
    }
    if (!any) ImGui::Text("None. Perfect score!");
    ImGui::EndChild();
}

void GuiApp::drawProgress() {
    heading("Progress");
    ImGui::Separator();
    ImGui::Spacing();

    const auto stats = progress_.stats();
    if (stats.empty()) {
        ImGui::Text("No sessions yet. Take a practice set first.");
    } else {
        std::string weakest;
        double weakestPct = 101.0;
        if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Subtest");
            ImGui::TableSetupColumn("Sessions");
            ImGui::TableSetupColumn("Accuracy");
            ImGui::TableSetupColumn("Sec / question");
            ImGui::TableHeadersRow();
            for (const auto& s : registry_.all()) {
                auto it = stats.find(s.id);
                if (it == stats.end()) continue;
                const SubtestStats& st = it->second;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%d", st.sessions);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f%%", st.accuracy());
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", st.avgSecondsPerQuestion());
                if (st.accuracy() < weakestPct) {
                    weakestPct = st.accuracy();
                    weakest = s.name;
                }
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::Text("Weakest area: %s", weakest.c_str());
    }
    ImGui::Text("Questions waiting in review: %d",
                static_cast<int>(progress_.missedQuestionIds().size()));
    ImGui::Spacing();
    if (ImGui::Button("Back")) screen_ = Screen::Menu;
}

void GuiApp::drawFlashcardMenu() {
    heading("Word Knowledge Flashcards")
    ImGui::Separator();
    ImGui::Spacing();

    const int mastered = deck_.masteredCount();
    const int total = static_cast<int>(deck_.size());
    ImGui::Text("Mastered %d of %d words", mastered, total);
    ImGui::ProgressBar(total ? static_cast<float>(mastered) / total : 0.0f, ImVec2(380, 0));
    ImGui::TextColored(kGray, "A word is mastered after you get it right 3 times in a row.");
    ImGui::Spacing();

    ImGui::Checkbox("Show the definition first (guess the word)", &definitionFirst_);
    ImGui::Spacing();

    auto start = [&](size_t count, bool learningOnly) {
        deck_.startSession(count, learningOnly, rng_);
        cardFlipped_ = false;
        if (deck_.sessionDone()) message_ = "No words to study. You've mastered them all!";
        else { message_.clear(); screen_ = Screen::Flashcard; }
    };
    if (ImGui::Button("Study 20 words", kMenuButton)) start(20, false);
    if (ImGui::Button("Study words I'm still learning", kMenuButton)) start(30, true);
    if (ImGui::Button("Study the whole deck", kMenuButton)) start(0, false);

    if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Back")) {
        message_.clear();
        screen_ = Screen::Menu;
    }
}

void GuiApp::drawFlashcard() {
    if (deck_.sessionDone()) {
        heading("Session complete");
        ImGui::Separator();
        ImGui::Text("Knew it:         %d", deck_.sessionGotIt());
        ImGui::Text("Still learning:  %d", deck_.sessionMissed());
        ImGui::Text("Mastered overall: %d of %d", deck_.masteredCount(),
                    static_cast<int>(deck_.size()));
        ImGui::Spacing();
        if (ImGui::Button("Back to flashcards", kMenuButton)) screen_ = Screen::FlashcardMenu;
        return;
    }

    const Flashcard& c = deck_.current();
    heading("Flashcards");
    ImGui::SameLine();
    ImGui::TextColored(kGray, "   %d cards left in this session", static_cast<int>(deck_.remaining()));
    ImGui::Separator();
    ImGui::Spacing();

    // The card itself
    ImGui::BeginChild("card", ImVec2(0, 300), ImGuiChildFlags_Borders);
    ImGui::Spacing();
    const bool showWord = !definitionFirst_ || cardFlipped_;
    const bool showMeaning = definitionFirst_ || cardFlipped_;
    if (showWord) {
        ImGui::SetWindowFontScale(2.0f);
        ImGui::Text("%s", c.word.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(kGray, "(%s)", c.partOfSpeech.c_str());
        ImGui::Spacing();
    }
    if (showMeaning) {
        ImGui::TextWrapped("%s", c.definition.c_str());
        if (cardFlipped_) {
            ImGui::Spacing();
            ImGui::TextColored(kGreen, "Synonym: %s", c.synonym.c_str());
            if (!c.example.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, kGray);
                ImGui::TextWrapped("\"%s\"", c.example.c_str());
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();

    // Controls: Space flips; then 1 = still learning, 2 = got it
    if (!cardFlipped_) {
        if (ImGui::Button("Flip card (Space)", kMenuButton) ||
            ImGui::IsKeyPressed(ImGuiKey_Space, false))
            cardFlipped_ = true;
    } else {
        int answer = -1;
        ImGui::PushStyleColor(ImGuiCol_Button, kRed);
        if (ImGui::Button("Still learning (1)", ImVec2(220, 44))) answer = 0;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, kGreen);
        if (ImGui::Button("Got it (2)", ImVec2(220, 44))) answer = 1;
        ImGui::PopStyleColor();
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) answer = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) answer = 1;
        if (answer >= 0) {
            deck_.mark(answer == 1);
            cardFlipped_ = false;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("End session")) screen_ = Screen::FlashcardMenu;
}

// ---------------------------------------------------------------- actions

void GuiApp::startSubtest(const SubtestInfo& s, SessionMode mode) {
    const int count = mode == SessionMode::Timed ? s.questionCount : 0;
    auto questions = bank_.draw(s.id, count, rng_);
    if (questions.empty()) {
        message_ = "No questions loaded for " + s.name + ".";
        screen_ = Screen::Menu;
        return;
    }
    message_.clear();
    sessionName_ = s.name + (mode == SessionMode::Timed ? "  (timed)" : "  (practice)");
    sessionTips_ = s.tips;
    isReview_ = false;
    engine_.start(s.id, std::move(questions), mode, s.timeLimitSeconds);
    screen_ = Screen::Quiz;
}

void GuiApp::startNextInQueue() {
    if (queue_.empty()) {
        screen_ = Screen::Menu;
        return;
    }
    SubtestInfo next = queue_.front();
    queue_.erase(queue_.begin());
    startSubtest(next, SessionMode::Timed);
}

void GuiApp::startReview() {
    auto questions = bank_.byIds(progress_.missedQuestionIds());
    if (questions.empty()) {
        message_ = "No missed questions to review. Nice work!";
        return;
    }
    std::shuffle(questions.begin(), questions.end(), rng_);
    message_.clear();
    sessionName_ = "Review missed questions";
    sessionTips_.clear();
    isReview_ = true;
    engine_.start("review", std::move(questions), SessionMode::Practice, 0);
    screen_ = Screen::Quiz;
}

void GuiApp::finishSession() {
    progress_.record(engine_.result(), isReview_);
    screen_ = Screen::Results;
}
