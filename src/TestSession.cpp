#include "TestSession.h"
#include <chrono>

using Clock = std::chrono::steady_clock;

int SessionResult::correctCount() const {
    int n = 0;
    for (const auto& a : answers) n += a.correct ? 1 : 0;
    return n;
}

int SessionResult::answeredCount() const {
    int n = 0;
    for (const auto& a : answers) n += a.chosenIndex >= 0 ? 1 : 0;
    return n;
}

TestSession::TestSession(UI& ui, std::string subtestId, std::vector<Question> questions,
                         SessionMode mode, int timeLimitSeconds)
    : ui_(ui),
      subtestId_(std::move(subtestId)),
      questions_(std::move(questions)),
      mode_(mode),
      timeLimitSeconds_(timeLimitSeconds) {}

SessionResult TestSession::run() {
    SessionResult result;
    result.subtestId = subtestId_;

    const bool timed = mode_ == SessionMode::Timed && timeLimitSeconds_ > 0;
    const auto start = Clock::now();
    const int total = static_cast<int>(questions_.size());

    for (int i = 0; i < total; ++i) {
        const Question& q = questions_[i];

        int secondsLeft = -1;
        if (timed) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start);
            secondsLeft = timeLimitSeconds_ - static_cast<int>(elapsed.count());
            if (secondsLeft <= 0) {
                // Out of time: everything left counts as unanswered.
                result.timeExpired = true;
                for (int j = i; j < total; ++j)
                    result.answers.push_back({questions_[j].id, -1, false, 0.0});
                break;
            }
        }

        auto qStart = Clock::now();
        int chosen = ui_.askQuestion(q, i + 1, total, secondsLeft);
        double secs = std::chrono::duration<double>(Clock::now() - qStart).count();

        // A console can't interrupt typing, so an answer submitted after the
        // clock ran out doesn't count.
        if (timed) {
            double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            if (elapsed > timeLimitSeconds_) {
                chosen = -1;
                ui_.showMessage("  (Time expired before this answer was entered.)");
            }
        }

        result.answers.push_back({q.id, chosen, chosen == q.correctIndex, secs});

        if (mode_ == SessionMode::Practice) ui_.showFeedback(q, chosen);
    }

    result.totalSeconds = std::chrono::duration<double>(Clock::now() - start).count();

    // Timed mode: review all misses at the end.
    if (mode_ == SessionMode::Timed) {
        bool anyMissed = false;
        for (size_t i = 0; i < result.answers.size(); ++i) {
            if (!result.answers[i].correct) {
                if (!anyMissed) ui_.showMessage("\n===== Review of missed questions =====");
                anyMissed = true;
                ui_.showMessage("\n" + questions_[i].prompt);
                ui_.showFeedback(questions_[i], result.answers[i].chosenIndex);
            }
        }
    }
    return result;
}
