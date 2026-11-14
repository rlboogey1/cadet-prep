#include "MissLog.h"
#include "SyncMerge.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// File layout: {"devices": {"<device id>": {"<kind>:<name>": [seen, missed, last miss time]}}}
void MissLog::load(const std::string& path, const std::string& deviceId) {
    path_ = path;
    device_ = deviceId;
    devices_.clear();
    std::ifstream in(path);
    if (!in) return;
    try {
        json root;
        in >> root;
        const json devices = root.value("devices", json::object());
        for (const auto& [dev, keys] : devices.items()) {
            if (!keys.is_object()) continue;
            for (const auto& [key, v] : keys.items()) {
                if (!v.is_array() || v.size() < 3) continue;
                devices_[dev][key] = {v[0].get<int>(), v[1].get<int>(), v[2].get<long long>()};
            }
        }
    } catch (...) {
        devices_.clear();  // unreadable file: start fresh
    }
}

void MissLog::record(const std::string& kind, const std::string& name, bool missed) {
    if (name.empty()) return;
    Cell& c = devices_[device_][kind + ":" + name];
    ++c.seen;
    if (missed) {
        ++c.missed;
        c.last = cloudsync::now();
    }
}

void MissLog::save() const {
    if (path_.empty()) return;
    json root;
    root["devices"] = json::object();
    for (const auto& [dev, keys] : devices_) {
        json k = json::object();
        for (const auto& [key, c] : keys) k[key] = json::array({c.seen, c.missed, c.last});
        root["devices"][dev] = k;
    }
    std::ofstream(path_) << root.dump(1);
}

std::vector<MissLog::Count> MissLog::counts(const std::string& kind) const {
    const std::string prefix = kind + ":";
    std::map<std::string, Count> sum;
    for (const auto& [dev, keys] : devices_) {
        for (const auto& [key, c] : keys) {
            if (key.compare(0, prefix.size(), prefix) != 0) continue;
            Count& s = sum[key.substr(prefix.size())];
            s.name = key.substr(prefix.size());
            s.seen += c.seen;
            s.missed += c.missed;
            s.lastMiss = std::max(s.lastMiss, c.last);
        }
    }
    std::vector<Count> out;
    for (auto& [name, c] : sum) out.push_back(c);
    return out;
}

MissLog::Count MissLog::count(const std::string& kind, const std::string& name) const {
    Count out;
    out.name = name;
    const std::string key = kind + ":" + name;
    for (const auto& [dev, keys] : devices_) {
        auto it = keys.find(key);
        if (it == keys.end()) continue;
        out.seen += it->second.seen;
        out.missed += it->second.missed;
        out.lastMiss = std::max(out.lastMiss, it->second.last);
    }
    return out;
}
