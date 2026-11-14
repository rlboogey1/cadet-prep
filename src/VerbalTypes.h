#pragma once
#include <string>
#include <utility>
#include <vector>

// Question types for the verbal sections. Every Verbal Analogies question names its
// relationship and every Reading Comprehension question its question type, in the
// question's "type" field. The relationship drill and the Weak spots page use them.
namespace verbal {

struct TypeInfo {
    const char* id;    // as written in the question files
    const char* name;  // shown in the app
    const char* hint;  // one line: how to spot it, with an example
};

const std::vector<TypeInfo>& relationTypes();  // Verbal Analogies
const std::vector<TypeInfo>& readingTypes();   // Reading Comprehension

// The list a section's "type" field must come from (empty = the section has none).
const std::vector<TypeInfo>& typesFor(const std::string& subtestId);
const TypeInfo* findType(const std::string& subtestId, const std::string& typeId);

// "PILOT is to AIRCRAFT as CAPTAIN is to" -> {"PILOT", "AIRCRAFT"}; empty if the
// prompt isn't an analogy.
std::pair<std::string, std::string> firstPair(const std::string& prompt);

// "CANDID most nearly means:" -> "candid" (lowercase); empty if it isn't that form.
std::string wordKnowledgeWord(const std::string& prompt);

}  // namespace verbal
