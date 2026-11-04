#pragma once
#include "ProgressStore.h"
#include "QuestionBank.h"
#include "QuizEngine.h"
#include "SubtestRegistry.h"
#include <random>
#include <string>
#include <vector>

// The whole GUI. draw() is called once per frame and shows the current screen.
class GuiApp {
public:
    bool load(const std::string& dataDir, std::string& error);
    void draw();

private:
    enum class Screen { Menu, PickSubtest, Quiz, Results, Progress };

    void drawMenu();
    void drawPickSubtest();
    void drawQuiz();
    void drawResults();
    void drawProgress();

    void startSubtest(const SubtestInfo& s, SessionMode mode);
    void startNextInQueue();
    void startReview();
    void finishSession();

    SubtestRegistry registry_;
    QuestionBank bank_;
    ProgressStore progress_{"afoqt_progress.json"};
    QuizEngine engine_;
    std::mt19937 rng_{std::random_device{}()};

    Screen screen_ = Screen::Menu;
    SessionMode pickMode_ = SessionMode::Practice;
    std::vector<SubtestInfo> queue_;   // remaining sections in a full battery
    std::string sessionName_;
    std::string sessionTips_;
    bool isReview_ = false;
    std::string message_;
};
