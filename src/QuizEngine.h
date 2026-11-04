#pragma once
#include "Question.h"
#include "TestSession.h"  // SessionMode, SessionResult
#include <chrono>
#include <string>
#include <vector>

// A quiz that advances one step at a time instead of waiting for input.
// A GUI redraws many times per second, so it can't block on "wait for answer";
// it asks this class what to show and tells it when the user clicks.
class QuizEngine {
public:
    void start(std::string subtestId, std::vector<Question> questions,
               SessionMode mode, int timeLimitSeconds);

    void update();                 // call every frame: ends the quiz when time runs out
    void submit(int choice);       // record an answer (-1 = skip)
    void next();                   // practice mode: move on after viewing feedback

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
    std::vector<Question> questions_;
    SessionMode mode_ = SessionMode::Practice;
    int timeLimitSeconds_ = 0;
    SessionResult result_;
    int index_ = 0;
    int lastChoice_ = -1;
    bool showingFeedback_ = false;
    bool finished_ = true;
    Clock::time_point start_, questionStart_;
};
