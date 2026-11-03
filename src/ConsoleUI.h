#pragma once
#include "UI.h"

class ConsoleUI : public UI {
public:
    void showMessage(const std::string& text) override;
    int askQuestion(const Question& q, int number, int total, int secondsLeft) override;
    void showFeedback(const Question& q, int chosenIndex) override;
    int menu(const std::string& title, const std::vector<std::string>& options) override;
    void pause() override;
};
