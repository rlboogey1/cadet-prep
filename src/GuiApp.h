#pragma once
#include "FlashcardDeck.h"
#include "ProgressStore.h"
#include "QuestionBank.h"
#include "QuizEngine.h"
#include "Sound.h"
#include "imgui.h"
#include "SubtestRegistry.h"
#include <map>
#include <random>
#include <string>
#include <vector>

// The whole GUI. draw() is called once per frame.
//
// Layout:
//   +------------+-------------------------------------------+
//   |  sidebar   |  main area (scrolls)                      |
//   |  (navy)    |  header label .................. chips    |
//   |  logo      |  page content: banner, cards, quiz...     |
//   |  menu      |                                           |
//   |  focus     |                                           |
//   +------------+-------------------------------------------+
class GuiApp {
public:
    bool load(const std::string& dataDir, std::string& error);
    void draw();

private:
    enum class Screen { Dashboard, Practice, Review, Flashcards, Progress, Quiz, Results };

    // Frame pieces
    void drawSidebar(float height);
    void drawHeader(const std::string& label);  // small CAPS label + chips on the right

    // Pages
    void drawDashboard();
    void drawPractice();
    void drawReview();
    void drawFlashcards();
    void drawProgress();
    void drawQuiz();
    void drawResults();

    // Shared pieces
    void drawStatRow();
    void drawSectionGrid(const std::vector<SubtestInfo>& sections, bool practicePage, bool addVocabCard);
    int subjectIndex(const std::string& id) const;  // position in the subtest list
    void focusMenuItems();
    void cardFocusMenu(const SubtestInfo& s, ImVec2 cardMin, ImVec2 cardMax);

    // Actions
    void startSubtest(const SubtestInfo& s, SessionMode mode);
    void startBattery(const std::string& category);
    void startNextInQueue();
    void startWarmup();
    void startReview(const std::string& subtestId);  // empty = all sections
    void startFlashcards(size_t count, bool learningOnly);
    void finishSession();
    void goTo(Screen s);

    // Stats helpers
    struct Overall { int correct = 0, total = 0, sessions = 0; };
    Overall overall() const;
    std::string weakestId() const;  // lowest accuracy
    std::string focusId() const;    // pinned section, or the weakest if none is pinned
    void setFocus(const std::string& id);  // "" = automatic
    std::map<std::string, int> missedBySubtest() const;

    // Settings saved between launches (afoqt_settings.json)
    void loadSettings();
    void saveSettings() const;

    SubtestRegistry registry_;
    QuestionBank bank_;
    ProgressStore progress_{"afoqt_progress.json"};
    QuizEngine engine_;
    Sound sound_;
    std::mt19937 rng_{std::random_device{}()};

    Screen screen_ = Screen::Dashboard;
    Screen lastScreen_ = Screen::Dashboard;
    std::vector<SubtestInfo> queue_;  // remaining sections in a full battery
    std::string sessionName_;
    std::string sessionTag_;          // "PRACTICE", "TIMED TEST", "REVIEW", "WARM-UP"
    std::string sessionTips_;
    int sessionTimeLimit_ = 0;
    bool isReview_ = false;
    std::string message_;
    std::string pinnedFocus_;  // subtest id chosen by the user; empty = automatic

    // Answer animation: when the current feedback started (ImGui time), or -1
    double feedbackTime_ = -1.0;

    // Flashcards
    FlashcardDeck deck_;
    bool flashActive_ = false;
    bool cardFlipped_ = false;
    bool definitionFirst_ = false;
};
