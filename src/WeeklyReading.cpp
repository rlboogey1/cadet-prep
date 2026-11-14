#include "WeeklyReading.h"
#include "AtomicFile.h"
#include "SyncMerge.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

std::tm localToday() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    tm.tm_hour = 12;  // midday, so daylight saving changes can't move the date
    tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return tm;
}

std::string dateString(std::tm tm) {
    std::mktime(&tm);  // normalizes the day, month and year
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

std::tm parseDate(const std::string& d) {
    std::tm tm{};
    tm.tm_year = std::atoi(d.c_str()) - 1900;
    tm.tm_mon = d.size() >= 7 ? std::atoi(d.c_str() + 5) - 1 : 0;
    tm.tm_mday = d.size() >= 10 ? std::atoi(d.c_str() + 8) : 1;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    return tm;
}

std::string weekBefore(const std::string& monday) {
    std::tm tm = parseDate(monday);
    tm.tm_mday -= 7;
    return dateString(tm);
}

}  // namespace

std::string WeeklyReading::thisWeek() {
    std::tm tm = localToday();
    std::mktime(&tm);  // fills in the weekday
    tm.tm_mday -= (tm.tm_wday + 6) % 7;  // back to Monday
    return dateString(tm);
}

long WeeklyReading::weekNumber() {
    std::tm monday = parseDate(thisWeek()), start = parseDate("2024-01-01");  // a Monday
    const double days = std::difftime(std::mktime(&monday), std::mktime(&start)) / 86400.0;
    return std::lround(days) / 7;
}

std::vector<size_t> WeeklyReading::passagesFor(long week, size_t passageCount) {
    std::vector<size_t> out;
    if (passageCount == 0) return out;
    const long n = static_cast<long>(passageCount);
    const long first = ((week % n) * kPassages % n + n) % n;
    for (int i = 0; i < kPassages && i < n; ++i) out.push_back(static_cast<size_t>((first + i) % n));
    return out;
}

void WeeklyReading::load(const std::string& path) {
    path_ = path;
    weeks_.clear();
    std::ifstream in(path);
    if (!in) return;
    try {
        json root;
        in >> root;
        const json weeks = root.value("weeks", json::object());
        for (const auto& [week, r] : weeks.items())
            weeks_[week] = {r.value("correct", 0), r.value("total", 0), r.value("time", 0LL)};
    } catch (...) {
        weeks_.clear();  // damaged file: set a copy aside before a save replaces it, then start fresh
        atomicfile::setAside(path);
    }
}

void WeeklyReading::save() const {
    if (path_.empty()) return;
    json root;
    root["weeks"] = json::object();
    for (const auto& [week, r] : weeks_)
        root["weeks"][week] = {{"correct", r.correct}, {"total", r.total}, {"time", r.time}};
    atomicfile::write(path_, root.dump(2));
}

const WeeklyReading::Result* WeeklyReading::thisWeekResult() const {
    auto it = weeks_.find(thisWeek());
    return it == weeks_.end() ? nullptr : &it->second;
}

void WeeklyReading::complete(int correct, int total) {
    weeks_[thisWeek()] = {correct, total, cloudsync::now()};
    save();
}

int WeeklyReading::streak() const {
    std::string week = thisWeek();
    if (!weeks_.count(week)) week = weekBefore(week);  // this week still open: last week keeps it alive
    int n = 0;
    while (weeks_.count(week)) {
        ++n;
        week = weekBefore(week);
    }
    return n;
}

int WeeklyReading::bestStreak() const {
    int best = 0, run = 0;
    std::string prev;
    for (const auto& [week, r] : weeks_) {  // in date order
        run = !prev.empty() && weekBefore(week) == prev ? run + 1 : 1;
        best = std::max(best, run);
        prev = week;
    }
    return best;
}
