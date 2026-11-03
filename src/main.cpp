#include "ConsoleUI.h"
#include "ProgressStore.h"
#include "QuestionBank.h"
#include "SubtestRegistry.h"
#include "TestSession.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <iostream>
#include <random>
#include <sstream>

#ifndef AFOQT_DATA_DIR
#define AFOQT_DATA_DIR "data"
#endif

namespace {

struct App {
    ConsoleUI ui;
    SubtestRegistry registry;
    QuestionBank bank;
    ProgressStore progress{"afoqt_progress.json"};
    std::mt19937 rng{std::random_device{}()};

    // Lets the user pick any subtest from the config file. Returns nullptr on "Back".
    const SubtestInfo* pickSubtest() {
        std::vector<std::string> names;
        for (const auto& s : registry.all())
            names.push_back(s.name + "  (" + std::to_string(bank.size(s.id)) + " questions)");
        names.push_back("Back");
        int choice = ui.menu("Choose a subtest", names);
        if (choice == static_cast<int>(registry.all().size())) return nullptr;
        return &registry.all()[choice];
    }

    void showSummary(const SubtestInfo& info, const SessionResult& r) {
        std::ostringstream os;
        int total = static_cast<int>(r.answers.size());
        double pct = total ? 100.0 * r.correctCount() / total : 0.0;
        os << "\n*** " << info.name << " results ***\n"
           << "  Score:     " << r.correctCount() << " / " << total << "  ("
           << std::fixed << std::setprecision(0) << pct << "%)\n"
           << "  Answered:  " << r.answeredCount() << " / " << total << "\n"
           << "  Time used: " << static_cast<int>(r.totalSeconds) << " s";
        if (r.answeredCount() > 0)
            os << "  (" << std::setprecision(1) << r.totalSeconds / r.answeredCount()
               << " s per answered question)";
        if (r.timeExpired) os << "\n  Time expired before you finished.";
        ui.showMessage(os.str());
    }

    void runSubtest(const SubtestInfo& info, SessionMode mode) {
        int count = mode == SessionMode::Timed ? info.questionCount : 0;
        auto questions = bank.draw(info.id, count, rng);
        if (questions.empty()) {
            ui.showMessage("No questions loaded for " + info.name + ".");
            return;
        }

        std::ostringstream intro;
        intro << "\n" << info.name << " -- "
              << (mode == SessionMode::Timed ? "TIMED TEST" : "PRACTICE (untimed)") << ", "
              << questions.size() << " questions";
        if (mode == SessionMode::Timed && info.timeLimitSeconds > 0)
            intro << ", " << info.timeLimitSeconds / 60 << " min "
                  << info.timeLimitSeconds % 60 << " s";
        if (!info.tips.empty()) intro << "\nTip: " << info.tips;
        ui.showMessage(intro.str());
        ui.pause();

        TestSession session(ui, info.id, std::move(questions), mode, info.timeLimitSeconds);
        SessionResult result = session.run();
        progress.record(result);
        showSummary(info, result);
        ui.pause();
    }

    void fullBattery() {
        auto sections = registry.inCategory("knowledge");
        ui.showMessage("\nFull knowledge battery: " + std::to_string(sections.size()) +
                       " timed sections back to back.");
        for (const auto& s : sections) runSubtest(s, SessionMode::Timed);
    }

    void reviewMissed() {
        auto questions = bank.byIds(progress.missedQuestionIds());
        if (questions.empty()) {
            ui.showMessage("\nNo missed questions to review. Nice work!");
            return;
        }
        std::shuffle(questions.begin(), questions.end(), rng);
        ui.showMessage("\nReviewing " + std::to_string(questions.size()) +
                       " previously missed questions. Answer correctly to clear them.");

        // Group by subtest so stats stay per-section.
        std::map<std::string, std::vector<Question>> groups;
        for (auto& q : questions) groups[q.subtest].push_back(q);
        for (auto& [id, qs] : groups) {
            TestSession session(ui, id, qs, SessionMode::Practice, 0);
            progress.record(session.run(), /*isReview=*/true);
        }
        ui.pause();
    }

    void showProgress() {
        auto stats = progress.stats();
        if (stats.empty()) {
            ui.showMessage("\nNo sessions yet. Take a practice set first.");
            return;
        }
        std::ostringstream os;
        os << "\n" << std::left << std::setw(24) << "Subtest" << std::right
           << std::setw(9) << "Sessions" << std::setw(10) << "Accuracy"
           << std::setw(12) << "Sec/quest" << "\n";
        os << std::string(55, '-') << "\n";

        std::string weakest;
        double weakestPct = 101.0;
        for (const auto& s : registry.all()) {
            auto it = stats.find(s.id);
            if (it == stats.end()) continue;
            const auto& st = it->second;
            os << std::left << std::setw(24) << s.name << std::right << std::setw(9)
               << st.sessions << std::setw(9) << std::fixed << std::setprecision(0)
               << st.accuracy() << "%" << std::setw(12) << std::setprecision(1)
               << st.avgSecondsPerQuestion() << "\n";
            if (st.accuracy() < weakestPct) {
                weakestPct = st.accuracy();
                weakest = s.name;
            }
        }
        os << "\nWeakest area: " << weakest << "\n"
           << "Questions waiting in review: " << progress.missedQuestionIds().size();
        ui.showMessage(os.str());
        ui.pause();
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    std::string dataDir = argc > 1 ? argv[1] : AFOQT_DATA_DIR;
    App app;

    try {
        app.registry.load(dataDir + "/subtests.json");
        for (const auto& s : app.registry.all())
            app.bank.loadFile(dataDir + "/" + s.file, s.id);
    } catch (const std::exception& e) {
        std::cerr << "Startup error: " << e.what() << "\n"
                  << "Usage: afoqt [path/to/data]\n";
        return 1;
    }

    app.ui.showMessage("AFOQT Study -- knowledge sections");
    while (true) {
        int choice = app.ui.menu("Main menu", {
            "Practice a subtest (untimed, instant feedback)",
            "Timed test (one subtest)",
            "Full knowledge battery (all sections, timed)",
            "Review missed questions",
            "View progress",
            "Quit",
        });
        switch (choice) {
            case 0: if (auto s = app.pickSubtest()) app.runSubtest(*s, SessionMode::Practice); break;
            case 1: if (auto s = app.pickSubtest()) app.runSubtest(*s, SessionMode::Timed); break;
            case 2: app.fullBattery(); break;
            case 3: app.reviewMissed(); break;
            case 4: app.showProgress(); break;
            default: return 0;
        }
    }
}
