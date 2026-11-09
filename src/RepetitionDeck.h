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

    void reload(const FlashcardDeck& vocab) { load(path_, vocab); }  // after online sync merged into the file

private:
    void save() const;

    std::string path_;
    std::vector<std::string> words_;
    std::vector<int> batches_;  // sizes of the batches added after the starting words, oldest first
    int streak_ = 0, best_ = 0;
    std::string lastDay_;       // last day the whole deck was finished
    long long updated_ = 0;     // when words were last added or removed (for syncing; 0 = never)
};

// The Warrior Knowledge spaced repetition deck: the handbook blocks the user has
// added, a whole block at a time, plus its own daily streak (same rules as above).
// It starts with Block 1, and always keeps at least one block.
class BlockRepetition {
public:
    // Loads saved state; a missing or bad file starts with Block 1.
    // `blockCount` = how many blocks exist (numbered 1 to blockCount).
    void load(const std::string& path, int blockCount);
    void reload() { load(path_, blockCount_); }  // after online sync merged into the file

    const std::vector<int>& blocks() const { return blocks_; }  // in order
    bool has(int block) const;
    void add(int block);
    void remove(int block);  // does nothing for the last block left

    int streak() const;
    bool doneToday() const { return lastDay_ == RepetitionDeck::today(); }
    int bestStreak() const { return best_; }
    bool completePass();  // after going through the whole deck; true if it counted a new day

private:
    void save() const;

    std::string path_;
    int blockCount_ = 0;
    std::vector<int> blocks_;
    int streak_ = 0, best_ = 0;
    std::string lastDay_;
    long long updated_ = 0;  // when blocks were last added or removed (for syncing)
};
