#pragma once
// Combines the progress saved on two devices (online sync).
//
// Each merge takes two copies of the same save file, as JSON, and returns one
// that keeps everything from both: no session is counted twice, and for each
// question or word the newer result wins. Merging is safe to repeat: merging a
// result again with either side changes nothing.
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace cloudsync {

using json = nlohmann::json;

long long now();            // seconds since 1970
std::string newId();        // random id for a practice session
std::string sessionKey(const json& session);  // its id, or one made from its fields (older saves)

json mergeProgress(const json& a, const json& b);    // afoqt_progress.json
json mergeBoxes(const json& a, const json& b);       // afoqt_flashcards.json, warrior_cards_progress.json
json mergeRepetition(const json& a, const json& b);  // afoqt_repetition.json

// The files that sync, as {bundle key, file name}. Settings (dark mode, sound)
// stay per device on purpose.
const std::vector<std::pair<std::string, std::string>>& syncedFiles();

// A bundle is {"<key>": <file contents>, ...} for the files above; a missing
// key means that device has no such file yet.
json mergeBundle(const json& a, const json& b);

}  // namespace cloudsync
