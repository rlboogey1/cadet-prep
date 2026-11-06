#pragma once
#include <string>
#include <vector>

// A picture some pilot questions show above the answers. The numbers in `data`
// describe what to draw:
//   Table:        range R, then (2R+1)^2 values, row by row (Y = +R at the top,
//                 X = -R on the left)
//   Instruments:  bank (degrees, + = right wing down), pitch (+ = nose up), heading
//   Blocks:       block count, then x, y, z, sizeX, sizeY, sizeZ, label for each block
enum class VisualKind { None, Table, Instruments, Blocks };
struct Visual {
    VisualKind kind = VisualKind::None;
    std::vector<int> data;
};

// One multiple-choice item. Every subtest produces these, so the session and
// scoring code works the same for all of them.
struct Question {
    std::string id;           // unique, e.g. "WK-003"
    std::string subtest;      // subtest id, e.g. "word_knowledge"
    std::string passage;      // optional (Reading Comprehension); empty otherwise
    std::string prompt;
    std::vector<std::string> choices;
    int correctIndex = 0;     // 0-based index into choices
    std::string explanation;
    Visual visual;            // optional picture (pilot sections)
};

// Result of answering one question.
struct AnswerRecord {
    std::string questionId;
    int chosenIndex = -1;     // -1 = unanswered (time ran out)
    bool correct = false;
    double seconds = 0.0;     // time spent on this question
};
