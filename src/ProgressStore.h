#pragma once
#include "TestSession.h"
#include <map>
#include <string>
#include <vector>

struct SubtestStats {
    int sessions = 0;
    int correct = 0;
    int total = 0;
    double seconds = 0.0;  // total time spent answering
    std::string last;      // date of the latest session, "YYYY-MM-DD HH:MM" ("" = none)
    double accuracy() const { return total ? 100.0 * correct / total : 0.0; }
    double avgSecondsPerQuestion() const { return total ? seconds / total : 0.0; }
};

// Saves every session to a JSON file and tracks questions you've missed.
class ProgressStore {
public:
    explicit ProgressStore(std::string path);
    void reload();  // reads the file again (after online sync merged into it)

    // Review sessions are saved but excluded from accuracy stats.
    void record(const SessionResult& result, bool isReview = false);
    std::map<std::string, SubtestStats> stats() const;

    // Puts a question on the review list (a generated question the user saved).
    void markMissed(const std::string& questionId);

    // Questions missed and not yet answered correctly since.
    std::vector<std::string> missedQuestionIds() const;

private:
    void load();
    void save() const;

    std::string path_;
    struct SessionEntry {
        std::string id;  // random, so syncing two devices never counts a session twice
        std::string date, subtestId;
        int correct = 0, total = 0;
        double seconds = 0.0;
        bool review = false;
    };
    std::vector<SessionEntry> sessions_;
    // Each question's latest result: missed or not, and when (seconds since 1970).
    // Syncing keeps the newer result for each question.
    struct Outcome { bool missed = false; long long time = 0; };
    std::map<std::string, Outcome> outcomes_;
};
