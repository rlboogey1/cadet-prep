#pragma once
// Finds the question data and picks a place to save progress.
//
// An app opened from Finder or the Dock doesn't start in your project folder,
// so relative paths like "data" or "afoqt_progress.json" would point nowhere.
// These helpers make both apps work no matter how they're launched.
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef AFOQT_DATA_DIR
#define AFOQT_DATA_DIR "data"
#endif

namespace apppaths {

namespace fs = std::filesystem;

// Where progress files live:
//   macOS:  ~/Library/Application Support/Cadet Prep
//   other:  ~/.cadet-prep
// `oldName` gives the folder used before the app was renamed from "AFOQT Study".
inline fs::path userDataDir(bool oldName = false) {
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::current_path();
#ifdef __APPLE__
    return base / "Library" / "Application Support" / (oldName ? "AFOQT Study" : "Cadet Prep");
#else
    return base / (oldName ? ".afoqt-study" : ".cadet-prep");
#endif
}

// Creates the progress folder, copies over progress saved by older versions
// (which wrote it next to the project), and makes it the working directory so
// "afoqt_progress.json" and "afoqt_flashcards.json" are saved there.
inline void useUserDataDir() {
    std::error_code ec;
    const fs::path dir = userDataDir();
    // Renamed app: move the old progress folder over, the first time only
    const fs::path old = userDataDir(true);
    if (!fs::exists(dir, ec) && fs::exists(old, ec)) fs::rename(old, dir, ec);
    fs::create_directories(dir, ec);

    const fs::path projectDir = fs::path(AFOQT_DATA_DIR).parent_path();
    for (const char* name : {"afoqt_progress.json", "afoqt_flashcards.json"}) {
        if (!fs::exists(dir / name, ec) && fs::exists(projectDir / name, ec))
            fs::copy_file(projectDir / name, dir / name, ec);
    }
    fs::current_path(dir, ec);
}

// Finds the question folder, in this order:
//   1. a folder given on the command line
//   2. the copy inside the .app bundle (Cadet Prep.app/Contents/Resources/data)
//   3. the project's data/ folder
// Returns an absolute path, since useUserDataDir() changes the working directory.
inline std::string findDataDir(int argc, char* argv[]) {
    std::error_code ec;
    if (argc > 1) return fs::absolute(argv[1], ec).string();

    if (argc > 0) {
        fs::path exe = fs::weakly_canonical(fs::absolute(argv[0], ec), ec);
        fs::path bundled = exe.parent_path().parent_path() / "Resources" / "data";
        if (fs::exists(bundled / "subtests.json", ec)) return bundled.string();
    }
    return AFOQT_DATA_DIR;
}

}  // namespace apppaths
