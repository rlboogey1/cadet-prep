#!/bin/sh
# Builds the Mac app for the website's Download section: build-release/CadetPrep.dmg.
#
# A separate, optimized build that runs on Apple silicon and Intel Macs with
# macOS 11 or later. It isn't signed with an Apple developer account, so macOS
# asks the user to allow it once (System Settings > Privacy & Security > Open Anyway).
#
# Upload the result to the Supabase project's private "downloads" bucket as
# CadetPrep.dmg (see WEB.md).
set -e
cd "$(dirname "$0")/.."
OUT=build-release

cmake -S . -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
      -DCADET_DATA_DIR=data
cmake --build "$OUT" --target afoqt_gui afoqt_gui_data -j8

APP="$OUT/Cadet Prep.app"
xattr -cr "$APP"                          # Finder tags and the like stop codesign
codesign --force --deep --sign - "$APP"   # ad-hoc signature: required on Apple silicon
codesign --verify --deep "$APP"

STAGE="$OUT/dmg"
rm -rf "$STAGE" "$OUT/CadetPrep.dmg"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "Cadet Prep" -srcfolder "$STAGE" -ov -format UDZO "$OUT/CadetPrep.dmg" >/dev/null
rm -rf "$STAGE"

echo "Made $OUT/CadetPrep.dmg ($(du -h "$OUT/CadetPrep.dmg" | cut -f1))"
