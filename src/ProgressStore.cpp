#include "ProgressStore.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace {
std::string nowString() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return os.str();
}
}  // namespace

ProgressStore::ProgressStore(std::string path) : path_(std::move(path)) { load(); }

void ProgressStore::load() {
    std::ifstream in(path_);
    if (!in) return;  // first run: no file yet
    json root;
    try {
        in >> root;
    } catch (...) {
        return;  // corrupt file: start fresh rather than crash
    }
    for (const auto& s : root.value("sessions", json::array())) {
        sessions_.push_back({s.value("date", ""), s.value("subtest", ""),
                             s.value("correct", 0), s.value("total", 0),
                             s.value("seconds", 0.0), s.value("review", false)});
    }
    for (const auto& id : root.value("missed", json::array()))
        missed_.insert(id.get<std::string>());
}

void ProgressStore::save() const {
    json root;
    root["sessions"] = json::array();
    for (const auto& s : sessions_) {
        root["sessions"].push_back({{"date", s.date}, {"subtest", s.subtestId},
                                    {"correct", s.correct}, {"total", s.total},
                                    {"seconds", s.seconds}, {"review", s.review}});
    }
    root["missed"] = json(std::vector<std::string>(missed_.begin(), missed_.end()));
    std::ofstream(path_) << root.dump(2);
}

void ProgressStore::record(const SessionResult& result, bool isReview) {
    SessionEntry e;
    e.date = nowString();
    e.subtestId = result.subtestId;
    e.total = static_cast<int>(result.answers.size());
    e.correct = result.correctCount();
    e.seconds = result.totalSeconds;
    e.review = isReview;
    sessions_.push_back(e);

    for (const auto& a : result.answers) {
        if (a.correct) missed_.erase(a.questionId);
        else missed_.insert(a.questionId);
    }
    save();
}

std::map<std::string, SubtestStats> ProgressStore::stats() const {
    std::map<std::string, SubtestStats> out;
    for (const auto& s : sessions_) {
        if (s.review) continue;
        auto& st = out[s.subtestId];
        st.sessions++;
        st.correct += s.correct;
        st.total += s.total;
        st.seconds += s.seconds;
    }
    return out;
}

std::vector<std::string> ProgressStore::missedQuestionIds() const {
    return {missed_.begin(), missed_.end()};
}
