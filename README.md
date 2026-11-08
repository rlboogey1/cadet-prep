# Cadet Prep

Prep for AFROTC cadets, written in C++, with a Mac window version and a console version.
The app is split into prep sections, which you pick from the main menu or the sidebar:

- **AFOQT Prep**: the AFOQT knowledge sections (Word Knowledge, Math Knowledge,
  Arithmetic Reasoning, Verbal Analogies, Reading Comprehension) and pilot sections
  (Table Reading, Instrument Comprehension, Aviation Information, Block Counting), with
  flashcards and estimated composite scores.
- **Warrior Knowledge**: Blocks 1 to 11 of the Det 105 Warrior Knowledge handbook,
  as flashcards.

The app opens on a main menu where you pick a prep section. Each prep section has its
own dashboard, practice tests, review list, cards, progress, and focus area.

> This project was built with [Claude Code](https://claude.com/claude-code), Anthropic's
> AI coding assistant.

## Build and run

Requires CMake 3.16+ and a C++17 compiler (g++, clang, or Visual Studio 2019+).
The JSON library (nlohmann/json) downloads automatically the first time you configure.

```bash
cmake -S . -B build
cmake --build build
open "build/Cadet Prep.app"   # window version (macOS)
./build/afoqt                 # console version
```

On Linux and Windows the window version is `build/afoqt_gui`. On a Mac you can also
double-click `Cadet Prep.app` in the `build` folder, or drag it to Applications.

The app reads questions from the project's `data/` folder, so you can edit question
files and rerun without rebuilding. To use a different folder: `./build/afoqt path/to/data`.

Progress is saved to `afoqt_progress.json` (tests) and `afoqt_flashcards.json` (flashcards)
in `~/Library/Application Support/Cadet Prep` on a Mac, or `~/.cadet-prep` elsewhere.
Progress from before the app was renamed (saved under "AFOQT Study") moves there
automatically the first time you open it.

## Features

- **Practice mode**: untimed, explanation after every question
- **Timed test**: countdown per section, review of misses at the end
- **Full battery**: all knowledge sections, or all pilot sections, back to back, timed
- **Review missed**: re-asks every question you've gotten wrong until you get it right
- **Progress**: accuracy and seconds per question by section, plus your weakest area
- **Settings**: sound effects on or off (with test buttons), light or dark mode, and
  the vocabulary card direction. Saved between launches.
- **Main menu**: the app opens here; pick AFOQT Prep or Warrior Knowledge.
- **Estimated composite scores**: Pilot, CSO, ABM, Academic Aptitude, Verbal, and
  Quantitative, each the average of your accuracy on the sections that make it up,
  with the weakest section and any you haven't practiced yet. The real AFOQT reports
  composites as percentiles compared with other test takers, and the formula isn't
  public, so these are a guide, not a prediction of your official score.
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
| Instrument Comprehension | unlimited: artificial horizon + compass, pick the matching aircraft picture | 25 in 5 min |
| Aviation Information | 44 written | 20 in 8 min |
| Block Counting | unlimited 3D block stacks | 30 in 5 min |

The Pilot composite score uses Math Knowledge, Table Reading, Instrument
Comprehension, and Aviation Information. Block Counting counts toward other
composites (CSO and ABM). Sections with pictures only appear in the window
version, not the console version. The real Table Reading test uses a larger
table (X and Y from -15 to +15); this app uses -10 to +10 so it fits on screen.
Instrument Comprehension answers are aircraft pictures, as on the real test: you
look north from behind the aircraft, so heading north points away from you and
heading east points right.

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

**Warrior Knowledge**: Blocks 1 to 11 of the Det 105 Warrior Knowledge handbook, as
flashcards (window version only). Every card is one handbook item from
`data/warrior_cards.json`: the title on the front, the handbook text on the back.
Flip the card, then swipe right if you knew it or left if you didn't; cards you
swipe left go to "Review missed".

| Block | Covers | Cards |
|---|---|---|
| 1 | Core values, Honor Code, missions and vision, seven basic responses | 10 |
| 2 | Fall 2026 Det 105 mission and vision, Field Training mission | 3 |
| 3 | Cadet and officer ranks, AFROTC mission, Det 105/CC priorities, foundational competencies | 5 |
| 4 | USAF and USSF enlisted ranks, Det 105/CC mission and vision, Five Cs | 5 |
| 5 | Chain of command, additional personnel, Det 105 founding, executing mission command | 4 |
| 6 | USAF major commands, USSF commands (one card each), Air Force Core Functions | 15 |
| 7 | Code of Conduct, Articles I to III | 3 |
| 8 | Code of Conduct, Articles IV to VI | 3 |
| 9 | Fallen alumni, phonetic alphabet, Schwarzkopf quote | 3 |
| 10 | Airman's Creed | 1 |
| 11 | Officer and Enlisted Oaths of Office | 2 |

Not included: the Air Force Song and Space Force Song lyrics (Block 2), and details
for the fallen alumnus you choose (Block 9). Add them as cards in
`data/warrior_cards.json` (`"pos"` is the block, e.g. `"BLOCK 2"`) if you'd like to
drill them. When the chain of command changes, update the "Chain of Command" card.

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
  Visuals.*                  tables, cockpit dials, aircraft pictures, and block stacks
  Composites.*               estimated composite scores
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

The console version runs tests through `TestSession`, which only talks to the `UI`
interface, never to `std::cout` directly. The window version uses `QuizEngine` instead,
which steps through a quiz one question at a time so the GUI can draw each frame.

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

## Adding more sections

**Multiple-choice sections** (like Aviation Information): add an entry to
`data/subtests.json` with its `"track"` (the prep section: `"afoqt"` or `"warrior"`) and
`"category"` (in AFOQT Prep, `"knowledge"` or `"pilot"`), and create its question file.
No code changes needed.

**A new prep section** (like Fitness): add it to `tracks()` near the top of
`src/GuiApp.cpp`, then give its sections that `"track"` in `subtests.json`.

**Sections with pictures** (like Instrument Comprehension): set `"visual": true` in
`subtests.json`, fill in the question's `visual` field (see `Visual` in `Question.h`),
and add a drawing function for the new `VisualKind` in `Visuals.cpp`. These sections
only show up in the window version.

**Generated sections** (like Table Reading and Block Counting): add a generator in
`QuestionGenerator.cpp` that builds `Question` objects in code, and hook it up in
`supports()` and `generate()`. You get unlimited fresh questions.

## Time limits

The pilot sections use the official question counts and time limits. The knowledge
section limits in `subtests.json` are practice pacing, not official values. Check the
current official AFOQT study guide and adjust `time_limit_seconds` and
`question_count` to match.
