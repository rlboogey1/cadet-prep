#pragma once
#include "TestSession.h"
#include <map>
#include <set>
#include <string>
#include <vector>

struct SubtestStats {
    int sessions = 0;
    int correct = 0;
    int total = 0;
    double seconds = 0.0;  // total time spent answering
    double accuracy() const { return total ? 100.0 * correct / total : 0.0; }
    double avgSecondsPerQuestion() const { return total ? seconds / total : 0.0; }
};

// Saves every session to a JSON file and tracks questions you've missed.
class ProgressStore {
public:
    explicit ProgressStore(std::string path);

    // Review sessions are saved but excluded from accuracy stats.
    void record(const SessionResult& result, bool isReview = false);
    std::map<std::string, SubtestStats> stats() const;

    // Questions missed and not yet answered correctly since.
    std::vector<std::string> missedQuestionIds() const;

private:
    void load();
    void save() const;

    std::string path_;
    struct SessionEntry {
        std::string date, subtestId;
        int correct = 0, total = 0;
        double seconds = 0.0;
        bool review = false;
    };
    std::vector<SessionEntry> sessions_;
    std::set<std::string> missed_;
};
