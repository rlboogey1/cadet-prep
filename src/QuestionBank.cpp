#include "QuestionBank.h"
#include "QuestionGenerator.h"
#include <numeric>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

void QuestionBank::loadFile(const std::string& path, const std::string& subtestId) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open question file: " + path);

    json root;
    try {
        in >> root;
    } catch (const json::parse_error& e) {
        throw std::runtime_error(path + ": JSON error: " + e.what());
    }

    auto& list = bySubtest_[subtestId];
    for (const auto& item : root.at("questions")) {
        Question q;
        q.id = item.at("id").get<std::string>();
        q.subtest = subtestId;
        q.passage = item.value("passage", "");
        q.prompt = item.at("prompt").get<std::string>();
        q.choices = item.at("choices").get<std::vector<std::string>>();
        q.correctIndex = item.at("answer").get<int>();
        q.explanation = item.value("explanation", "");

        if (q.choices.size() < 2 || q.correctIndex < 0 ||
            q.correctIndex >= static_cast<int>(q.choices.size())) {
            throw std::runtime_error(path + ": question " + q.id +
                                     " has an invalid answer index");
        }
        list.push_back(std::move(q));
    }
}

std::vector<Question> QuestionBank::draw(const std::string& subtestId, size_t count,
                                         std::mt19937& rng) const {
    auto it = bySubtest_.find(subtestId);
    std::vector<Question> pool;
    if (it != bySubtest_.end()) pool = it->second;
    else if (!QuestionGenerator::supports(subtestId)) return {};
    std::shuffle(pool.begin(), pool.end(), rng);

    if (QuestionGenerator::supports(subtestId)) {
        // Sections with written questions: two thirds of a timed test is generated,
        // and practice (count 0) gets 10 written questions plus 20 generated, so
        // every set is mostly new. Generated-only sections (Table Reading, etc.)
        // get a full set.
        size_t generated = count > 0 ? count * 2 / 3 : 20;
        if (pool.empty()) generated = count > 0 ? count : 20;
        const size_t written = count > 0 ? count - generated : 10;
        if (pool.size() > written) pool.resize(written);
        auto extra = QuestionGenerator::generate(subtestId, generated, rng);
        pool.insert(pool.end(), extra.begin(), extra.end());
        std::shuffle(pool.begin(), pool.end(), rng);
    }
    if (count > 0 && count < pool.size()) pool.resize(count);

    for (auto& q : pool) shuffleChoices(q, rng);
    return pool;
}

std::vector<Question> QuestionBank::drawByIds(const std::vector<std::string>& ids,
                                              std::mt19937& rng) const {
    auto out = byIds(ids);
    std::shuffle(out.begin(), out.end(), rng);
    for (auto& q : out) shuffleChoices(q, rng);
    return out;
}

void QuestionBank::shuffleChoices(Question& q, std::mt19937& rng) {
    const std::string correct = q.choices[q.correctIndex];
    std::vector<size_t> order(q.choices.size());
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);

    std::vector<std::string> shuffled;
    for (size_t i : order) shuffled.push_back(q.choices[i]);
    q.choices = std::move(shuffled);
    if (q.choiceVisuals.size() == order.size()) {  // keep each picture with its choice
        std::vector<Visual> pics;
        for (size_t i : order) pics.push_back(q.choiceVisuals[i]);
        q.choiceVisuals = std::move(pics);
    }
    q.correctIndex = static_cast<int>(
        std::find(q.choices.begin(), q.choices.end(), correct) - q.choices.begin());
}

std::vector<Question> QuestionBank::byIds(const std::vector<std::string>& ids) const {
    std::vector<Question> out;
    for (const auto& [subtest, list] : bySubtest_) {
        for (const auto& q : list) {
            if (std::find(ids.begin(), ids.end(), q.id) != ids.end()) out.push_back(q);
        }
    }
    return out;
}

size_t QuestionBank::size(const std::string& subtestId) const {
    auto it = bySubtest_.find(subtestId);
    return it == bySubtest_.end() ? 0 : it->second.size();
}
