#pragma once
#include <string>
#include <vector>

// Describes one subtest. Adding a new section (including pilot sections later)
// means adding an entry to data/subtests.json -- no code changes for
// multiple-choice sections.
struct SubtestInfo {
    std::string id;            // "word_knowledge"
    std::string name;          // "Word Knowledge"
    std::string track;         // prep section it belongs to: "afoqt" or "warrior"
    std::string category;      // group within the track, e.g. "knowledge" or "pilot"
    std::string file;          // question file, relative to data dir ("" = generated only)
    bool visual = false;       // questions need pictures (GUI only, not the console)
    int timeLimitSeconds = 0;  // timed-test limit (0 = untimed)
    int questionCount = 0;     // questions per timed test (0 = all)
    std::string tips;          // short strategy note shown before starting
    std::string description;   // optional one-line card description (window version)
    std::string icon;          // optional card icon name, e.g. "Star" (window version)
};

class SubtestRegistry {
public:
    void load(const std::string& path);
    const std::vector<SubtestInfo>& all() const { return subtests_; }
    std::vector<SubtestInfo> inCategory(const std::string& category) const;
    std::vector<SubtestInfo> inTrack(const std::string& track) const;
    const SubtestInfo* find(const std::string& id) const;

private:
    std::vector<SubtestInfo> subtests_;
};
