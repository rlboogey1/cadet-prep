#pragma once
// Online sync: one Cadet Prep account (Supabase) that keeps practice history,
// missed questions, and flashcard boxes the same on the Mac, iPhone, and iPad.
//
// How it works: the progress files (cloudsync::syncedFiles()) are bundled into one
// JSON document stored online. A sync downloads it, merges it with this device's
// files (SyncMerge.h), uploads the result, and writes it back here. It runs when
// the app opens or comes back to the front, a few seconds after any progress
// file changes, and every 10 minutes while open.
//
// Network calls run on a background thread; files are only read and written on
// the main thread, in update(), so saving progress never races a sync.
//
// The server address and public key come from supabase.json at the project root
// (see SYNC.md), baked in at build time. Without them the account section says
// sync isn't set up and nothing else changes.
#include <nlohmann/json.hpp>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class CloudSync {
public:
    void load();  // reads the saved sign-in (afoqt_account.json)

    bool configured() const;  // this build has a server address
    bool signedIn() const { return !account_.refreshToken.empty(); }
    const std::string& email() const { return account_.email; }
    bool busy() const { return job_ != nullptr; }
    std::string status() const;  // "Syncing...", "Synced at 2:41 PM", or the last error
    bool failed() const { return !busy() && !error_.empty(); }  // status() is an error

    void signIn(const std::string& email, const std::string& password);
    void signOut();  // forgets the sign-in; progress on this device stays
    void syncNow();  // runs now, or right after the current sync
    void onForeground() { syncNow(); }

    // The website's Download section: asks the server for a link to the Mac app
    // that works for a few minutes (the file is private; only a signed-in account
    // can get a link). The link arrives a moment later through takeDownloadUrl().
    void requestDownload();
    bool downloadBusy() const { return wantDownload_ || downloading_; }
    std::string takeDownloadUrl();  // the link, once; empty until it's ready
    const std::string& downloadError() const { return downloadError_; }

    // Call every frame. Returns true when progress files were rewritten with
    // merged progress, so the app should reload them.
    bool update();

private:
    struct Account {
        std::string email, userId, accessToken, refreshToken;
        long long expiresAt = 0;
    };
    struct Job;  // one background sign-in or sync

    void start(std::shared_ptr<Job> job);
    bool apply(Job& job);  // main thread: use a finished job's results
    void saveAccount() const;
    nlohmann::json readLocal() const;  // the synced files on this device, as a bundle
    void rememberFileTimes();
    bool filesChanged() const;

    Account account_;
    std::shared_ptr<Job> job_;
    bool pending_ = false;  // another sync was asked for while one was running
    bool wantDownload_ = false, downloading_ = false;
    std::string downloadUrl_, downloadError_;
    std::string error_;
    std::time_t lastSync_ = 0, lastAttempt_ = 0;
    double nextCheck_ = 0, changedAt_ = -1;
    std::map<std::string, std::filesystem::file_time_type> fileTimes_;
};
