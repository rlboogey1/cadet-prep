# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Cadet Prep: a C++17 study app for AFROTC cadets, with a Mac window version (Dear ImGui + GLFW + OpenGL),
an iPhone/iPad version (Dear ImGui + SDL3, see `IOS.md`), and a console version. It's split into **prep sections** ("tracks"): AFOQT Prep and Warrior Knowledge.
Internal names still say `afoqt` (targets, save files) from before the rename; keep them, since
existing saved progress depends on the file names.

## Commands

```bash
cmake -S . -B build                  # configure (first time, or after deleting build/); downloads deps
cmake --build build                  # build both apps; also re-copies data/ into the .app bundle
open "build/Cadet Prep.app"          # window version (macOS); build/afoqt_gui elsewhere
./build/afoqt                        # console version; optional arg: path to a data folder

cmake -S . -B build-mobile -DCADET_MOBILE=ON && cmake --build build-mobile --target cadet_mobile
./build-mobile/cadet_mobile          # phone layout in a Mac window (--ipad, --size WxH, --touch)
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS   # iPhone/iPad Xcode project (needs Xcode)
```

To check layouts without clicking, `cadet_mobile --script file.txt` replays `wait N`, `tap X Y`,
`drag X1 Y1 X2 Y2 FRAMES`, `type TEXT`, `key enter`, and `shot out.bmp` lines (points, touch input), then quits. Run it with
`HOME` set to a scratch folder so it doesn't touch the user's real progress.

There are no tests and no linter. Builds use `-Wall -Wextra -Wpedantic`; keep them warning-free.
On startup both apps load and validate every question file (bad answer indexes, missing files) and
report errors, so running the app is the check for data edits.

The window version reads questions from the copy inside the bundle (`Contents/Resources/data`), so
run `cmake --build build` after editing anything in `data/`. If the project folder moves, delete
`build/` and reconfigure (CMake caches absolute paths).

## Architecture

**Three front ends, one core.** `afoqt_core` (static lib) holds everything that isn't UI. The console
app (`main.cpp`, `ConsoleUI`) runs quizzes with `TestSession`, which blocks on input through the
abstract `UI` interface. The window app (`gui_main.cpp`, `GuiApp`) is immediate mode: `draw()` runs
every frame, so it uses `QuizEngine`, which advances one step at a time (`submit`, `next`, `update`
for the timer) instead of blocking. The iPhone/iPad app (`mobile_main.cpp`, target `cadet_mobile`)
runs the same `GuiApp` on SDL3 with SDL's renderer (Metal on Apple devices); it passes the notch
and home-bar insets (`setSafeArea`) and turns on touch mode (`setTouch`). Each front end gives
`Visuals` a texture maker (`setTextureMaker`), since OpenGL and SDL make textures differently.

**Phone layout.** Below 1000 points of width (`kCompactBelow`: any iPhone, an upright iPad),
`GuiApp` is "compact": a navy top bar with a menu button that slides the sidebar in
(`drawDrawer`), cards that stack (`Grid(maxCols, minW)` picks the column count, `subjectCard`
grows to fit and moves its buttons under the footer), and auto-height cards
(`beginAutoCard`) instead of cards that scroll inside the page. Keep the Mac layout unchanged
when editing: put phone-only changes behind `compact_`. Touch mode adds finger scrolling with
momentum (`touchScroll`, which also stops a drag from clicking the button it started on) and
says "tap" instead of "click"/keyboard hints.

**Data-driven sections.** `data/subtests.json` lists every section: `track` (prep section),
`category` (group within a track, e.g. AFOQT `knowledge`/`pilot`), question `file`, `visual`,
timed-test `time_limit_seconds` and `question_count`, `tips`, and optional `icon`/`description`
for the card. Multiple-choice sections need only a JSON entry plus a question file
(`{"questions":[{id, prompt, choices, answer (0-based), explanation, passage?}]}`). IDs must be
unique; they key the missed-question list.

**Questions.** `QuestionBank` loads and validates files, draws sets, and shuffles choices per
draw (`shuffleChoices` also permutes `choiceVisuals` so pictures stay with their choice).
`QuestionGenerator` builds questions in code: Math Knowledge and Arithmetic Reasoning templates
(mixed into written sets), and the generated-only pilot sections Table Reading, Instrument
Comprehension, and Block Counting (`supports()`/`generate()`). Generated IDs start with `GEN-`
and are never saved for review.

**Pictures.** `Question::visual` (`VisualKind` + an int array whose layout is documented in
`Question.h`) is drawn by `Visuals.cpp` with `ImDrawList`: the table, attitude indicator and
compass, isometric blocks, and the 3D aircraft (a small flat-shaded model sorted back to front).
Instrument Comprehension answers are aircraft pictures via `Question::choiceVisuals`; the choice
strings stay as text descriptions for the results screen and console. Sections with
`"visual": true` are hidden in the console.

**Prep sections in the GUI.** `tracks()` near the top of `GuiApp.cpp` defines them. `track_` is the
current one, and pages filter through `trackSections()` / `inTrack()`: dashboard, practice,
review, progress, focus area, and stats. Warm-ups record under `warmup` (AFOQT) or
`warmup_<track>` so they count toward the right track. Estimated composite scores (`Composites.cpp`,
which lists the sections in each composite) are AFOQT only. The app opens on `Screen::Home` (main
menu); Home and Settings hide the per-track workspace menu.

**Flashcards.** `FlashcardDeck` is a Leitner box system (mastered at box 3), used for the AFOQT
Word Knowledge vocabulary deck (`deck_`, `data/flashcards.json`, the Flashcards screen, AFOQT only).
The Flashcards screen also has a spaced repetition deck (`RepetitionDeck`, `repDeck_`): it starts
with the first 3 words of `flashcards.json`, grows 3 at a time in file order, can remove the last
batch added (never the first 3), and keeps a daily streak (finish the whole deck on consecutive
local calendar days).
**Warrior Knowledge has no multiple choice:** its blocks are built only from the handbook items in
`data/warrior_cards.json` (`addWarriorCards()`: `pos` "BLOCK n" → `wk_blockNN`, front = `word`, back
= `definition`, id `WK-` + title). Exceptions, matched by exact card title: the rank lists
(Blocks 3-4: cadet, officer, USAF enlisted, USSF enlisted) and "Chain of Command" / "Additional
Personnel" (Block 5) are split into one card per line: rank from its insignia picture (`VisualKind::Insignia`),
rank from its pay grade, and "Who is the <position>?". Renaming those cards in the JSON turns them
back into single list cards. The insignia are pictures, not drawings: `data/insignia/<scheme>_<index>.png`
(cadet, officer, af_enlisted, sf_enlisted), cut from charts the user sent with the background made
transparent, loaded as OpenGL textures by `drawInsignia()` in `Visuals.cpp` (stb_image). Don't redraw
them in code. Don't add written or made-up questions to these blocks. Every quiz in
that track (`cardMode_`) shows each card as a swipe card (`ui::swipeCard` in `Widgets.cpp`); swipe
right = correct, left = missed (recorded as the placeholder wrong choice so it lands on the review
list). `memDeck_` only loads the cards; there's no separate memorize screen. The console version
hides Warrior Knowledge.

**Saving.** `AppPaths.h` sets the progress folder (`~/Library/Application Support/Cadet Prep` on
macOS, `~/.cadet-prep` elsewhere) and migrates older folders. **Always save and load with
`apppaths::userFile(name)`, never a bare file name:** on macOS GLFW moves the working directory
into the bundle's Resources folder unless `GLFW_COCOA_CHDIR_RESOURCES` is off (it's turned off in
`gui_main.cpp`), which once made settings save inside the app. Files: `afoqt_progress.json`
(`ProgressStore`; review sessions don't count toward accuracy), `afoqt_flashcards.json`, `afoqt_repetition.json` (spaced repetition deck and streak),
`warrior_cards_progress.json` (no longer written, kept for old installs), and `afoqt_settings.json` (sound, dark mode, definition-first,
last track, focus area per track). Settings save on every change.

**Online sync** (`SYNC.md`): one Supabase account (only the user's; sign-ups off) keeps the progress
files the same across devices. Settings don't sync. `cloudsync::syncedFiles()` in `SyncMerge.h` lists
what syncs. The files are bundled into one JSON row (`progress` table) and merged by `SyncMerge.cpp`
(core): sessions are unioned by `id` (older id-less ones get a key made from their fields), and missed
questions (`outcomes`) and flashcard boxes (`changed`) are decided per item by the newer timestamp.
Merges must stay order-independent and repeatable; when you add a field or a synced file, merge it
the same way. `CloudSync` (GUI only) runs the network calls on a background thread through `Http.mm`
(NSURLSession; a stub elsewhere) and reads and writes files only on the main thread in `update()`,
then `GuiApp::reloadProgress()` reloads them. The server address and key come from the git-ignored
`supabase.json`, read by CMake. Without it the Account card says sync isn't set up. Sign-in tokens
are in `afoqt_account.json`. Test with a fake server and a scratch `HOME`, never the real project.

**Look and theme.** `Widgets.h/.cpp` holds the design system: fonts, cards, buttons, chips,
toggles, answer cards, icons (`Icon` enum + `drawIcon`), and the `col::` palette. The palette is
mutable: `setDarkMode()` assigns light or dark values and re-applies the ImGui style. Don't
hard-code colors in drawing code; add a `col::` token and give it both values in `setDarkMode()`.
Pictures (dials, aircraft, blocks) intentionally keep fixed colors. `subtests.json` icon names
map to `Icon` values in `iconNamed()` in `GuiApp.cpp`.

## Content notes

- Warrior Knowledge (`data/warrior_cards.json`) is exactly the user's Det 105 Warrior Knowledge
  handbook, Blocks 1 to 11; only add what the user sends. The chain of command (Block 5) changes
  over time; update the "Chain of Command" card.
- A missed Word Knowledge or Verbal Analogies question shows definitions for its words, looked
  up in `data/flashcards.json` and then `data/va_definitions.json` (lookup only, never studied).
  When adding analogy questions, add any new words to `va_definitions.json`.
- Don't add song lyrics (Air Force Song, Space Force Song); the user can add them to the cards file.
- Pilot sections use official AFOQT question counts and time limits; knowledge-section limits are
  practice pacing.
- `IDEAS.md` is the user's personal notes file, ignored via `.git/info/exclude`. Don't commit it.
