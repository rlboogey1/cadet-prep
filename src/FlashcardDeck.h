#pragma once
#include <deque>
#include <map>
#include <random>
#include <string>
#include <vector>

struct Flashcard {
    std::string word;
    std::string partOfSpeech;  // "adj", "verb", "noun"
    std::string definition;
    std::string synonym;
    std::string example;
};

// Vocabulary flashcards with simple spaced repetition (the Leitner system).
//
// Every word sits in a "box" from 0 to 5. "Got it" moves a word up one box;
// "Still learning" drops it back to box 0. Each study session pulls words from
// the lowest boxes first, so words you struggle with come up most often.
// A word in box 3 or higher counts as mastered.
class FlashcardDeck {
public:
    static constexpr int kMaxBox = 5;
    static constexpr int kMasteredBox = 3;

    void load(const std::string& cardsPath);
    void loadProgress(const std::string& progressPath);  // missing file = all box 0

    // Starts a session with up to `count` cards (0 = all).
    // learningOnly = only words not yet mastered.
    void startSession(size_t count, bool learningOnly, std::mt19937& rng);

    bool sessionDone() const { return queue_.empty(); }
    const Flashcard& current() const { return cards_[queue_.front()]; }
    size_t remaining() const { return queue_.size(); }
    int sessionGotIt() const { return gotIt_; }
    int sessionMissed() const { return missed_; }

    // Records the answer for the current card and moves on. A missed card
    // comes back later in the same session.
    void mark(bool knewIt);

    size_t size() const { return cards_.size(); }
    int masteredCount() const;
    int box(const std::string& word) const;

private:
    void saveProgress() const;

    std::vector<Flashcard> cards_;
    std::map<std::string, int> boxes_;
    std::deque<size_t> queue_;  // indexes into cards_
    std::string progressPath_;
    int gotIt_ = 0, missed_ = 0;
};
