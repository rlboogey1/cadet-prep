#include "AppPaths.h"
#include "ConsoleUI.h"
#include "FlashcardDeck.h"
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
    FlashcardDeck deck;
    std::mt19937 rng{std::random_device{}()};

    static std::string readKey() {
        std::string line;
        if (!std::getline(std::cin, line)) return "q";
        return line;
    }

    void flashcards() {
        std::ostringstream intro;
        intro << "\nMastered " << deck.masteredCount() << " of " << deck.size() << " words.";
        ui.showMessage(intro.str());
        int choice = ui.menu("Word Knowledge flashcards",
                             {"Study 20 words", "Study words I'm still learning",
                              "Study the whole deck", "Back"});
        if (choice == 3) return;
        deck.startSession(choice == 0 ? 20 : choice == 1 ? 30 : 0, choice == 1, rng);
        if (deck.sessionDone()) {
            ui.showMessage("No words to study. You've mastered them all!");
            return;
        }

        while (!deck.sessionDone()) {
            const Flashcard& c = deck.current();
            std::cout << "\n----------------------------------------------------------\n"
                      << deck.remaining() << " cards left\n\n   " << c.word << "  ("
                      << c.partOfSpeech << ")\n\nPress Enter to flip (q to stop)... ";
            if (readKey() == "q") return;
            std::cout << "\n   " << c.definition << "\n   Synonym: " << c.synonym << "\n";
            if (!c.example.empty()) std::cout << "   \"" << c.example << "\"\n";

            std::string k;
            do {
                std::cout << "\n1 = still learning, 2 = got it, q = stop: ";
                k = readKey();
            } while (k != "1" && k != "2" && k != "q");
            if (k == "q") return;
            deck.mark(k == "2");
        }
        std::ostringstream done;
        done << "\nSession complete. Knew it: " << deck.sessionGotIt()
             << ", still learning: " << deck.sessionMissed() << ". Mastered overall: "
             << deck.masteredCount() << " of " << deck.size() << ".";
        ui.showMessage(done.str());
        ui.pause();
    }

    // Lets the user pick any subtest from the config file. Returns nullptr on "Back".
    // Sections with pictures (Table Reading, Instrument Comprehension, Block
    // Counting) are only in the window version.
    const SubtestInfo* pickSubtest() {
        std::vector<const SubtestInfo*> shown;
        std::vector<std::string> names;
        for (const auto& s : registry.all()) {
            if (s.visual) continue;
            shown.push_back(&s);
            names.push_back(s.name + "  (" + std::to_string(bank.size(s.id)) + " questions)");
        }
        names.push_back("Back");
        int choice = ui.menu("Choose a subtest", names);
        if (choice == static_cast<int>(shown.size())) return nullptr;
        return shown[choice];
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
        auto questions = bank.drawByIds(progress.missedQuestionIds(), rng);
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
    // Find the questions, then switch to the folder where progress is saved.
    const std::string dataDir = apppaths::findDataDir(argc, argv);
    apppaths::useUserDataDir();
    App app;

    try {
        app.registry.load(dataDir + "/subtests.json");
        for (const auto& s : app.registry.all())
            if (!s.file.empty()) app.bank.loadFile(dataDir + "/" + s.file, s.id);
        app.deck.load(dataDir + "/flashcards.json");
        app.deck.loadProgress("afoqt_flashcards.json");
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
            "Word Knowledge flashcards",
            "Review missed questions",
            "View progress",
            "Quit",
        });
        switch (choice) {
            case 0: if (auto s = app.pickSubtest()) app.runSubtest(*s, SessionMode::Practice); break;
            case 1: if (auto s = app.pickSubtest()) app.runSubtest(*s, SessionMode::Timed); break;
            case 2: app.fullBattery(); break;
            case 3: app.flashcards(); break;
            case 4: app.reviewMissed(); break;
            case 5: app.showProgress(); break;
            default: return 0;
        }
    }
}
