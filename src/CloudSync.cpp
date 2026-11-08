#include "CloudSync.h"
#include "AppPaths.h"
#include "Http.h"
#include "SyncMerge.h"
#include <chrono>
#include <fstream>
#include <thread>

#ifndef CADET_SUPABASE_URL
#define CADET_SUPABASE_URL ""
#endif
#ifndef CADET_SUPABASE_KEY
#define CADET_SUPABASE_KEY ""
#endif

using json = nlohmann::json;

namespace {

const std::string kUrl = CADET_SUPABASE_URL;
const std::string kKey = CADET_SUPABASE_KEY;  // the project's public ("anon") key
const char* kAccountFile = "afoqt_account.json";

double seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

json readJson(const std::string& path) {
    std::ifstream in(path);
    if (!in) return json();
    try {
        json j;
        in >> j;
        return j;
    } catch (...) {
        return json();
    }
}

// The server's explanation of a failed request, in a form fit to show.
std::string errorText(const http::Response& r) {
    if (r.status == 0) return r.error.empty() ? "Can't reach the server" : r.error;
    try {
        const json j = json::parse(r.body);
        for (const char* k : {"msg", "error_description", "message", "error"})
            if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
    } catch (...) {
    }
    return "Server error " + std::to_string(r.status);
}

std::string utcNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace

// One background sign-in or sync. The worker thread fills in the results and
// sets `done`; the main thread picks them up in update().
struct CloudSync::Job {
    bool signIn = false;
    std::string email, password;
    Account account;      // in: current sign-in; out: refreshed tokens
    json local;           // this device's files when the job started
    std::atomic<bool> done{false};
    // Results
    json merged;          // the progress now stored online
    bool hasMerged = false;
    bool signedOut = false;  // the sign-in expired or was revoked
    std::string error;

    void run();
    bool authorize();     // signs in, or refreshes an old access token
    void sync();
    http::Headers headers() const {
        return {{"apikey", kKey}, {"Authorization", "Bearer " + account.accessToken},
                {"Content-Type", "application/json"}};
    }
    bool takeTokens(const http::Response& r);
};

bool CloudSync::Job::takeTokens(const http::Response& r) {
    try {
        const json j = json::parse(r.body);
        account.accessToken = j.at("access_token").get<std::string>();
        account.refreshToken = j.at("refresh_token").get<std::string>();
        account.expiresAt = cloudsync::now() + j.value("expires_in", 3600LL);
        if (j.contains("user")) {
            account.userId = j["user"].value("id", account.userId);
            account.email = j["user"].value("email", account.email);
        }
        return !account.userId.empty();
    } catch (...) {
        error = "Unexpected answer from the server";
        return false;
    }
}

bool CloudSync::Job::authorize() {
    const http::Headers h = {{"apikey", kKey}, {"Content-Type", "application/json"}};
    if (signIn) {
        const auto r = http::request("POST", kUrl + "/auth/v1/token?grant_type=password", h,
                                     json{{"email", email}, {"password", password}}.dump());
        password.clear();
        if (!r.ok()) {
            error = errorText(r);
            return false;
        }
        account.email = email;
        return takeTokens(r);
    }
    if (account.expiresAt - 60 > cloudsync::now()) return true;  // the access token still works
    const auto r = http::request("POST", kUrl + "/auth/v1/token?grant_type=refresh_token", h,
                                 json{{"refresh_token", account.refreshToken}}.dump());
    if (!r.ok()) {
        error = errorText(r);
        // The server turned the sign-in down (not just a lost connection)
        if (r.status >= 400 && r.status < 500) {
            signedOut = true;
            error = "Signed out (" + error + "). Sign in again to keep syncing.";
        }
        return false;
    }
    return takeTokens(r);
}

void CloudSync::Job::sync() {
    const auto got = http::request("GET", kUrl + "/rest/v1/progress?select=data", headers());
    if (!got.ok()) {
        error = errorText(got);
        return;
    }
    json remote = json::object();
    try {
        const json rows = json::parse(got.body);
        if (rows.is_array() && !rows.empty() && rows[0].contains("data")) remote = rows[0]["data"];
    } catch (...) {
        error = "Unexpected answer from the server";
        return;
    }
    merged = cloudsync::mergeBundle(local, remote);
    if (merged != remote) {
        http::Headers h = headers();
        h.push_back({"Prefer", "resolution=merge-duplicates,return=minimal"});
        const json row = {{"user_id", account.userId}, {"data", merged}, {"updated_at", utcNow()}};
        const auto put = http::request("POST", kUrl + "/rest/v1/progress?on_conflict=user_id", h, row.dump());
        if (!put.ok()) {
            error = errorText(put);
            return;
        }
    }
    hasMerged = true;
}

void CloudSync::Job::run() {
    if (authorize()) sync();
    done = true;
}

// ================================================================ main thread

bool CloudSync::configured() const { return !kUrl.empty() && !kKey.empty(); }

void CloudSync::load() {
    const json j = readJson(apppaths::userFile(kAccountFile));
    if (j.is_object()) {
        account_.email = j.value("email", "");
        account_.userId = j.value("user_id", "");
        account_.accessToken = j.value("access_token", "");
        account_.refreshToken = j.value("refresh_token", "");
        account_.expiresAt = j.value("expires_at", 0LL);
        lastSync_ = static_cast<std::time_t>(j.value("last_sync", 0LL));
    }
    if (configured() && signedIn()) syncNow();  // pull what the other devices saved
}

void CloudSync::saveAccount() const {
    const std::string path = apppaths::userFile(kAccountFile);
    if (!signedIn()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }
    const json j = {{"email", account_.email}, {"user_id", account_.userId},
                    {"access_token", account_.accessToken}, {"refresh_token", account_.refreshToken},
                    {"expires_at", account_.expiresAt}, {"last_sync", static_cast<long long>(lastSync_)}};
    std::ofstream(path) << j.dump(2);
}

std::string CloudSync::status() const {
    if (busy()) return "Syncing...";
    if (!error_.empty()) return error_;
    if (lastSync_ == 0) return "Not synced yet";
    std::tm tm{}, today{};
    localtime_r(&lastSync_, &tm);
    const std::time_t now = std::time(nullptr);
    localtime_r(&now, &today);
    char buf[64];
    const bool sameDay = tm.tm_year == today.tm_year && tm.tm_yday == today.tm_yday;
    std::strftime(buf, sizeof buf, sameDay ? "Synced at %-I:%M %p" : "Synced %b %-d at %-I:%M %p", &tm);
    return buf;
}

void CloudSync::start(std::shared_ptr<Job> job) {
    job->account = account_;
    job->local = readLocal();
    job_ = job;
    lastAttempt_ = std::time(nullptr);
    pending_ = false;
    changedAt_ = -1;
    // The thread keeps its own reference, so quitting mid-request is safe
    std::thread([job] { job->run(); }).detach();
}

void CloudSync::signIn(const std::string& email, const std::string& password) {
    if (!configured() || busy()) return;
    auto job = std::make_shared<Job>();
    job->signIn = true;
    job->email = email;
    job->password = password;
    error_.clear();
    start(job);
}

void CloudSync::signOut() {
    if (busy()) return;
    account_ = Account{};
    error_.clear();
    lastSync_ = 0;
    saveAccount();
}

void CloudSync::syncNow() {
    if (!configured() || !signedIn()) return;
    if (busy()) pending_ = true;
    else start(std::make_shared<Job>());
}

json CloudSync::readLocal() const {
    json bundle = json::object();
    for (const auto& [key, file] : cloudsync::syncedFiles()) {
        json j = readJson(apppaths::userFile(file));
        if (j.is_object()) bundle[key] = j;
    }
    return bundle;
}

void CloudSync::rememberFileTimes() {
    fileTimes_.clear();
    std::error_code ec;
    for (const auto& [key, file] : cloudsync::syncedFiles()) {
        auto t = std::filesystem::last_write_time(apppaths::userFile(file), ec);
        if (!ec) fileTimes_[file] = t;
    }
}

bool CloudSync::filesChanged() const {
    std::error_code ec;
    for (const auto& [key, file] : cloudsync::syncedFiles()) {
        auto t = std::filesystem::last_write_time(apppaths::userFile(file), ec);
        auto it = fileTimes_.find(file);
        if (ec ? it != fileTimes_.end() : (it == fileTimes_.end() || it->second != t)) return true;
    }
    return false;
}

bool CloudSync::apply(Job& job) {
    if (job.signedOut) {
        account_ = Account{};
        saveAccount();
        error_ = job.error;
        return false;
    }
    if (!job.account.refreshToken.empty()) account_ = job.account;  // new tokens, even if the sync failed
    error_ = job.error;
    bool changed = false;
    if (job.hasMerged) {
        // Merge again with the files as they are now, in case progress was saved
        // while the sync ran; that newer progress goes up with the next sync.
        const json now = readLocal();
        const json merged = cloudsync::mergeBundle(now, job.merged);
        for (const auto& [key, file] : cloudsync::syncedFiles()) {
            if (!merged.contains(key) || (now.contains(key) && now[key] == merged[key])) continue;
            std::ofstream(apppaths::userFile(file)) << merged[key].dump(2);
            changed = true;
        }
        if (now != job.local) pending_ = true;
        lastSync_ = std::time(nullptr);
    }
    saveAccount();
    rememberFileTimes();
    return changed;
}

bool CloudSync::update() {
    if (!configured()) return false;
    bool changed = false;
    if (job_ && job_->done) {
        auto job = job_;
        job_.reset();
        changed = apply(*job);
        if (pending_ && signedIn()) syncNow();
    }
    if (!signedIn() || busy()) return changed;

    // Every second: has progress been saved since the last sync? Wait until it's
    // been quiet for a few seconds (a flashcard session saves after every card).
    const double t = seconds();
    if (t < nextCheck_) return changed;
    nextCheck_ = t + 1.0;
    if (filesChanged()) {
        rememberFileTimes();
        changedAt_ = t;
    }
    if (changedAt_ >= 0 && t - changedAt_ > 3.0) syncNow();
    else if (std::time(nullptr) - lastAttempt_ > 600) syncNow();  // pick up other devices, or retry
    return changed;
}
