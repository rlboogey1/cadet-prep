#include "AtomicFile.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <system_error>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#define ATOMICFILE_FSYNC 1
#endif

namespace atomicfile {

namespace fs = std::filesystem;

namespace {

std::mutex gMutex;
std::vector<std::string> gNotices;
std::set<std::string> gReported;  // set-aside names already announced this run
std::set<std::string> gFailing;   // paths whose last save failed (one notice until a save works)

// What the file holds, in words a person would use.
std::string kindOf(const std::string& name) {
    static const std::pair<const char*, const char*> kinds[] = {
        {"afoqt_progress.json", "progress"},
        {"afoqt_flashcards.json", "vocabulary card"},
        {"warrior_cards_progress.json", "Warrior Knowledge card"},
        {"afoqt_repetition.json", "spaced repetition deck"},
        {"warrior_repetition.json", "Warrior Knowledge deck"},
        {"afoqt_roots.json", "word roots"},
        {"afoqt_roots_repetition.json", "word roots deck"},
        {"afoqt_saved_questions.json", "saved questions"},
        {"afoqt_misses.json", "weak spots"},
        {"afoqt_weekly.json", "weekly reading"},
        {"afoqt_settings.json", "settings"},
    };
    for (const auto& [file, kind] : kinds)
        if (name == file) return kind;
    if (name.find("account") != std::string::npos) return "account";
    return "saved progress";
}

std::string fileName(const std::string& path) { return fs::path(path).filename().string(); }

bool sameContents(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    return fa && fb &&
           std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(fb));
}

}  // namespace

bool write(const std::string& path, const std::string& contents) {
    const std::string tmp = path + ".tmp";
    bool ok = false;
    if (std::FILE* f = std::fopen(tmp.c_str(), "wb")) {
        ok = std::fwrite(contents.data(), 1, contents.size(), f) == contents.size();
        ok = std::fflush(f) == 0 && ok;
#ifdef ATOMICFILE_FSYNC
        ok = ::fsync(::fileno(f)) == 0 && ok;  // on disk before the rename makes it the real file
#endif
        ok = std::fclose(f) == 0 && ok;
    }
    std::error_code ec;
    if (ok) {
        fs::rename(tmp, path, ec);  // replaces the old file in one step
        ok = !ec;
    }
    if (!ok) fs::remove(tmp, ec);

    std::lock_guard<std::mutex> lock(gMutex);
    if (ok) {
        gFailing.erase(path);
    } else if (gFailing.insert(path).second) {
        gNotices.push_back("Couldn't save your " + kindOf(fileName(path)) +
                           " file. Your latest changes are still in the app, and it will try again on the next save.");
    }
    return ok;
}

std::string setAside(const std::string& path) {
    const fs::path src(path);
    const std::string kind = kindOf(fileName(path));
    std::error_code ec;

    // The same damaged file already set aside (a previous run, or loaded twice before
    // anything saved)? Reuse that copy instead of piling up new ones.
    const std::string prefix = src.filename().string() + ".corrupt-";
    std::string name;
    for (fs::directory_iterator it(src.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string f = it->path().filename().string();
        if (f.compare(0, prefix.size(), prefix) == 0 &&
            f.compare(f.size() - 5, 5, ".part") != 0 && sameContents(it->path(), src)) {
            name = f;
            break;
        }
    }

    bool ok = !name.empty();
    if (!ok) {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        fs::path dest = src.parent_path() / (prefix + stamp);
        for (int n = 2; fs::exists(dest, ec) && n < 100; ++n)  // another damaged copy from this same second
            dest = src.parent_path() / (prefix + stamp + "-" + std::to_string(n));

        // Copy to a temp name and rename, so a half-copied file never looks like a set-aside one
        const fs::path part = dest.string() + ".part";
        ok = fs::copy_file(src, part, fs::copy_options::none, ec);
        if (ok) {
            fs::rename(part, dest, ec);
            ok = !ec;
        }
        if (!ok) fs::remove(part, ec);
        if (ok) name = dest.filename().string();
    }

    std::lock_guard<std::mutex> lock(gMutex);
    if (!ok) {
        name.clear();
        if (gReported.insert(path).second)
            gNotices.push_back("Your " + kind + " file was damaged and couldn't be set aside. "
                               "It will be replaced the next time you save.");
    } else if (gReported.insert(name).second) {
        gNotices.push_back("Your " + kind + " file was damaged, so it was set aside as " + name + ".");
    }
    return name;
}

std::vector<std::string> takeNotices() {
    std::lock_guard<std::mutex> lock(gMutex);
    std::vector<std::string> out;
    out.swap(gNotices);
    return out;
}

}  // namespace atomicfile
