#pragma once
#include <map>
#include <string>
#include <vector>

// The weekly timed reading set: each calendar week (Monday to Sunday, local time)
// has its own 3 passages from Reading Comprehension, taken as one timed test.
// The set moves 3 passages along the file every week, so consecutive weeks never
// share a passage and every passage comes around in turn. Finishing the set in a
// week marks that week done; done weeks in a row make the weekly streak.
class WeeklyReading {
public:
    static constexpr int kPassages = 3;

    void load(const std::string& path);
    void reload() { load(path_); }  // after online sync merged into the file

    static std::string thisWeek();             // Monday of this week, "YYYY-MM-DD"
    static long weekNumber();                  // weeks since Monday 2024-01-01 (picks the passages)
    // Indexes (into the list of passages in file order) of this week's set.
    static std::vector<size_t> passagesFor(long week, size_t passageCount);

    bool doneThisWeek() const { return weeks_.count(thisWeek()) > 0; }
    int streak() const;      // weeks in a row (still counts if this week isn't done yet but last week was)
    int bestStreak() const;
    struct Result { int correct = 0, total = 0; long long time = 0; };
    const Result* thisWeekResult() const;
    void complete(int correct, int total);  // a retake replaces the week's score

private:
    void save() const;
    std::string path_;
    std::map<std::string, Result> weeks_;  // Monday of the week -> result
};
