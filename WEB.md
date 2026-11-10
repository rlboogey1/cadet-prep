# Website version

The website runs the same app as the Mac and iPhone versions, compiled to WebAssembly
with Emscripten. It is **AFOQT Prep only**: Warrior Knowledge is left out of the code
(`CADET_NO_WARRIOR`) and out of the data that ships (`web/web_data.cmake`, which fails
the build if any of it slips in).

## Build and try it

```bash
brew install emscripten                      # once
export EMSDK_PYTHON=/opt/homebrew/bin/python3   # Emscripten needs Python 3.10+, newer than Xcode's
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target cadet_mobile
python3 -m http.server 8765 --directory build-web   # then open http://localhost:8765/study.html
```

The build makes `study.html`, `study.js`, `study.wasm` and `study.data` (questions and
fonts) in `build-web/`. After rebuilding, hard-reload the page (Cmd+Shift+R): browsers
keep the old `.js` and `.wasm`.

## How it differs from the other versions

- **Front end**: `mobile_main.cpp` (SDL3). The browser calls one frame at a time
  (`emscripten_set_main_loop`), so the loop is a frame function and its state is static.
- **Page**: `web/study_shell.html` holds the canvas, which fills the tab, and tells the
  app when its size changes (`cadet_resize`). Under 1000 points wide it uses the phone layout;
  a phone or tablet browser also gets touch mode.
- **Saving**: `web/pre.js` keeps the progress folder in the browser's storage (IndexedDB),
  loaded before the app starts and written back whenever a file is saved. Clearing site
  data erases it.
- **Online sync**: off for now (no sign-in in the browser yet).

## Still to do

Login and sync, the introduction page, the Mac download section, and publishing.
