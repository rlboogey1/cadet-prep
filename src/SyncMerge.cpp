#include "SyncMerge.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <set>
#include <sstream>

namespace cloudsync {

namespace {

bool present(const json& j) { return j.is_object() && !j.empty(); }

// Each question's latest result. Questions only in the old "missed" list get time 0,
// so any newer result for them wins.
std::map<std::string, std::pair<bool, long long>> outcomes(const json& progress) {
    std::map<std::string, std::pair<bool, long long>> out;
    for (const auto& id : progress.value("missed", json::array()))
        if (id.is_string()) out[id.get<std::string>()] = {true, 0};
    const json saved = progress.value("outcomes", json::object());  // keep alive for the loop
    for (const auto& [id, o] : saved.items())
        if (o.is_object()) out[id] = {o.value("missed", false), o.value("time", 0LL)};
    return out;
}

}  // namespace

long long now() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string newId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream os;
    os << std::hex << rng();
    return os.str();
}

std::string sessionKey(const json& s) {
    const std::string id = s.value("id", "");
    if (!id.empty()) return id;
    std::ostringstream os;
    os << "old|" << s.value("date", "") << '|' << s.value("subtest", "") << '|' << s.value("correct", 0) << '|'
       << s.value("total", 0) << '|' << s.value("seconds", 0.0) << '|' << s.value("review", false);
    return os.str();
}

json mergeProgress(const json& a, const json& b) {
    // Sessions: every one from both sides, once
    std::set<std::string> seen;
    json sessions = json::array();
    for (const json* side : {&a, &b}) {
        for (json s : side->value("sessions", json::array())) {
            if (!s.is_object()) continue;
            const std::string key = sessionKey(s);
            if (!seen.insert(key).second) continue;
            s["id"] = key;
            sessions.push_back(s);
        }
    }
    std::stable_sort(sessions.begin(), sessions.end(), [](const json& x, const json& y) {
        return x.value("date", "") < y.value("date", "");
    });

    // Missed questions: the newer result for each question wins
    auto merged = outcomes(a);
    for (const auto& [id, o] : outcomes(b)) {
        auto it = merged.find(id);
        if (it == merged.end() || o.second > it->second.second) merged[id] = o;
    }
    json out;
    out["sessions"] = sessions;
    out["missed"] = json::array();
    out["outcomes"] = json::object();
    for (const auto& [id, o] : merged) {
        if (o.first) out["missed"].push_back(id);
        out["outcomes"][id] = {{"missed", o.first}, {"time", o.second}};
    }
    return out;
}

json mergeBoxes(const json& a, const json& b) {
    const json boxA = a.value("boxes", json::object()), boxB = b.value("boxes", json::object());
    const json timeA = a.value("changed", json::object()), timeB = b.value("changed", json::object());
    std::set<std::string> words;
    for (const auto& [w, v] : boxA.items()) words.insert(w);
    for (const auto& [w, v] : boxB.items()) words.insert(w);

    json out;
    out["boxes"] = json::object();
    out["changed"] = json::object();
    for (const auto& w : words) {
        const bool inA = boxA.contains(w), inB = boxB.contains(w);
        const long long ta = timeA.value(w, 0LL), tb = timeB.value(w, 0LL);
        const int ba = inA ? boxA[w].get<int>() : 0, bb = inB ? boxB[w].get<int>() : 0;
        int box;
        long long t;
        if (!inB || (inA && ta > tb)) box = ba, t = ta;
        else if (!inA || tb > ta) box = bb, t = tb;
        else box = std::max(ba, bb), t = ta;  // same time (older saves have none): the further box
        out["boxes"][w] = box;
        if (t > 0) out["changed"][w] = t;
    }
    return out;
}

json mergeRepetition(const json& inA, const json& inB) {
    // One side may have no deck yet: merge the other with itself (same layout either way)
    const json& a = present(inA) ? inA : inB;
    const json& b = present(inB) ? inB : inA;
    if (!present(a)) return json::object();
    // The word list: from the side that added or removed words last
    const long long ua = a.value("updated", 0LL), ub = b.value("updated", 0LL);
    // (the vocabulary deck keeps "words" and "batches"; Warrior Knowledge keeps "blocks")
    const auto count = [](const json& side) {
        return side.value("words", json::array()).size() + side.value("blocks", json::array()).size();
    };
    const size_t na = count(a), nb = count(b);
    const json& words = ub > ua || (ub == ua && (nb > na || (nb == na && b.dump() > a.dump()))) ? b : a;
    json out;
    if (a.contains("blocks") || b.contains("blocks")) {
        out["blocks"] = words.value("blocks", json::array());
    } else {
        out["words"] = words.value("words", json::array());
        out["batches"] = words.value("batches", json::array());
    }
    out["updated"] = std::max(ua, ub);

    // The streak: from the side that finished the deck on the later day
    const std::string da = a.value("last_day", ""), db = b.value("last_day", "");
    const int sa = a.value("streak", 0), sb = b.value("streak", 0);
    const bool useB = db > da || (db == da && sb > sa);
    out["last_day"] = useB ? db : da;
    out["streak"] = useB ? sb : sa;
    out["best_streak"] = std::max({a.value("best_streak", 0), b.value("best_streak", 0), sa, sb});
    return out;
}

json mergeSaved(const json& a, const json& b) {
    // Saved questions: every one from both sides, once. Each has a random id and
    // never changes after it's saved, so the same id means the same question;
    // the smaller copy wins only to keep the result the same whichever side is first.
    std::map<std::string, json> byId;
    for (const json* side : {&a, &b}) {
        for (const json& q : side->value("questions", json::array())) {
            if (!q.is_object() || !q.contains("id") || !q["id"].is_string()) continue;
            const std::string id = q["id"].get<std::string>();
            auto it = byId.find(id);
            if (it == byId.end() || q.dump() < it->second.dump()) byId[id] = q;
        }
    }
    json out;
    out["questions"] = json::array();
    for (const auto& [id, q] : byId) out["questions"].push_back(q);
    return out;
}

const std::vector<std::pair<std::string, std::string>>& syncedFiles() {
    static const std::vector<std::pair<std::string, std::string>> files = {
        {"progress", "afoqt_progress.json"},
        {"flashcards", "afoqt_flashcards.json"},
        {"warrior_cards", "warrior_cards_progress.json"},
        {"repetition", "afoqt_repetition.json"},
        {"warrior_repetition", "warrior_repetition.json"},
        {"saved_questions", "afoqt_saved_questions.json"},
    };
    return files;
}

json mergeBundle(const json& a, const json& b) {
    json out = json::object();
    for (const auto& [key, file] : syncedFiles()) {
        // A file only one device has still goes through the merge, so the result
        // always has the same layout (merging it again then changes nothing)
        json fa = a.is_object() ? a.value(key, json()) : json();
        json fb = b.is_object() ? b.value(key, json()) : json();
        if (!present(fa) && !present(fb)) continue;
        if (!present(fa)) fa = json::object();
        if (!present(fb)) fb = json::object();
        if (key == "progress") out[key] = mergeProgress(fa, fb);
        else if (key == "repetition" || key == "warrior_repetition") out[key] = mergeRepetition(fa, fb);
        else if (key == "saved_questions") out[key] = mergeSaved(fa, fb);
        else out[key] = mergeBoxes(fa, fb);
    }
    return out;
}

}  // namespace cloudsync
