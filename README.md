# AFOQT Study (C++)

A console study app for the AFOQT knowledge sections: Word Knowledge, Math Knowledge,
Arithmetic Reasoning, Verbal Analogies, and Reading Comprehension. It's built so pilot
sections can be added later without restructuring.

## Build and run

Requires CMake 3.16+ and a C++17 compiler (g++, clang, or Visual Studio 2019+).
The JSON library (nlohmann/json) downloads automatically the first time you configure.

```bash
cmake -S . -B build
cmake --build build
./build/afoqt_gui        # window version
./build/afoqt            # console version
```

The app reads questions from the project's `data/` folder, so you can edit question
files and rerun without rebuilding. To use a different folder: `./build/afoqt path/to/data`.

Progress is saved to `afoqt_progress.json` (tests) and `afoqt_flashcards.json` (flashcards)
in the folder you run it from.

## Features

- **Practice mode**: untimed, explanation after every question
- **Timed test**: countdown per section, review of misses at the end
- **Full battery**: all knowledge sections back to back, timed
- **Review missed**: re-asks every question you've gotten wrong until you get it right
- **Progress**: accuracy and seconds per question by section, plus your weakest area
- **Word Knowledge flashcards**: 161 words with definitions, synonyms, and example
  sentences. Uses spaced repetition: words you miss come back more often, and a word
  counts as mastered after 3 correct in a row. Keys: Space flips, 1 = still learning,
  2 = got it. You can also show the definition first and guess the word.

## Question bank

**Knowledge sections**

| Section | Written questions | Plus |
|---|---|---|
| Word Knowledge | 233 | 233 flashcards |
| Math Knowledge | 60 | unlimited generated questions |
| Arithmetic Reasoning | 50 | unlimited generated questions |
| Verbal Analogies | 63 | |
| Reading Comprehension | 33 (11 passages) | |

**Pilot sections** (official question counts and time limits)

| Section | Questions | Timed test |
|---|---|---|
| Table Reading | unlimited, a new 21 x 21 table each session | 40 in 7 min |
| Instrument Comprehension | unlimited: artificial horizon + compass | 25 in 5 min |
| Aviation Information | 44 written | 20 in 8 min |
| Block Counting | unlimited 3D block stacks | 30 in 5 min |

The Pilot composite score uses Math Knowledge, Table Reading, Instrument
Comprehension, and Aviation Information. Block Counting counts toward other
composites (CSO and ABM). Sections with pictures only appear in the window
version, not the console version. The real Table Reading test uses a larger
table (X and Y from -15 to +15); this app uses -10 to +10 so it fits on screen,
and Instrument Comprehension answers are written descriptions rather than the
aircraft pictures on the real test.

Everything is randomized:
- **Question order:** each test draws questions in a random order.
- **Answer order:** the choices are shuffled every time a question is shown,
  so the correct answer can land on any letter.
- **Generated questions** (`src/QuestionGenerator.cpp`): Math Knowledge and
  Arithmetic Reasoning mix in brand-new questions with random numbers, about a
  third of a timed test and 20 extra in practice. The wrong choices come from
  common mistakes. There are 12 question types for each section; add a function
  there and list it in `templatesFor()` to add more. Generated questions don't
  go into "Review missed", since they aren't saved anywhere.

## Adding flashcards

Add an entry to `data/flashcards.json`:

```json
{ "word": "vigilant", "pos": "adj", "definition": "Alert and watching for danger.",
  "synonym": "watchful", "example": "Guards remained vigilant all night." }
```

## Logo

`assets/` has the F-22 logo as `logo.svg` (vector), `logo_1024.png`, and `AppIcon.icns`
(the Mac app icon, built into the app automatically).

## Project layout

```
data/
  subtests.json              list of sections, time limits, tips
  word_knowledge.json        one question file per section
  ...
src/
  Question.h                 Question and AnswerRecord structs
  QuizEngine.*               step-by-step quiz logic used by the GUI
  QuestionGenerator.*        random math questions and the pilot sections
  Visuals.*                  tables, cockpit dials, and block stacks
  Sound.*                    sound effects (synthesized, using miniaudio)
  JetShape.h                 the F-22 outline used by the logo
  FlashcardDeck.*            flashcards and spaced repetition
  GuiApp.*, gui_main.cpp     the window version (Dear ImGui)
  QuestionBank.*             loads JSON, validates, shuffles
  SubtestRegistry.*          reads subtests.json
  TestSession.*              runs a set of questions and the timer
  ProgressStore.*            saves history and missed questions
  UI.h                       abstract interface for all input/output
  ConsoleUI.*                console implementation of UI
  main.cpp                   menus
```

`TestSession` only talks to the `UI` interface, never to `std::cout` directly. When you
want a graphical version, write an `ImGuiUI` or `QtUI` class that implements `UI` and
the rest of the code stays the same.

## Adding questions

Add an object to the relevant file in `data/`:

```json
{ "id": "WK-011", "prompt": "VIGILANT most nearly means:",
  "choices": ["watchful", "violent", "weary", "vague", "valid"], "answer": 0,
  "explanation": "Vigilant means keeping careful watch for danger." }
```

- `answer` is the **0-based** index of the correct choice (0 = A, 1 = B, ...).
- IDs must be unique; they're how missed questions are tracked.
- Reading Comprehension questions also have a `"passage"` field.
- The app checks every file on startup and reports bad answer indexes.

Write your own questions or use openly licensed material. Don't copy from commercial
prep books, especially if you'll share the app.

## Adding pilot sections later

**Multiple-choice pilot sections** (Instrument Comprehension, Aviation Information,
etc.): add an entry to `data/subtests.json` with `"category": "pilot"` and create its
question file. No code changes needed. For Instrument Comprehension, you'll add an
`"image"` field to `Question` and display it once you move to a GUI.

**Generated sections** (Table Reading, arithmetic drills): write a class that builds
`Question` objects in code instead of loading them from JSON, and hand them to
`TestSession` the same way. You get unlimited fresh questions.

## Time limits

The limits in `subtests.json` are practice pacing, not official values. Check the
current official AFOQT study guide and adjust `time_limit_seconds` and
`question_count` to match.
