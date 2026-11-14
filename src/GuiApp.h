#pragma once
#include "AppPaths.h"
#include "CloudSync.h"
#include "FlashcardDeck.h"
#include "MissLog.h"
#include "ProgressStore.h"
#include "QuestionBank.h"
#include "QuizEngine.h"
#include "RepetitionDeck.h"
#include "SavedQuestions.h"
#include "Sound.h"
#include "imgui.h"
#include "SubtestRegistry.h"
#include "Widgets.h"
#include "WeeklyReading.h"
#include <map>
#include <utility>
#include <random>
#include <set>
#include <string>
#include <vector>

// The whole GUI. draw() is called once per frame.
//
// Layout on wide screens (Mac, iPad held sideways):
//   +------------+-------------------------------------------+
//   |  sidebar   |  main area (scrolls)                      |
//   |  (navy)    |  page title                               |
//   |  wordmark  |  next up strip, figures line, tables...   |
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
    enum class Screen { Home, Settings, Dashboard, Practice, Review, Flashcards, Progress, Quiz, Results, WeakSpots };

    // Frame pieces
    void drawSidebar(float height);
    void drawTopBar(ImVec2 origin, float width, float height);  // phone layout
    void drawDrawer();                                          // phone layout: the sidebar, slid in
    const char* pageName() const;                               // top bar title
    void touchScroll();                                         // drag with a finger to scroll

    // Pages
    void drawHome();       // main menu: pick a prep section
    void drawSettings();   // sound, dark mode, flashcard options, account
    void drawAccount();    // Settings: sign in for online sync
    void drawDashboard();
    void drawPractice();
    void drawReview();
    void drawFlashcards();
    void drawProgress();
    void drawWeakSpots();  // AFOQT: the words, roots and question types missed most
    void drawQuiz();
    void drawPassage(const std::string& passage);  // Reading Comprehension: the passage with zoom and highlighting
    void drawCardQuestion(const Question& q);  // Warrior Knowledge: the question as a swipe card
    void swipeButtons(const char* flipLabel);  // the buttons under a swipe card
    void drawResults();

    // Shared pieces
    struct Cell {                // one figure in a table row
        std::string text;        // as shown in its column ("87%")
        std::string phone;       // with its unit, for stacked rows on phones ("87% accuracy")
        ImVec4 color;
        std::vector<float> trend;  // if set: a small line of these values (0-100) instead of text
        Cell(std::string t, std::string p = "", ImVec4 c = ui::col::Ink, std::vector<float> tr = {})
            : text(std::move(t)), phone(std::move(p)), color(c), trend(std::move(tr)) {}
    };
    struct TableRow {
        std::string name, tag;              // tag: small amber label after the name ("FOCUS")
        std::string note;                   // optional muted line under the name (tips, rules)
        std::vector<Cell> cells;            // one per column header
        std::vector<std::string> buttons;   // outline buttons on the right
        const SubtestInfo* focusMenu = nullptr;  // right-click menu to pin the focus area
    };
    // A flat sheet: a header row of mono caps, then one row per item between thin rules.
    // Returns {row, button} for a click this frame, or {-1, -1}.
    std::pair<int, int> drawTable(const char* id, const char* first, const std::vector<const char*>& headers,
                                  const std::vector<TableRow>& rows, float minActionsW = 0.0f);

    // "Next up": the one thing to do next in a prep section. In order: review missed
    // questions, then today's spaced repetition deck, then practice the focus area
    // (pinned, else a section not tried yet, else the lowest accuracy).
    struct NextUp {
        enum Kind { None, Review, Deck, Practice } kind = None;
        std::string track, title, detail, button, subtest;
        bool urgent = false;  // amber mark: missed questions waiting, or a streak at risk
    };
    NextUp nextUpFor(const std::string& track) const;
    bool drawNextUp(const NextUp& n, bool showTrack, bool warmup);  // true if it started something
    void runNextUp(const NextUp& n);
    void drawFigures();  // the line of figures under the strip on a dashboard
    void drawComposites();  // estimated composite scores (AFOQT dashboard)
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
    void startCardDrill(const std::vector<std::string>& words, bool roots);  // Weak spots: these cards, once
    void recordCard(const Flashcard& c, bool knewIt);  // vocabulary and roots cards: count it for Weak spots
    // Verbal Analogies relationship drill: name the first pair's relationship, then answer.
    // `type` empty = every relationship.
    void startRelationDrill(const std::string& type);
    void drawRelationStep(const Question& q);
    void startReadingType(const std::string& type);  // Reading Comprehension: questions of one type
    // The weekly timed reading set (WeeklyReading)
    std::vector<Question> weeklyQuestions() const;  // this week's, in passage order, choices as written
    void startWeeklyReading();
    void drawWeeklyStrip();  // its row on the AFOQT dashboard
    void recordMisses();     // after a quiz: count its words and question types for Weak spots
    // The AFOQT spaced repetition deck can be studied three ways (see repMode_).
    enum class RepMode { Swipe, Quiz, Describe };
    void startRepetition(RepMode mode, bool roots = false);
    void drawRepetition(bool roots);  // roots: the word roots deck instead of the vocabulary deck
    void buildDeckQuestion(const Flashcard& c);  // a quiz question for this word (repQ_)
    void drawDeckQuiz(const Flashcard& c);
    void drawDeckDescribe(const Flashcard& c);
    void finishDeckCard(bool knewIt);  // quiz and describe: record the answer, go to the next word
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
    std::map<std::string, int> missedIn(const std::string& track) const;

    // Settings saved between launches (afoqt_settings.json)
    void loadSettings();
#if defined(__EMSCRIPTEN__)
    void drawDownloads();  // the website's Download section (main menu)
    bool downloadStarted_ = false;
#endif
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
    std::vector<std::string> notices_;  // atomicfile notices (a damaged file set aside, a save that failed), until dismissed
    bool showAllSessions_ = false;  // Progress: the whole session log, not just the latest 20
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
    FlashcardDeck rootDeck_; // AFOQT: word roots, prefixes and suffixes (data/roots.json)
    bool repRoots_ = false;  // the flashcard session on screen is the roots deck
    FlashcardDeck& activeDeck() { return track_ == "afoqt" ? (repRoots_ ? rootDeck_ : deck_) : memDeck_; }
    const FlashcardDeck& activeDeck() const { return track_ == "afoqt" ? (repRoots_ ? rootDeck_ : deck_) : memDeck_; }
    RepetitionDeck& activeRep() { return repRoots_ ? rootRep_ : repDeck_; }
    bool flashActive_ = false;
    RepetitionDeck repDeck_;   // AFOQT: the spaced repetition deck and its daily streak
    RepetitionDeck rootRep_;   // AFOQT: the word roots deck and its own daily streak
    bool repSession_ = false;  // the current flashcard session is the spaced repetition deck
    bool repCounted_ = false;  // that session's finish counted a new streak day
    RepMode repMode_ = RepMode::Swipe;  // how the deck is being studied (the last way used, between sessions)
    // Quiz: the question on screen. Five choices of one kind: 0 = pick the definition
    // of the word, 1 = pick the word for the definition, 2 = pick the word's synonym.
    struct DeckQuestion {
        int kind = -1;  // -1 = not built yet for the current word
        std::string prompt;
        std::vector<std::string> choices;
        int correct = 0;
        int chosen = -1;  // -1 = not answered yet
    };
    DeckQuestion repQ_;
    std::map<std::string, int> repLastKind_;  // per word this session: a word that comes back is asked another way
    // Describe: what the user typed, and whether the answer is showing
    char repText_[1024] = "";
    bool repRevealed_ = false;
    bool repDontKnow_ = false;  // revealed with "I don't know" (counts as missed)
    bool repFocus_ = false;     // put the cursor in the text box next frame
    BlockRepetition wkRep_;    // Warrior Knowledge: blocks in the spaced repetition deck, and its streak
    bool wkRepCounted_ = false;  // the last Warrior Knowledge deck session counted a new streak day
    bool definitionFirst_ = false;

    // Weak spots: what's missed most (afoqt_misses.json; counted per device, see MissLog)
    MissLog misses_;
    std::string deviceId_;  // this device's id in that file (saved in settings, which don't sync)

    // Relationship drill: step 1 (name the relationship) for the question on screen
    bool relDrill_ = false;
    int relStepFor_ = -1;        // the question index whose step 1 is done (-1 = none yet)
    int relChoicesFor_ = -1;     // the question index relChoices_ was made for
    std::vector<int> relChoices_;  // indexes into verbal::relationTypes()
    int relPick_ = -1;           // the choice picked in step 1 (-1 = not yet)
    int relNamed_ = 0, relNamedRight_ = 0;  // step 1 this session

    // Reading passages: text size (an index into the zoom steps in drawPassage, saved in settings)
    // and highlights. Highlights are kept per passage only while a session is open (one flag
    // per word); every session start clears them. On touch screens a drag scrolls the page,
    // so highlighting is a mode (highlightMode_) that turns that off over the passage.
    int passageZoom_ = 1;
    bool highlightMode_ = false;
    std::map<std::string, std::vector<char>> highlights_;
    struct HighlightDrag {
        bool active = false;
        int anchor = 0;          // the word the press started on
        bool paint = true;       // painting (true) or erasing (false), decided by that first word
        std::vector<char> base;  // the flags when the press started; the dragged range is applied on top
    } hlDrag_;
    bool passageLive_ = false;   // touch + highlight mode: the passage rect below takes drags
    ImVec2 passageMin_{0, 0}, passageMax_{0, 0};

    // The weekly reading set
    WeeklyReading weekly_;
    bool weeklySession_ = false;  // the session on screen is this week's set

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
