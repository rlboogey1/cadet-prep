# AFOQT Study (C++ and Claude AI) 

A console study app for the AFOQT knowledge sections: Word Knowledge, Math Knowledge,
Arithmetic Reasoning, Verbal Analogies, and Reading Comprehension. It's built so pilot
sections can be added later without restructuring.

## Build and run

Requires CMake 3.16+ and a C++17 compiler (g++, clang, or Visual Studio 2019+).
The JSON library (nlohmann/json) downloads automatically the first time you configure.

```bash
cmake -S . -B build
cmake --build build
./build/afoqt            # Windows: build\Debug\afoqt.exe
```

The app reads questions from the project's `data/` folder, so you can edit question
files and rerun without rebuilding. To use a different folder: `./build/afoqt path/to/data`.

Progress is saved to `afoqt_progress.json` in the folder you run it from.

## Features

- **Practice mode**: untimed, explanation after every question
- **Timed test**: countdown per section, review of misses at the end
- **Full battery**: all knowledge sections back to back, timed
- **Review missed**: re-asks every question you've gotten wrong until you get it right
- **Progress**: accuracy and seconds per question by section, plus your weakest area

## Project layout

```
data/
  subtests.json              list of sections, time limits, tips
  word_knowledge.json        one question file per section
  ...
src/
  Question.h                 Question and AnswerRecord structs
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
