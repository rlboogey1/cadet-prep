#include "QuestionBank.h"
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
    if (it == bySubtest_.end()) return {};

    std::vector<Question> pool = it->second;
    std::shuffle(pool.begin(), pool.end(), rng);
    if (count > 0 && count < pool.size()) pool.resize(count);
    return pool;
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
