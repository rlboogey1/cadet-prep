#pragma once
#include "AppPaths.h"
#include "CloudSync.h"
#include "FlashcardDeck.h"
#include "ProgressStore.h"
#include "QuestionBank.h"
#include "QuizEngine.h"
#include "RepetitionDeck.h"
#include "SavedQuestions.h"
#include "Sound.h"
#include "imgui.h"
#include "SubtestRegistry.h"
#include "Widgets.h"
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

// The whole GUI. draw() is called once per frame.
//
// Layout on wide screens (Mac, iPad held sideways):
//   +------------+-------------------------------------------+
//   |  sidebar   |  main area (scrolls)                      |
//   |  (navy)    |  header label .................. chips    |
//   |  logo      |  page content: banner, cards, quiz...     |
//   |  menu      |                                           |
//   |  focus     |                                           |
//   +------------+-------------------------------------------+
// Narrow screens (iPhone, iPad held upright) get the phone layout ("compact"):
// a navy top bar with a menu button that slides the sidebar in, and cards that
// stack into one or two columns.
class GuiApp {
public:
    bool load(const std::string& dataDir, std::string& error);
    void draw();

    // iPhone and iPad (set by mobile_main.cpp)
    void setTouch(bool on) { touch_ = on; }  // drag to scroll, "tap" wording, no keyboard hints
    // Space the device keeps for itself (notch, rounded corners, home bar), in points
    void setSafeArea(float top, float right, float bottom, float left) { safe_ = {top, right, bottom, left}; }
    bool animating() const;  // something is moving: keep drawing at full speed
    void onForeground() { cloud_.onForeground(); }  // app came back to the front: pull other devices' progress
    ImVec4 backgroundColor() const { return ui::col::Bg; }

private:
    enum class Screen { Home, Settings, Dashboard, Practice, Review, Flashcards, Progress, Quiz, Results };

    // Frame pieces
    void drawSidebar(float height);
    void drawTopBar(ImVec2 origin, float width, float height);  // phone layout
    void drawDrawer();                                          // phone layout: the sidebar, slid in
    const char* pageName() const;                               // top bar title
    void touchScroll();                                         // drag with a finger to scroll
    void drawHeader(const std::string& label);  // small CAPS label + chips on the right

    // Pages
    void drawHome();       // main menu: pick a prep section
    void drawSettings();   // sound, dark mode, flashcard options, account
    void drawAccount();    // Settings: sign in for online sync
    void drawDashboard();
    void drawPractice();
    void drawReview();
    void drawFlashcards();
    void drawProgress();
    void drawQuiz();
    void drawCardQuestion(const Question& q);  // Warrior Knowledge: the question as a swipe card
    void swipeButtons(const char* flipLabel);  // the buttons under a swipe card
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
    void startEndless(const SubtestInfo& s);  // math: generated questions until you tap Finish
    void topUpEndless();                      // keep a few generated questions ahead
    void saveGenerated(const Question& q);    // keep a generated question for Review missed
    void saveButton(const Question& q, bool fullWidth, bool sameLine);  // "Save for review" / "Saved for review"
    void startBattery(const std::string& category);
    void startNextInQueue();
    void startWarmup();
    void startReview(const std::string& subtestId);  // empty = all sections
    void startFlashcards(size_t count, bool learningOnly);
    void startRepetition();
    void drawRepetition();
    void drawWarriorRepetition();   // Warrior Knowledge: the spaced repetition page
    void startWarriorRepetition();  // its deck as swipe cards; missed cards come back at the end
    bool warriorRepSession() const { return sessionTag_ == "SPACED REPETITION"; }
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
    void addWarriorCards();  // Warrior Knowledge blocks: one card per handbook item
    void saveSettings() const;
    void reloadProgress();   // after online sync merged other devices' progress into the files

    SubtestRegistry registry_;
    QuestionBank bank_;
    ProgressStore progress_{apppaths::userFile("afoqt_progress.json")};
    SavedQuestions saved_{apppaths::userFile("afoqt_saved_questions.json")};
    std::set<std::string> savedGen_;  // GEN- ids saved this run (the button then says "Saved")
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
    bool endless_ = false;  // endless practice: more questions are added as you go
    std::string message_;
    std::string pinnedFocus_;  // subtest id chosen by the user for the current track; empty = automatic
    std::map<std::string, std::string> pinnedByTrack_;  // the same, for every track
    std::string track_ = "afoqt";
    bool darkMode_ = false;        // saved setting
    bool themeApplied_ = false;    // the saved theme is applied on the first frame (needs ImGui running)

    // Warrior Knowledge quizzes are flashcards: swipe right = knew it, left = didn't.
    bool cardMode_ = false;
    ui::SwipeCard swipe_;  // the card on screen (quiz card mode and the Flashcards page)

    // Answer animation: when the current feedback started (ImGui time), or -1
    double feedbackTime_ = -1.0;

    // Flashcards
    FlashcardDeck deck_;     // AFOQT: Word Knowledge vocabulary
    FlashcardDeck memDeck_;  // Warrior Knowledge: memorize cards (creeds, oaths, lists)
    FlashcardDeck vaWords_;  // Verbal Analogies word definitions (lookup only, never studied)
    FlashcardDeck& activeDeck() { return track_ == "afoqt" ? deck_ : memDeck_; }
    const FlashcardDeck& activeDeck() const { return track_ == "afoqt" ? deck_ : memDeck_; }
    bool flashActive_ = false;
    RepetitionDeck repDeck_;   // AFOQT: the spaced repetition deck and its daily streak
    bool repSession_ = false;  // the current flashcard session is the spaced repetition deck
    bool repCounted_ = false;  // that session's finish counted a new streak day
    BlockRepetition wkRep_;    // Warrior Knowledge: blocks in the spaced repetition deck, and its streak
    bool wkRepCounted_ = false;  // the last Warrior Knowledge deck session counted a new streak day
    bool definitionFirst_ = false;

    // Online sync (Settings > Account)
    CloudSync cloud_;
    char email_[128] = "";
    char password_[128] = "";

    // Phone and tablet
    struct Insets { float top = 0, right = 0, bottom = 0, left = 0; };
    Insets safe_;
    bool touch_ = false;
    bool compact_ = false;   // phone layout this frame
    bool menuOpen_ = false;  // phone layout: the sidebar drawer is showing
    double menuTime_ = -1.0; // when the drawer started opening (slide-in animation)
    // Touch scrolling: the windows under the finger that can scroll, the direction
    // picked once the finger has moved far enough (0 = not yet, -1 = not a scroll),
    // and the speed it keeps after the finger lifts
    void* scrollWinY_ = nullptr;
    void* scrollWinX_ = nullptr;
    int dragAxis_ = 0;
    void* flingWin_ = nullptr;
    ImVec2 flingVel_{0, 0};
};
