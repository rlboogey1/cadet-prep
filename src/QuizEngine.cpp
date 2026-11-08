#include "QuizEngine.h"

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
    start_ = questionStart_ = Clock::now();
}

int QuizEngine::secondsLeft() const {
    if (mode_ != SessionMode::Timed || timeLimitSeconds_ <= 0) return -1;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start_);
    int left = timeLimitSeconds_ - static_cast<int>(elapsed.count());
    return left > 0 ? left : 0;
}

void QuizEngine::update() {
    if (finished_ || secondsLeft() != 0) return;
    // Out of time: every remaining question counts as unanswered.
    for (int j = index_; j < total(); ++j)
        result_.answers.push_back({questions_[j].id, -1, false, 0.0});
    result_.timeExpired = true;
    finish();
}

void QuizEngine::submit(int choice) {
    if (finished_ || showingFeedback_) return;
    const Question& q = questions_[index_];
    double secs = std::chrono::duration<double>(Clock::now() - questionStart_).count();
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
    ++index_;
    questionStart_ = Clock::now();
    if (index_ >= total()) finish();
}

void QuizEngine::finish() {
    finished_ = true;
    result_.totalSeconds = std::chrono::duration<double>(Clock::now() - start_).count();
}
