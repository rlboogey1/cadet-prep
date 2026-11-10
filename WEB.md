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
- **Online sync**: the same account and merge rules as the apps (`SYNC.md`). A browser
  has no background threads, so `CloudSync` runs its steps through `http::requestAsync`
  (the browser's `fetch`). The website never changes Warrior Knowledge progress: it
  carries those files through a sync untouched.

## Login

When the site is built with an account server (`supabase.json`), the study page only
opens while logged in:

- `login.html` signs in with the email and password and hands the sign-in to
  `study.html` (through `localStorage`). `web/pre.js` saves it as `afoqt_account.json`,
  which is where the app's sync reads it. There is no sign-up form.
- Opening `study.html` without a sign-in goes to `login.html`. **Log out** (Settings >
  Account) forgets the sign-in and goes back there. Progress already in the browser stays.
- **Forgot password** emails a link to `reset.html`, which sets a new password. For that
  email to arrive with the right link, add the site's address to the Supabase project:
  **Authentication > URL Configuration**, set **Site URL** to the site and add
  `https://<site>/reset.html` under **Redirect URLs**.
- `config.js` (made by CMake) gives those pages the server address and public key.

The login keeps people out of the study screens. It doesn't hide the site's files: the
question data sits at a public address like everything else on a static host, which is
why the website carries AFOQT Prep only.

Built without `supabase.json`, nothing asks for a login and progress stays in the browser.

## Testing login and sync

Never test against the real project. `web/fake_supabase.py` is a stand-in server with
one made-up account (its email and password are at the top of that file):

```bash
echo '{"url": "http://localhost:8766", "key": "test-public-key"}' > /tmp/fake.json
emcmake cmake -S . -B build-web-test -DCMAKE_BUILD_TYPE=Release -DSUPABASE_CONFIG=/tmp/fake.json
cmake --build build-web-test --target cadet_mobile
python3 web/fake_supabase.py --seed web/fake_seed.json      # the account server, port 8766
python3 -m http.server 8767 --directory build-web-test      # the site: http://localhost:8767/login.html
```

"Forgot password" prints the reset link in the fake server's output, and
`http://localhost:8766/_state` shows the progress it holds.

## Introduction page

`index.html` is the public page in front of the login. It is made at build time from
`web/index.html.in` by `web/site_data.cmake`, which fills in the section table and the
totals from `data/subtests.json` and the question files, so the page can't drift from
what the app holds. It never mentions Warrior Knowledge.

Its four pictures (`web/shots/`) are real screens of the app. To retake them, build the
Mac preview without Warrior Knowledge and run it on the website's data with a scratch
`HOME` holding made-up progress, using `--script` to tap through and save each screen:

```bash
cmake -S . -B build-shots -DCADET_MOBILE=ON -DCADET_NO_WARRIOR=ON
cmake --build build-shots --target cadet_mobile
HOME=/tmp/shots-home ./build-shots/cadet_mobile --size 1280x800 --script taps.txt build-web/webdata
```

Then crop off the sidebar (the left 288 points) and scale to 1400 pixels wide.

## Still to do

The Mac download section, and publishing.
