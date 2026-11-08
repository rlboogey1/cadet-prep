#pragma once
#include <string>
#include <vector>

class FlashcardDeck;

// The vocabulary "spaced repetition" deck: a small set of words the user grows
// a few at a time, plus a daily streak.
//
// It starts with the first kStartSize words of the vocabulary file. "Add more"
// appends the next words in file order; "remove last added" takes the most
// recent batch back off (the starting words always stay). Going through the
// whole deck once on a day counts that day; doing it on consecutive days grows
// the streak, and missing a day starts it over.
class RepetitionDeck {
public:
    static constexpr int kStartSize = 3;
    static constexpr int kBatch = 3;

    // Loads saved state; a missing or bad file starts a new deck.
    void load(const std::string& path, const FlashcardDeck& vocab);

    const std::vector<std::string>& words() const { return words_; }
    int canAdd(const FlashcardDeck& vocab) const;  // how many words "add more" would add (0 = none left)
    void addMore(const FlashcardDeck& vocab);
    int lastBatchSize() const { return batches_.empty() ? 0 : batches_.back(); }  // 0 = nothing to remove
    void removeLastBatch();

    // Days in a row (still counts if today isn't done yet but yesterday was).
    int streak() const;
    bool doneToday() const { return lastDay_ == today(); }
    int bestStreak() const { return best_; }
    // Call after finishing the whole deck. Returns true if this counted a new day.
    bool completePass();

    static std::string today();      // local date, "YYYY-MM-DD"
    static std::string yesterday();

private:
    void save() const;

    std::string path_;
    std::vector<std::string> words_;
    std::vector<int> batches_;  // sizes of the batches added after the starting words, oldest first
    int streak_ = 0, best_ = 0;
    std::string lastDay_;       // last day the whole deck was finished
};
