#include "QuizEngine.h"
#include <algorithm>

void QuizEngine::start(std::string subtestId, std::vector<Question> questions,
                       SessionMode mode, int timeLimitSeconds) {
    questions_ = std::move(questions);
    mode_ = mode;
    timeLimitSeconds_ = timeLimitSeconds;
    result_ = SessionResult{};
    result_.subtestId = std::move(subtestId);
    index_ = 0;
    lastChoice_ = -1;
    showingFeedback_ = false;
    finished_ = questions_.empty();
    active_ = questionStart_ = 0.0;
    lastTick_ = Clock::now();
}

void QuizEngine::tick() {
    const Clock::time_point now = Clock::now();
    const double gap = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;
    if (!paused_ && !finished_) active_ += std::clamp(gap, 0.0, kMaxStep);
}

void QuizEngine::setPaused(bool paused) {
    tick();  // count the time up to now with the old state
    paused_ = paused;
}

int QuizEngine::secondsLeft() const {
    if (mode_ != SessionMode::Timed || timeLimitSeconds_ <= 0) return -1;
    const int left = timeLimitSeconds_ - static_cast<int>(active_);
    return left > 0 ? left : 0;
}

void QuizEngine::update() {
    tick();
    if (finished_ || secondsLeft() != 0) return;
    // Out of time: every remaining question counts as unanswered.
    for (int j = index_; j < total(); ++j)
        result_.answers.push_back({questions_[j].id, -1, false, 0.0});
    result_.timeExpired = true;
    finish();
}

void QuizEngine::submit(int choice) {
    if (finished_ || showingFeedback_) return;
    tick();
    const Question& q = questions_[index_];
    const double secs = active_ - questionStart_;
    result_.answers.push_back({q.id, choice, choice == q.correctIndex, secs});
    lastChoice_ = choice;

    if (mode_ == SessionMode::Practice) showingFeedback_ = true;
    else advance();
}

void QuizEngine::next() {
    if (!showingFeedback_) return;
    showingFeedback_ = false;
    advance();
}

void QuizEngine::append(std::vector<Question> more) {
    for (auto& q : more) questions_.push_back(std::move(q));
}

void QuizEngine::finishNow() {
    if (!finished_) finish();
}

void QuizEngine::advance() {
    tick();
    ++index_;
    questionStart_ = active_;
    if (index_ >= total()) finish();
}

void QuizEngine::finish() {
    tick();
    finished_ = true;
    result_.totalSeconds = active_;
}
