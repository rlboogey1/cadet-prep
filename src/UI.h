#pragma once
#include "Question.h"
#include <string>
#include <vector>

// All input/output goes through this interface. TestSession only talks to UI,
// so you can later write an ImGuiUI or QtUI without touching quiz logic.
class UI {
public:
    virtual ~UI() = default;

    virtual void showMessage(const std::string& text) = 0;

    // Shows a question; returns the chosen index (0-based), or -1 to skip.
    // `secondsLeft` < 0 means untimed.
    virtual int askQuestion(const Question& q, int number, int total,
                            int secondsLeft) = 0;

    virtual void showFeedback(const Question& q, int chosenIndex) = 0;

    // Shows a numbered menu; returns the chosen index (0-based).
    virtual int menu(const std::string& title,
                     const std::vector<std::string>& options) = 0;

    virtual void pause() = 0;
};
