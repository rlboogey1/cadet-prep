#pragma once
#include <string>
#include <vector>

// One multiple-choice item. Every subtest (knowledge now, pilot sections later)
// produces these, so the session/scoring code never needs to change.
struct Question {
    std::string id;           // unique, e.g. "WK-003"
    std::string subtest;      // subtest id, e.g. "word_knowledge"
    std::string passage;      // optional (Reading Comprehension); empty otherwise
    std::string prompt;
    std::vector<std::string> choices;
    int correctIndex = 0;     // 0-based index into choices
    std::string explanation;
};

// Result of answering one question.
struct AnswerRecord {
    std::string questionId;
    int chosenIndex = -1;     // -1 = unanswered (time ran out)
    bool correct = false;
    double seconds = 0.0;     // time spent on this question
};
