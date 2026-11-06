#include "SubtestRegistry.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

void SubtestRegistry::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open subtest config: " + path);
    json root;
    in >> root;

    subtests_.clear();
    for (const auto& s : root.at("subtests")) {
        SubtestInfo info;
        info.id = s.at("id").get<std::string>();
        info.name = s.at("name").get<std::string>();
        info.track = s.value("track", "afoqt");
        info.category = s.value("category", "knowledge");
        info.file = s.value("file", "");
        info.visual = s.value("visual", false);
        info.timeLimitSeconds = s.value("time_limit_seconds", 0);
        info.questionCount = s.value("question_count", 0);
        info.tips = s.value("tips", "");
        info.description = s.value("description", "");
        info.icon = s.value("icon", "");
        subtests_.push_back(std::move(info));
    }
}

std::vector<SubtestInfo> SubtestRegistry::inCategory(const std::string& category) const {
    std::vector<SubtestInfo> out;
    for (const auto& s : subtests_)
        if (s.category == category) out.push_back(s);
    return out;
}

std::vector<SubtestInfo> SubtestRegistry::inTrack(const std::string& track) const {
    std::vector<SubtestInfo> out;
    for (const auto& s : subtests_)
        if (s.track == track) out.push_back(s);
    return out;
}

const SubtestInfo* SubtestRegistry::find(const std::string& id) const {
    for (const auto& s : subtests_)
        if (s.id == id) return &s;
    return nullptr;
}
