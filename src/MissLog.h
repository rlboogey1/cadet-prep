#pragma once
#include <map>
#include <string>
#include <vector>

// Counts how often each word, root, and question type was asked and missed, for
// the Weak spots page. Keys have a kind and a name:
//   word:<word>       a vocabulary word (Word Knowledge questions, vocabulary cards)
//   root:<root>       a word root, prefix or suffix (the roots deck)
//   relation:<type>   a Verbal Analogies question of that relationship
//   relname:<type>    naming that relationship (step 1 of the relationship drill)
//   reading:<type>    a Reading Comprehension question of that type
//
// Syncing: each device keeps its own counts under its own id, and merging takes the
// larger count for each device and key, so nothing is counted twice and merges can
// repeat in any order. The totals add up every device.
class MissLog {
public:
    struct Count {
        std::string name;  // the key without its kind ("candid", "part_whole")
        int seen = 0, missed = 0;
        long long lastMiss = 0;  // seconds since 1970 (0 = never missed)
        double missRate() const { return seen ? static_cast<double>(missed) / seen : 0.0; }
    };

    void load(const std::string& path, const std::string& deviceId);
    void reload() { load(path_, device_); }  // after online sync merged into the file

    // Records one answer. Call save() after a batch (record() alone doesn't write).
    void record(const std::string& kind, const std::string& name, bool missed);
    void save() const;

    // Every key of one kind, all devices added up.
    std::vector<Count> counts(const std::string& kind) const;
    Count count(const std::string& kind, const std::string& name) const;

private:
    struct Cell { int seen = 0, missed = 0; long long last = 0; };
    std::string path_, device_;
    std::map<std::string, std::map<std::string, Cell>> devices_;  // device id -> key -> counts
};
