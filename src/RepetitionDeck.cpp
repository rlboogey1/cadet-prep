#include "RepetitionDeck.h"
#include "FlashcardDeck.h"
#include "SyncMerge.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

// Local date `daysAgo` days back, as "YYYY-MM-DD".
std::string localDate(int daysAgo) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    tm.tm_mday -= daysAgo;
    tm.tm_hour = 12;  // midday, so daylight saving changes can't move the date
    tm.tm_isdst = -1;
    std::mktime(&tm);  // normalizes the day, month and year
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

std::string RepetitionDeck::today() { return localDate(0); }
std::string RepetitionDeck::yesterday() { return localDate(1); }

void RepetitionDeck::load(const std::string& path, const FlashcardDeck& vocab) {
    path_ = path;
    words_.clear();
    batches_.clear();
    streak_ = best_ = 0;
    lastDay_.clear();
    updated_ = 0;
    std::ifstream in(path);
    if (in) {
        try {
            json root;
            in >> root;
            for (const auto& w : root.value("words", json::array())) words_.push_back(w.get<std::string>());
            for (const auto& b : root.value("batches", json::array())) batches_.push_back(b.get<int>());
            streak_ = root.value("streak", 0);
            best_ = root.value("best_streak", 0);
            lastDay_ = root.value("last_day", "");
            updated_ = root.value("updated", 0LL);
        } catch (...) {
            words_.clear();
            batches_.clear();
            streak_ = best_ = 0;
            lastDay_.clear();
        }
    }
    // Drop words no longer in the vocabulary file, and batches that no longer fit.
    words_.erase(std::remove_if(words_.begin(), words_.end(),
                                [&](const std::string& w) {
                                    const auto& cards = vocab.cards();
                                    return std::none_of(cards.begin(), cards.end(),
                                                        [&](const Flashcard& c) { return c.word == w; });
                                }),
                 words_.end());
    int sum = 0;
    for (int b : batches_) sum += b;
    while (!batches_.empty() && (sum > static_cast<int>(words_.size()) - kStartSize || batches_.back() <= 0)) {
        sum -= batches_.back();
        batches_.pop_back();
    }
    // New deck: the first words of the vocabulary file.
    for (const Flashcard& c : vocab.cards()) {
        if (static_cast<int>(words_.size()) >= kStartSize) break;
        if (!contains(words_, c.word)) words_.push_back(c.word);
    }
    save();
}

int RepetitionDeck::canAdd(const FlashcardDeck& vocab) const {
    int n = 0;
    for (const Flashcard& c : vocab.cards())
        if (!contains(words_, c.word) && ++n == kBatch) break;
    return n;
}

void RepetitionDeck::addMore(const FlashcardDeck& vocab) {
    int added = 0;
    for (const Flashcard& c : vocab.cards()) {
        if (added == kBatch) break;
        if (!contains(words_, c.word)) {
            words_.push_back(c.word);
            ++added;
        }
    }
    if (added > 0) {
        batches_.push_back(added);
        updated_ = cloudsync::now();
        save();
    }
}

void RepetitionDeck::removeLastBatch() {
    if (batches_.empty()) return;
    const int n = std::min(batches_.back(), static_cast<int>(words_.size()) - kStartSize);
    words_.resize(words_.size() - static_cast<size_t>(std::max(0, n)));
    batches_.pop_back();
    updated_ = cloudsync::now();
    save();
}

int RepetitionDeck::streak() const {
    return lastDay_ == today() || lastDay_ == yesterday() ? streak_ : 0;
}

bool RepetitionDeck::completePass() {
    const std::string t = today();
    if (lastDay_ == t) return false;
    streak_ = lastDay_ == yesterday() ? streak_ + 1 : 1;
    best_ = std::max(best_, streak_);
    lastDay_ = t;
    save();
    return true;
}

void RepetitionDeck::save() const {
    if (path_.empty()) return;
    json root;
    root["words"] = words_;
    root["batches"] = batches_;
    root["streak"] = streak_;
    root["best_streak"] = best_;
    root["last_day"] = lastDay_;
    root["updated"] = updated_;
    std::ofstream(path_) << root.dump(2);
}

// ---------------------------------------------------------------- BlockRepetition

void BlockRepetition::load(const std::string& path, int blockCount) {
    path_ = path;
    blockCount_ = blockCount;
    blocks_.clear();
    streak_ = best_ = 0;
    lastDay_.clear();
    updated_ = 0;
    std::ifstream in(path);
    if (in) {
        try {
            json root;
            in >> root;
            for (const auto& b : root.value("blocks", json::array())) blocks_.push_back(b.get<int>());
            streak_ = root.value("streak", 0);
            best_ = root.value("best_streak", 0);
            lastDay_ = root.value("last_day", "");
            updated_ = root.value("updated", 0LL);
        } catch (...) {
            blocks_.clear();
            streak_ = best_ = 0;
            lastDay_.clear();
        }
    }
    // Keep real blocks, once each, in order; a new deck starts with Block 1
    blocks_.erase(std::remove_if(blocks_.begin(), blocks_.end(), [&](int b) { return b < 1 || b > blockCount_; }),
                  blocks_.end());
    std::sort(blocks_.begin(), blocks_.end());
    blocks_.erase(std::unique(blocks_.begin(), blocks_.end()), blocks_.end());
    if (blocks_.empty() && blockCount_ > 0) blocks_.push_back(1);
    save();
}

bool BlockRepetition::has(int block) const {
    return std::find(blocks_.begin(), blocks_.end(), block) != blocks_.end();
}

void BlockRepetition::add(int block) {
    if (block < 1 || block > blockCount_ || has(block)) return;
    blocks_.insert(std::upper_bound(blocks_.begin(), blocks_.end(), block), block);
    updated_ = cloudsync::now();
    save();
}

void BlockRepetition::remove(int block) {
    if (blocks_.size() <= 1 || !has(block)) return;
    blocks_.erase(std::find(blocks_.begin(), blocks_.end(), block));
    updated_ = cloudsync::now();
    save();
}

int BlockRepetition::streak() const {
    return lastDay_ == RepetitionDeck::today() || lastDay_ == RepetitionDeck::yesterday() ? streak_ : 0;
}

bool BlockRepetition::completePass() {
    const std::string t = RepetitionDeck::today();
    if (lastDay_ == t) return false;
    streak_ = lastDay_ == RepetitionDeck::yesterday() ? streak_ + 1 : 1;
    best_ = std::max(best_, streak_);
    lastDay_ = t;
    save();
    return true;
}

void BlockRepetition::save() const {
    if (path_.empty()) return;
    json root;
    root["blocks"] = blocks_;
    root["streak"] = streak_;
    root["best_streak"] = best_;
    root["last_day"] = lastDay_;
    root["updated"] = updated_;
    std::ofstream(path_) << root.dump(2);
}
