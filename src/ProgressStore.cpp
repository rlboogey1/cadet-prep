#include "ProgressStore.h"
#include "SyncMerge.h"
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

void ProgressStore::reload() {
    sessions_.clear();
    outcomes_.clear();
    load();
}

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
        // Sessions saved before syncing existed have no id: cloudsync::sessionKey() makes one
        sessions_.push_back({cloudsync::sessionKey(s), s.value("date", ""), s.value("subtest", ""),
                             s.value("correct", 0), s.value("total", 0),
                             s.value("seconds", 0.0), s.value("review", false)});
    }
    for (const auto& id : root.value("missed", json::array()))
        outcomes_[id.get<std::string>()] = {true, 0};
    const json outcomes = root.value("outcomes", json::object());  // keep alive for the loop
    for (const auto& [id, o] : outcomes.items())
        outcomes_[id] = {o.value("missed", false), o.value("time", 0LL)};
}

void ProgressStore::save() const {
    json root;
    root["sessions"] = json::array();
    for (const auto& s : sessions_) {
        root["sessions"].push_back({{"id", s.id}, {"date", s.date}, {"subtest", s.subtestId},
                                    {"correct", s.correct}, {"total", s.total},
                                    {"seconds", s.seconds}, {"review", s.review}});
    }
    root["missed"] = json(missedQuestionIds());  // also read by older versions
    root["outcomes"] = json::object();
    for (const auto& [id, o] : outcomes_) root["outcomes"][id] = {{"missed", o.missed}, {"time", o.time}};
    std::ofstream(path_) << root.dump(2);
}

void ProgressStore::record(const SessionResult& result, bool isReview) {
    SessionEntry e;
    e.id = cloudsync::newId();
    e.date = nowString();
    e.subtestId = result.subtestId;
    e.total = static_cast<int>(result.answers.size());
    e.correct = result.correctCount();
    e.seconds = result.totalSeconds;
    e.review = isReview;
    sessions_.push_back(e);

    const long long now = cloudsync::now();
    for (const auto& a : result.answers) {
        if (a.questionId.rfind("GEN-", 0) == 0) continue;  // generated: can't be reviewed later
        outcomes_[a.questionId] = {!a.correct, now};
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
    std::vector<std::string> out;
    for (const auto& [id, o] : outcomes_)
        if (o.missed) out.push_back(id);
    return out;
}
