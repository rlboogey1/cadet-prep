#pragma once
#include "Question.h"
#include <random>
#include <string>
#include <vector>

// Makes new math questions with random numbers, so Math Knowledge and
// Arithmetic Reasoning never run out of fresh practice.
//
// Each question type ("template") picks random numbers, computes the correct
// answer, and builds wrong choices from common mistakes (forgetting a step,
// using the wrong formula). Generated questions have ids starting with "GEN-".
// They aren't saved in the question files, so they don't go into
// "Review missed".
class QuestionGenerator {
public:
    static bool supports(const std::string& subtestId);
    static std::vector<Question> generate(const std::string& subtestId, size_t count,
                                          std::mt19937& rng);
};
