#pragma once
#include <string>
#include <vector>

// A picture some pilot questions show above the answers. The numbers in `data`
// describe what to draw:
//   Table:        range R, then (2R+1)^2 values, row by row (Y = +R at the top,
//                 X = -R on the left)
//   Instruments:  bank (degrees, + = right wing down), pitch (+ = nose up), heading
//   Blocks:       block count, then x, y, z, sizeX, sizeY, sizeZ, label for each block
//   Aircraft:     bank, pitch, heading (same meaning as Instruments), drawn as a
//                 plane seen from behind and a little above, looking north
//   Insignia:     scheme, index, onFront (Warrior Knowledge rank cards; 1 = picture on
//                 the front of the card, 0 = only on the back). The picture is
//                 data/insignia/<scheme>_<index>.png, cut from the user's charts:
//                   0 cadet       = AFROTC cadet shoulder board, 0 (Fourth Class) to 7 (Colonel)
//                   1 officer     = pay grade 1 to 10
//                   2 af_enlisted = pay grade 2 to 9 (1 = no insignia, shown as text)
//                   3 sf_enlisted = pay grade 1 to 9, 10 = CMSSF
enum class VisualKind { None, Table, Instruments, Blocks, Aircraft, Insignia };
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
    std::vector<Visual> choiceVisuals;  // optional: one picture per choice, shown instead
                                        // of the choice text (Instrument Comprehension)
};

// Result of answering one question.
struct AnswerRecord {
    std::string questionId;
    int chosenIndex = -1;     // -1 = unanswered (time ran out)
    bool correct = false;
    double seconds = 0.0;     // time spent on this question
};
