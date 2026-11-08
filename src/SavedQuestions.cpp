#include "SavedQuestions.h"
#include "SyncMerge.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

SavedQuestions::SavedQuestions(std::string path) : path_(std::move(path)) { load(); }

void SavedQuestions::reload() {
    questions_.clear();
    savedAt_.clear();
    load();
}

void SavedQuestions::load() {
    std::ifstream in(path_);
    if (!in) return;  // nothing saved yet
    json root;
    try {
        in >> root;
    } catch (...) {
        return;  // corrupt file: start fresh rather than crash
    }
    for (const auto& item : root.value("questions", json::array())) {
        if (!item.is_object()) continue;
        Question q;
        q.id = item.value("id", "");
        q.subtest = item.value("subtest", "");
        q.prompt = item.value("prompt", "");
        q.choices = item.value("choices", std::vector<std::string>{});
        q.correctIndex = item.value("answer", 0);
        q.explanation = item.value("explanation", "");
        q.passage = item.value("passage", "");
        if (q.id.empty() || q.choices.size() < 2 || q.correctIndex < 0 ||
            q.correctIndex >= static_cast<int>(q.choices.size()))
            continue;  // skip anything damaged instead of showing a broken question
        questions_.push_back(std::move(q));
        savedAt_.push_back(item.value("saved", 0LL));
    }
}

void SavedQuestions::save() const {
    json root;
    root["questions"] = json::array();
    for (size_t i = 0; i < questions_.size(); ++i) {
        const Question& q = questions_[i];
        json item = {{"id", q.id},           {"subtest", q.subtest},       {"prompt", q.prompt},
                     {"choices", q.choices}, {"answer", q.correctIndex},   {"explanation", q.explanation},
                     {"saved", savedAt_[i]}};
        if (!q.passage.empty()) item["passage"] = q.passage;
        root["questions"].push_back(item);
    }
    std::ofstream(path_) << root.dump(2);
}

Question SavedQuestions::add(const Question& q) {
    Question copy = q;
    copy.id = "SAVED-" + cloudsync::newId();
    copy.visual = Visual{};
    copy.choiceVisuals.clear();
    questions_.push_back(copy);
    savedAt_.push_back(cloudsync::now());
    save();
    return copy;
}
