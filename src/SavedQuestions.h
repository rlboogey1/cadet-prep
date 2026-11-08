#pragma once
#include "Question.h"
#include <string>
#include <vector>

// Generated math questions the user chose to keep for "Review missed".
//
// Generated questions (ids "GEN-...") are made fresh each time and normally
// vanish after the session. Saving one stores a full copy with its own random
// id ("SAVED-..."), so the review list can find it again later and on other
// devices (the file syncs). File: afoqt_saved_questions.json
//   {"questions": [{id, subtest, prompt, choices, answer, explanation, saved}]}
class SavedQuestions {
public:
    explicit SavedQuestions(std::string path);
    void reload();  // reads the file again (after online sync merged into it)

    const std::vector<Question>& all() const { return questions_; }

    // Stores a copy of `q` under a new id and returns that copy.
    Question add(const Question& q);

private:
    void load();
    void save() const;

    std::string path_;
    std::vector<Question> questions_;
    std::vector<long long> savedAt_;  // when each was saved (seconds since 1970)
};
