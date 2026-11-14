#pragma once
#include "Question.h"
#include "TestSession.h"  // SessionMode, SessionResult
#include <chrono>
#include <string>
#include <vector>

// A quiz that advances one step at a time instead of waiting for input.
// A GUI redraws many times per second, so it can't block on "wait for answer";
// it asks this class what to show and tells it when the user clicks.
//
// Time counts only while the quiz is on screen: the clock adds up the time between
// calls to update() (once a frame), skips any time while paused (app in the
// background, window minimized), and counts at most kMaxStep for one gap, so time
// the app wasn't running at all (computer asleep, app suspended, a browser tab in the
// background) is left out. std::chrono::steady_clock keeps running through sleep on
// macOS and iOS, so it can't be used directly. Timed tests, per-question times, and
// the saved session time all use this active time.
class QuizEngine {
public:
    void start(std::string subtestId, std::vector<Question> questions,
               SessionMode mode, int timeLimitSeconds);

    void update();                 // call every frame: counts the time since the last frame, ends the quiz when time runs out
    void setPaused(bool paused);   // app hidden (background, minimized): time stops counting
    void submit(int choice);       // record an answer (-1 = skip)
    void next();                   // practice mode: move on after viewing feedback
    void append(std::vector<Question> more);  // endless practice: add questions to the end
    void finishNow();              // end early (endless practice); only answered questions count

    bool finished() const { return finished_; }
    bool showingFeedback() const { return showingFeedback_; }
    int lastChoice() const { return lastChoice_; }
    int index() const { return index_; }
    int total() const { return static_cast<int>(questions_.size()); }
    int secondsLeft() const;       // -1 when untimed
    const Question& current() const { return questions_[index_]; }
    const std::vector<Question>& questions() const { return questions_; }
    const SessionResult& result() const { return result_; }

private:
    void advance();
    void finish();

    using Clock = std::chrono::steady_clock;
    static constexpr double kMaxStep = 1.0;  // seconds: a longer gap between frames means the app wasn't running
    void tick();  // adds the time since the last tick (unless paused) to active_
    std::vector<Question> questions_;
    SessionMode mode_ = SessionMode::Practice;
    int timeLimitSeconds_ = 0;
    SessionResult result_;
    int index_ = 0;
    int lastChoice_ = -1;
    bool showingFeedback_ = false;
    bool finished_ = true;
    Clock::time_point lastTick_;
    double active_ = 0.0;         // seconds the quiz has been on screen
    double questionStart_ = 0.0;  // active_ when the current question appeared
    bool paused_ = false;
};
