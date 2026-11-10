#include "CloudSync.h"
#include "AppPaths.h"
#include "Http.h"
#include "SyncMerge.h"
#include <chrono>
#include <fstream>
#include <functional>
#include <thread>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

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
// sets `done`; the main thread picks them up in update(). It's written as steps
// (make a request, handle its answer) so the browser version, which has no
// threads and can't wait for an answer, can run the same steps one after another.
struct CloudSync::Job {
    struct Request {
        std::string method, url;
        http::Headers headers;
        std::string body;
    };

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

    void run();                                   // all the steps, waiting for each answer
    static void runAsync(std::shared_ptr<Job> job);  // the same, without waiting (browser)

    // Step 1: sign in, or refresh an old access token (skipped while it still works)
    bool needsAuth() const { return signIn || account.expiresAt - 60 <= cloudsync::now(); }
    Request authRequest();
    bool authDone(const http::Response& r);
    // Step 2: download what's online and merge. True if `put` must be sent (step 3).
    Request getRequest() const;
    bool gotDone(const http::Response& r, Request& put);
    void putDone(const http::Response& r);

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

CloudSync::Job::Request CloudSync::Job::authRequest() {
    const http::Headers h = {{"apikey", kKey}, {"Content-Type", "application/json"}};
    if (signIn) {
        Request q{"POST", kUrl + "/auth/v1/token?grant_type=password", h,
                  json{{"email", email}, {"password", password}}.dump()};
        password.clear();
        return q;
    }
    return {"POST", kUrl + "/auth/v1/token?grant_type=refresh_token", h,
            json{{"refresh_token", account.refreshToken}}.dump()};
}

bool CloudSync::Job::authDone(const http::Response& r) {
    if (!r.ok()) {
        error = errorText(r);
        // The server turned a saved sign-in down (not just a lost connection)
        if (!signIn && r.status >= 400 && r.status < 500) {
            signedOut = true;
            error = "Signed out (" + error + "). Sign in again to keep syncing.";
        }
        return false;
    }
    if (signIn) account.email = email;
    return takeTokens(r);
}

CloudSync::Job::Request CloudSync::Job::getRequest() const {
    return {"GET", kUrl + "/rest/v1/progress?select=data", headers(), ""};
}

bool CloudSync::Job::gotDone(const http::Response& got, Request& put) {
    if (!got.ok()) {
        error = errorText(got);
        return false;
    }
    json remote = json::object();
    try {
        const json rows = json::parse(got.body);
        if (rows.is_array() && !rows.empty() && rows[0].contains("data")) remote = rows[0]["data"];
    } catch (...) {
        error = "Unexpected answer from the server";
        return false;
    }
    merged = cloudsync::mergeBundle(local, remote);
    if (merged == remote) {  // nothing new here to send up
        hasMerged = true;
        return false;
    }
    http::Headers h = headers();
    h.push_back({"Prefer", "resolution=merge-duplicates,return=minimal"});
    const json row = {{"user_id", account.userId}, {"data", merged}, {"updated_at", utcNow()}};
    put = {"POST", kUrl + "/rest/v1/progress?on_conflict=user_id", h, row.dump()};
    return true;
}

void CloudSync::Job::putDone(const http::Response& r) {
    if (r.ok()) hasMerged = true;
    else error = errorText(r);
}

void CloudSync::Job::run() {
    auto send = [](const Request& q) { return http::request(q.method, q.url, q.headers, q.body); };
    if (!needsAuth() || authDone(send(authRequest()))) {
        Request put;
        if (gotDone(send(getRequest()), put)) putDone(send(put));
    }
    done = true;
}

#if defined(__EMSCRIPTEN__)
void CloudSync::Job::runAsync(std::shared_ptr<Job> job) {
    auto send = [](const Request& q, std::function<void(const http::Response&)> then) {
        http::requestAsync(q.method, q.url, q.headers, q.body, std::move(then));
    };
    auto sync = [job, send] {
        send(job->getRequest(), [job, send](const http::Response& got) {
            Request put;
            if (!job->gotDone(got, put)) {
                job->done = true;
                return;
            }
            send(put, [job](const http::Response& r) {
                job->putDone(r);
                job->done = true;
            });
        });
    };
    if (!job->needsAuth()) {
        sync();
        return;
    }
    send(job->authRequest(), [job, sync](const http::Response& r) {
        if (job->authDone(r)) sync();
        else job->done = true;
    });
}

// The website's study page only opens while signed in: once the sign-in is gone,
// the page (web/pre.js) saves that and goes back to the login page.
EM_JS(void, pageSignedOut, (), {
    if (Module.cadetSignedOut) Module.cadetSignedOut();
});
#endif

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
#if defined(__EMSCRIPTEN__)
        pageSignedOut();
#endif
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
#if defined(__EMSCRIPTEN__)
    Job::runAsync(job);  // no threads in a browser: each answer comes back as an event
#else
    // The thread keeps its own reference, so quitting mid-request is safe
    std::thread([job] { job->run(); }).detach();
#endif
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
