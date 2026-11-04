#pragma once
#include "FlashcardDeck.h"
#include "ProgressStore.h"
#include "QuestionBank.h"
#include "QuizEngine.h"
#include "SubtestRegistry.h"
#include <map>
#include <random>
#include <string>
#include <vector>

// The whole GUI. draw() is called once per frame.
//
// Layout (like a dashboard):
//   +-------------------------------------------------+
//   | logo |  top bar                          avatar |
//   +------+-----------------------------+------------+
//   | icon |  center: the current screen |  side      |
//   | rail |                             |  panel     |
//   +------+-----------------------------+------------+
class GuiApp {
public:
    bool load(const std::string& dataDir, std::string& error);
    void draw();

private:
    enum class Screen { Dashboard, Quiz, Results, Flashcards, Progress };
    enum class Mode { Practice, Timed, Review };

    // Frame pieces
    void drawTopBar();
    void drawRail();

    // Each screen has a center part and a side-panel part
    void drawDashboard();
    void drawQuiz();
    void drawQuizSide();
    void drawResults();
    void drawFlashcards();
    void drawFlashcardsSide();
    void drawProgress();
    void drawOverviewSide();  // progress summary, shared by several screens
    void focusMenuItems();    // the list of choices in the focus-area menu
    void cardFocusMenu(const SubtestInfo& s);  // right-click menu on a subtest card

    // Actions
    void startSubtest(const SubtestInfo& s, SessionMode mode);
    void startBattery();
    void startNextInQueue();
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

    // Settings saved between launches (afoqt_settings.json)
    void loadSettings();
    void saveSettings() const;
    std::map<std::string, int> missedBySubtest() const;

    SubtestRegistry registry_;
    QuestionBank bank_;
    ProgressStore progress_{"afoqt_progress.json"};
    QuizEngine engine_;
    std::mt19937 rng_{std::random_device{}()};

    Screen screen_ = Screen::Dashboard;
    Mode mode_ = Mode::Practice;
    std::vector<SubtestInfo> queue_;  // remaining sections in a full battery
    std::string sessionName_;
    std::string sessionTag_;          // "PRACTICE", "TIMED TEST", "REVIEW"
    std::string sessionTips_;
    int sessionTimeLimit_ = 0;
    bool isReview_ = false;
    std::string message_;
    std::string pinnedFocus_;  // subtest id chosen by the user; empty = automatic

    // Flashcards
    FlashcardDeck deck_;
    bool flashActive_ = false;
    bool cardFlipped_ = false;
    bool definitionFirst_ = false;
};
