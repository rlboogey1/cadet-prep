#include "FlashcardDeck.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

void FlashcardDeck::load(const std::string& cardsPath) {
    std::ifstream in(cardsPath);
    if (!in) throw std::runtime_error("Cannot open flashcard file: " + cardsPath);
    json root;
    try {
        in >> root;
    } catch (const json::parse_error& e) {
        throw std::runtime_error(cardsPath + ": JSON error: " + e.what());
    }
    cards_.clear();
    for (const auto& c : root.at("cards")) {
        cards_.push_back({c.at("word").get<std::string>(), c.value("pos", ""),
                          c.at("definition").get<std::string>(), c.value("synonym", ""),
                          c.value("example", "")});
    }
}

void FlashcardDeck::loadProgress(const std::string& progressPath) {
    progressPath_ = progressPath;
    boxes_.clear();
    std::ifstream in(progressPath);
    if (!in) return;
    try {
        json root;
        in >> root;
        const json boxes = root.value("boxes", json::object());  // keep alive for the loop
        for (const auto& [word, b] : boxes.items())
            boxes_[word] = std::clamp(b.get<int>(), 0, kMaxBox);
    } catch (...) {
        boxes_.clear();  // corrupt file: start fresh
    }
}

void FlashcardDeck::saveProgress() const {
    if (progressPath_.empty()) return;
    json root;
    root["boxes"] = boxes_;
    std::ofstream(progressPath_) << root.dump(2);
}

int FlashcardDeck::box(const std::string& word) const {
    auto it = boxes_.find(word);
    return it == boxes_.end() ? 0 : it->second;
}

int FlashcardDeck::masteredCount() const {
    int n = 0;
    for (const auto& c : cards_) n += box(c.word) >= kMasteredBox ? 1 : 0;
    return n;
}

void FlashcardDeck::startSession(size_t count, bool learningOnly, std::mt19937& rng) {
    std::vector<size_t> picks;
    for (size_t i = 0; i < cards_.size(); ++i)
        if (!learningOnly || box(cards_[i].word) < kMasteredBox) picks.push_back(i);

    // Shuffle, then stable-sort by box: lowest boxes first, random within a box.
    std::shuffle(picks.begin(), picks.end(), rng);
    std::stable_sort(picks.begin(), picks.end(), [this](size_t a, size_t b) {
        return box(cards_[a].word) < box(cards_[b].word);
    });
    if (count > 0 && count < picks.size()) picks.resize(count);
    std::shuffle(picks.begin(), picks.end(), rng);  // don't study in box order

    queue_.assign(picks.begin(), picks.end());
    gotIt_ = missed_ = 0;
}

void FlashcardDeck::startSession(const std::vector<std::string>& words, std::mt19937& rng) {
    std::vector<size_t> picks;
    for (size_t i = 0; i < cards_.size(); ++i)
        if (std::find(words.begin(), words.end(), cards_[i].word) != words.end()) picks.push_back(i);
    std::shuffle(picks.begin(), picks.end(), rng);
    queue_.assign(picks.begin(), picks.end());
    gotIt_ = missed_ = 0;
}

void FlashcardDeck::mark(bool knewIt) {
    if (queue_.empty()) return;
    size_t idx = queue_.front();
    queue_.pop_front();
    int& b = boxes_[cards_[idx].word];

    if (knewIt) {
        b = std::min(b + 1, kMaxBox);
        ++gotIt_;
    } else {
        b = 0;
        ++missed_;
        // See it again after a few other cards (or at the end if few remain).
        size_t pos = std::min<size_t>(3, queue_.size());
        queue_.insert(queue_.begin() + static_cast<long>(pos), idx);
    }
    saveProgress();
}
