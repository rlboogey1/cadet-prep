#!/bin/sh
# Publishes the website and the Mac download. Run it on your Mac whenever you want
# what's on main to go live; nothing publishes on its own. See WEB.md, "Publishing".
#
#   ./publish.sh              build everything, put the site on Cloudflare Pages, upload the Mac app
#   ./publish.sh --site       only the site
#   ./publish.sh --app        only the Mac app
#   ./publish.sh --dry-run    build and check everything, upload nothing
#
# What it needs:
#   - supabase.json (the account server; see SYNC.md), or the site won't ask for a login
#   - Emscripten (brew install emscripten) and Node (comes with it)
#   - once: npx wrangler login          (your Cloudflare account)
#   - for the Mac app: the Supabase project's secret key in the file supabase.secret,
#     or in SUPABASE_SECRET_KEY. Without it the app is built and you upload it by hand.
#
# The site's Cloudflare Pages project is "cadet-prep" (CADET_PAGES_PROJECT changes it).
set -e
cd "$(dirname "$0")"

SITE=1; APP=1; DRY=0
for arg in "$@"; do
    case "$arg" in
        --site) APP=0 ;;
        --app) SITE=0 ;;
        --dry-run) DRY=1 ;;
        *) echo "Unknown option: $arg (see the top of publish.sh)"; exit 1 ;;
    esac
done
PROJECT="${CADET_PAGES_PROJECT:-cadet-prep}"
OUT=build-web
DIST="$OUT/site"

say() { printf '\n== %s\n' "$1"; }
fail() { printf '\nStopped: %s\n' "$1" >&2; exit 1; }

# Reads "url" or "key" from supabase.json
setting() { python3 -c "import json,sys; print(json.load(open('supabase.json')).get(sys.argv[1], ''))" "$1"; }

[ -f supabase.json ] || fail "there is no supabase.json, so the site would have no login. See SYNC.md."
URL=$(setting url | sed 's:/*$::')
[ -n "$URL" ] || fail "supabase.json has no \"url\"."

# ---------------------------------------------------------------- the site
if [ "$SITE" = 1 ]; then
    say "Building the website"
    command -v emcmake >/dev/null || fail "Emscripten isn't installed (brew install emscripten)."
    # Emscripten needs Python 3.10 or newer; the one that comes with Xcode is older
    if [ -z "$EMSDK_PYTHON" ] && [ -x /opt/homebrew/bin/python3 ]; then
        export EMSDK_PYTHON=/opt/homebrew/bin/python3
    fi
    emcmake cmake -S . -B "$OUT" -DCMAKE_BUILD_TYPE=Release -DSUPABASE_CONFIG="$PWD/supabase.json" >/dev/null
    cmake --build "$OUT" --target cadet_mobile -j8 | grep -E "warning|error|Built target cadet_mobile" || true
    [ -f "$OUT/study.wasm" ] || fail "the website didn't build."

    # Only the site's own files go up, never the rest of the build folder
    rm -rf "$DIST"
    mkdir -p "$DIST"
    cp "$OUT"/index.html "$OUT"/login.html "$OUT"/reset.html "$OUT"/site.css "$OUT"/config.js \
       "$OUT"/study.html "$OUT"/study.js "$OUT"/study.wasm "$OUT"/study.data "$DIST"/
    cp -R "$OUT/fonts" "$DIST"/
    cp -R web/shots "$DIST"/   # from the source, not the build folder's copy
    # Something on this Mac (iCloud Desktop syncing, most likely) leaves duplicates
    # named "file 2.png" behind. They never belong on the site.
    find "$DIST" -name '* [0-9].*' -delete

    say "Checking the site"
    grep -q "$URL" "$DIST/config.js" || fail "config.js doesn't point at the account server in supabase.json."
    # The website is AFOQT Prep only. The handbook must never be in what goes up.
    if grep -rIil "warrior" "$DIST" --include='*.html' --include='*.css' --include='*.js' | grep -v '/study.js$' | grep -q .; then
        fail "a page mentions Warrior Knowledge."
    fi
    if grep -aqi "warrior\|wk_block\|handbook" "$DIST/study.data"; then
        fail "Warrior Knowledge is in the website's question data."
    fi
    [ -z "$(find "$DIST" -name '*.png' -path '*insignia*')" ] || fail "insignia pictures are in the site."
    echo "No Warrior Knowledge in it. Files: $(find "$DIST" -type f | wc -l | tr -d ' '), $(du -sh "$DIST" | cut -f1)."

    if [ "$DRY" = 1 ]; then
        echo "Dry run: the site is in $DIST and was not uploaded."
    else
        say "Putting the site on Cloudflare Pages ($PROJECT)"
        npx --yes wrangler pages deploy "$DIST" --project-name "$PROJECT" --branch main --commit-dirty=true
    fi
fi

# ---------------------------------------------------------------- the Mac app
if [ "$APP" = 1 ]; then
    say "Building the Mac app"
    ./web/make_dmg.sh | tail -1
    DMG=build-release/CadetPrep.dmg
    [ -f "$DMG" ] || fail "the Mac app didn't build."

    SECRET="$SUPABASE_SECRET_KEY"
    if [ -z "$SECRET" ] && [ -f supabase.secret ]; then SECRET=$(tr -d ' \n\r' < supabase.secret); fi

    if [ "$DRY" = 1 ]; then
        echo "Dry run: $DMG was not uploaded."
    elif [ -z "$SECRET" ]; then
        echo "No secret key (supabase.secret), so nothing was uploaded."
        echo "Upload $DMG by hand: Supabase > Storage > downloads, named CadetPrep.dmg."
    else
        say "Uploading the Mac app to the private downloads bucket"
        STATUS=$(curl -sS -o /tmp/cadet-upload.txt -w '%{http_code}' -X POST \
            "$URL/storage/v1/object/downloads/CadetPrep.dmg" \
            -H "Authorization: Bearer $SECRET" -H "apikey: $SECRET" \
            -H "Content-Type: application/x-apple-diskimage" -H "x-upsert: true" \
            --data-binary "@$DMG")
        if [ "$STATUS" = 200 ]; then
            echo "Uploaded."
        else
            cat /tmp/cadet-upload.txt; echo
            fail "the upload was turned down (HTTP $STATUS). Is the downloads bucket made (WEB.md)?"
        fi
        rm -f /tmp/cadet-upload.txt
    fi
fi

say "Done"
