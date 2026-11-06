#pragma once
// Estimated AFOQT composite scores.
//
// The real AFOQT reports composites as percentiles (1-99) compared with other test
// takers, and the scoring formula isn't public. This estimate is simpler: the
// average of your accuracy on each section that goes into the composite. It's a
// guide to which composites are strong and which sections hold them back, not a
// prediction of your official score.
#include "ProgressStore.h"
#include <map>
#include <string>
#include <vector>

struct CompositeInfo {
    std::string name;                   // e.g. "Pilot"
    std::string description;            // what it's used for
    std::vector<std::string> sections;  // subtest ids that go into it
};

struct CompositeEstimate {
    const CompositeInfo* info = nullptr;
    double score = 0.0;                  // average accuracy of practiced sections, 0-100
    int practiced = 0;                   // sections with at least one session
    std::vector<std::string> missing;    // sections not practiced yet
    std::string weakest;                 // practiced section with the lowest accuracy
    bool hasData() const { return practiced > 0; }
    bool complete() const { return missing.empty(); }
};

// The composites on the current AFOQT (Form T), minus Situational Judgment,
// which this app doesn't cover yet.
const std::vector<CompositeInfo>& compositeList();

std::vector<CompositeEstimate> estimateComposites(const std::map<std::string, SubtestStats>& stats);
