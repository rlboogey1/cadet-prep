#include "ConsoleUI.h"
#include <cctype>
#include <iostream>
#include <string>

namespace {

std::string readLine() {
    std::string line;
    if (!std::getline(std::cin, line)) return "q";  // EOF behaves like quit/skip
    // trim whitespace
    size_t a = line.find_first_not_of(" \t\r\n");
    size_t b = line.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : line.substr(a, b - a + 1);
}

std::string formatTime(int seconds) {
    int m = seconds / 60, s = seconds % 60;
    return std::to_string(m) + ":" + (s < 10 ? "0" : "") + std::to_string(s);
}

}  // namespace

void ConsoleUI::showMessage(const std::string& text) {
    std::cout << text << "\n";
}

int ConsoleUI::askQuestion(const Question& q, int number, int total, int secondsLeft) {
    std::cout << "\n----------------------------------------------------------\n";
    std::cout << "Question " << number << " of " << total;
    if (secondsLeft >= 0) std::cout << "    [time left " << formatTime(secondsLeft) << "]";
    std::cout << "\n\n";

    if (!q.passage.empty()) std::cout << q.passage << "\n\n";
    std::cout << q.prompt << "\n\n";

    for (size_t i = 0; i < q.choices.size(); ++i)
        std::cout << "  " << static_cast<char>('A' + i) << ") " << q.choices[i] << "\n";

    while (true) {
        std::cout << "\nYour answer (A-" << static_cast<char>('A' + q.choices.size() - 1)
                  << ", or S to skip): ";
        std::string in = readLine();
        if (in.empty()) continue;
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(in[0])));
        if (c == 'S' || c == 'Q') return -1;
        int idx = c - 'A';
        if (idx >= 0 && idx < static_cast<int>(q.choices.size())) return idx;
        std::cout << "Please enter a valid letter.";
    }
}

void ConsoleUI::showFeedback(const Question& q, int chosenIndex) {
    char correct = static_cast<char>('A' + q.correctIndex);
    if (chosenIndex == q.correctIndex) {
        std::cout << "\n  Correct!\n";
    } else if (chosenIndex < 0) {
        std::cout << "\n  Skipped. Answer: " << correct << ") "
                  << q.choices[q.correctIndex] << "\n";
    } else {
        std::cout << "\n  Incorrect. Answer: " << correct << ") "
                  << q.choices[q.correctIndex] << "\n";
    }
    if (!q.explanation.empty()) std::cout << "  " << q.explanation << "\n";
}

int ConsoleUI::menu(const std::string& title, const std::vector<std::string>& options) {
    while (true) {
        std::cout << "\n=== " << title << " ===\n";
        for (size_t i = 0; i < options.size(); ++i)
            std::cout << "  " << (i + 1) << ") " << options[i] << "\n";
        std::cout << "Choose: ";
        std::string in = readLine();
        try {
            int n = std::stoi(in);
            if (n >= 1 && n <= static_cast<int>(options.size())) return n - 1;
        } catch (...) {
        }
        if (in == "q" ) return static_cast<int>(options.size()) - 1;  // EOF -> last option
        std::cout << "Enter a number from 1 to " << options.size() << ".\n";
    }
}

void ConsoleUI::pause() {
    std::cout << "\nPress Enter to continue...";
    readLine();
}
