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

    // Returns up to `count` random questions from one subtest (0 = all), with
    // the answer choices shuffled. For Math Knowledge and Arithmetic Reasoning,
    // about two thirds of the set is newly generated with random numbers.
    std::vector<Question> draw(const std::string& subtestId, size_t count,
                               std::mt19937& rng) const;

    // Looks up questions by id (used to count missed questions).
    std::vector<Question> byIds(const std::vector<std::string>& ids) const;

    // Same as byIds, but in random order with shuffled choices (for review).
    std::vector<Question> drawByIds(const std::vector<std::string>& ids, std::mt19937& rng) const;

    // Puts a question's answer choices in a random order and updates the answer.
    static void shuffleChoices(Question& q, std::mt19937& rng);

    size_t size(const std::string& subtestId) const;

    // Adds a question made in code (Warrior Knowledge cards).
    void add(Question q) { bySubtest_[q.subtest].push_back(std::move(q)); }

private:
    std::unordered_map<std::string, std::vector<Question>> bySubtest_;
};
