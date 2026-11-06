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
    enum class Screen { Home, Settings, Dashboard, Practice, Review, Flashcards, Progress, Quiz, Results };

    // Frame pieces
    void drawSidebar(float height);
    void drawHeader(const std::string& label);  // small CAPS label + chips on the right

    // Pages
    void drawHome();       // main menu: pick a prep section
    void drawSettings();   // sound, dark mode, flashcard options
    void drawDashboard();
    void drawPractice();
    void drawReview();
    void drawFlashcards();
    void drawProgress();
    void drawQuiz();
    void drawResults();

    // Shared pieces
    void drawStatRow();
    void drawComposites();  // estimated composite scores (Progress page)
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
    Overall overall() const;  // current prep section
    Overall overallFor(const std::string& track) const;
    std::string weakestId() const;  // lowest accuracy
    std::string focusId() const;    // pinned section, or the weakest if none is pinned
    void setFocus(const std::string& id);  // "" = automatic

    // Prep sections ("tracks"): AFOQT Prep, Warrior Knowledge, ...
    // Every page shows only the current track's sections.
    std::vector<SubtestInfo> trackSections() const { return registry_.inTrack(track_); }
    bool inTrack(const std::string& subtestId) const { return inTrackOf(subtestId, track_); }
    bool inTrackOf(const std::string& subtestId, const std::string& track) const;  // also true for its warm-ups
    void switchTrack(const std::string& track);
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

    Screen screen_ = Screen::Home;  // the app opens on the main menu
    Screen lastScreen_ = Screen::Dashboard;
    std::vector<SubtestInfo> queue_;  // remaining sections in a full battery
    std::string sessionName_;
    std::string sessionTag_;          // "PRACTICE", "TIMED TEST", "REVIEW", "WARM-UP"
    std::string sessionTips_;
    int sessionTimeLimit_ = 0;
    bool isReview_ = false;
    std::string message_;
    std::string pinnedFocus_;  // subtest id chosen by the user for the current track; empty = automatic
    std::map<std::string, std::string> pinnedByTrack_;  // the same, for every track
    std::string track_ = "afoqt";
    bool darkMode_ = false;        // saved setting
    bool themeApplied_ = false;    // the saved theme is applied on the first frame (needs ImGui running)

    // Answer animation: when the current feedback started (ImGui time), or -1
    double feedbackTime_ = -1.0;

    // Flashcards
    FlashcardDeck deck_;     // AFOQT: Word Knowledge vocabulary
    FlashcardDeck memDeck_;  // Warrior Knowledge: memorize cards (creeds, oaths, lists)
    FlashcardDeck& activeDeck() { return track_ == "afoqt" ? deck_ : memDeck_; }
    const FlashcardDeck& activeDeck() const { return track_ == "afoqt" ? deck_ : memDeck_; }
    bool flashActive_ = false;
    bool cardFlipped_ = false;
    bool definitionFirst_ = false;
};
