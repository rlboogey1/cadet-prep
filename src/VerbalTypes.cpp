#include "VerbalTypes.h"
#include <cctype>

namespace verbal {

const std::vector<TypeInfo>& relationTypes() {
    static const std::vector<TypeInfo> list = {
        {"synonym", "Synonyms", "The two words mean the same thing: ADVERSARY : FOE."},
        {"antonym", "Antonyms", "The two words mean opposite things: ARID : MOIST."},
        {"degree", "Degree", "The same idea, one much stronger or bigger: WHISPER : SHOUT."},
        {"part_whole", "Part and whole", "One is a piece or smaller unit of the other: PETAL : FLOWER."},
        {"group_member", "Group and member", "One is a group made of the other: FLOCK : SHEEP."},
        {"type_category", "Type and category", "One is a kind or unit of the other: MILE : DISTANCE."},
        {"function", "Function", "A thing and what it does or is used for: COMPASS : DIRECTION."},
        {"worker_tool", "Worker and tool", "A person and the tool of their trade: SURGEON : SCALPEL."},
        {"worker_creation", "Creator and creation", "A person and what they make: ARCHITECT : BLUEPRINT."},
        {"worker_work", "Person and what they do", "A person and what they act on, lead, or study: JUDGE : SENTENCE."},
        {"place", "Place", "Where something lives, works, or is kept: HIVE : BEE."},
        {"cause_effect", "Cause and effect", "One brings about or answers the other: COLD : SHIVER."},
        {"lack", "Lack", "One is the absence of the other: DROUGHT : WATER."},
        {"characteristic", "Characteristic", "A defining trait, or what it is made of: CANDLE : WAX."},
        {"sequence", "Sequence", "One comes before or grows into the other: TADPOLE : FROG."},
    };
    return list;
}

const std::vector<TypeInfo>& readingTypes() {
    static const std::vector<TypeInfo> list = {
        {"main_idea", "Main idea", "What the whole passage is about, or its best title."},
        {"detail", "Stated detail", "A fact the passage says outright. Find the sentence."},
        {"inference", "Inference", "What the passage suggests or supports without saying it."},
        {"vocab", "Vocabulary in context", "What a word means in that sentence, not in general."},
        {"purpose", "Purpose and tone", "Why the author includes a detail, and how they sound."},
    };
    return list;
}

const std::vector<TypeInfo>& typesFor(const std::string& subtestId) {
    static const std::vector<TypeInfo> none;
    if (subtestId == "verbal_analogies") return relationTypes();
    if (subtestId == "reading_comprehension") return readingTypes();
    return none;
}

const TypeInfo* findType(const std::string& subtestId, const std::string& typeId) {
    for (const auto& t : typesFor(subtestId))
        if (typeId == t.id) return &t;
    return nullptr;
}

std::pair<std::string, std::string> firstPair(const std::string& prompt) {
    const std::string isTo = " is to ";
    const size_t i = prompt.find(isTo);
    if (i == std::string::npos) return {};
    std::string rest = prompt.substr(i + isTo.size());
    const size_t as = rest.find(" as");
    if (as != std::string::npos) rest = rest.substr(0, as);
    return {prompt.substr(0, i), rest};
}

std::string wordKnowledgeWord(const std::string& prompt) {
    const size_t i = prompt.find(" most nearly means");
    if (i == std::string::npos) return "";
    std::string w = prompt.substr(0, i);
    for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return w;
}

}  // namespace verbal
