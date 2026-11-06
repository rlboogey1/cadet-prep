#include "Composites.h"

namespace {
const std::string WK = "word_knowledge", MK = "math_knowledge", AR = "arithmetic_reasoning",
                  VA = "verbal_analogies", RC = "reading_comprehension", TR = "table_reading",
                  IC = "instrument_comprehension", AI = "aviation_information", BC = "block_counting";
}

const std::vector<CompositeInfo>& compositeList() {
    static const std::vector<CompositeInfo> list = {
        {"Pilot", "Pilot training selection", {MK, TR, IC, AI}},
        {"Combat Systems Officer", "CSO (navigator) selection", {WK, MK, TR, BC}},
        {"Air Battle Manager", "ABM selection", {VA, MK, TR, IC, BC, AI}},
        {"Academic Aptitude", "Overall verbal and math ability", {VA, AR, WK, MK, RC}},
        {"Verbal", "Reading and vocabulary", {VA, RC, WK}},
        {"Quantitative", "Math", {AR, MK}},
    };
    return list;
}

std::vector<CompositeEstimate> estimateComposites(const std::map<std::string, SubtestStats>& stats) {
    std::vector<CompositeEstimate> out;
    for (const auto& c : compositeList()) {
        CompositeEstimate e;
        e.info = &c;
        double sum = 0.0, lowest = 101.0;
        for (const auto& id : c.sections) {
            auto it = stats.find(id);
            if (it == stats.end() || it->second.total == 0) {
                e.missing.push_back(id);
                continue;
            }
            const double acc = it->second.accuracy();
            sum += acc;
            ++e.practiced;
            if (acc < lowest) {
                lowest = acc;
                e.weakest = id;
            }
        }
        if (e.practiced) e.score = sum / e.practiced;
        out.push_back(std::move(e));
    }
    return out;
}
