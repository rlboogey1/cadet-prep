#pragma once
#include "Question.h"
#include "UI.h"
#include <string>
#include <vector>

enum class SessionMode {
    Practice,  // untimed, feedback after every question
    Timed      // countdown, feedback only at the end (like the real test)
};

struct SessionResult {
    std::string subtestId;
    std::vector<AnswerRecord> answers;
    double totalSeconds = 0.0;
    bool timeExpired = false;

    int correctCount() const;
    int answeredCount() const;
};

// Runs one set of questions. Knows nothing about files or the console.
class TestSession {
public:
    TestSession(UI& ui, std::string subtestId, std::vector<Question> questions,
                SessionMode mode, int timeLimitSeconds);

    SessionResult run();

private:
    UI& ui_;
    std::string subtestId_;
    std::vector<Question> questions_;
    SessionMode mode_;
    int timeLimitSeconds_;
};
