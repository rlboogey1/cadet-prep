#pragma once
#include "Question.h"
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// Loads questions from JSON files and hands out shuffled sets.
class QuestionBank {
public:
    // Loads a JSON file of questions for the given subtest id.
    // Throws std::runtime_error on bad files so mistakes are caught early.
    void loadFile(const std::string& path, const std::string& subtestId);

    // Returns up to `count` random questions from one subtest (0 = all).
    std::vector<Question> draw(const std::string& subtestId, size_t count,
                               std::mt19937& rng) const;

    // Looks up questions by id (used for "review missed questions").
    std::vector<Question> byIds(const std::vector<std::string>& ids) const;

    size_t size(const std::string& subtestId) const;

private:
    std::unordered_map<std::string, std::vector<Question>> bySubtest_;
};
