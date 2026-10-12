#!/bin/sh
# Exercise the shipped bundle in an isolated CI profile, without a GUI.
set -eu
: "${RUNNER_TEMP:?Run this only in the macOS CI runner}"
SHELF_TEST_VERSION=$1
SHELF_TEST_DMG=$(find "${2:-dist}" -maxdepth 1 -name "Arcade-Shelf-$SHELF_TEST_VERSION-macos-*.dmg" -print)
[ -n "$SHELF_TEST_DMG" ] || { echo 'Shelf disk image missing' >&2; exit 1; }
SHELF_TEST_ROOT=$(mktemp -d "$RUNNER_TEMP/shelf-macos-install.XXXXXX")
SHELF_TEST_MOUNT="$SHELF_TEST_ROOT/dmg"
mkdir -p "$SHELF_TEST_MOUNT" "$SHELF_TEST_ROOT/download" "$SHELF_TEST_ROOT/profile"
trap 'hdiutil detach "$SHELF_TEST_MOUNT" >/dev/null 2>&1 || true; rm -rf "$SHELF_TEST_ROOT"' EXIT HUP INT TERM
hdiutil attach "$SHELF_TEST_DMG" -readonly -nobrowse -mountpoint "$SHELF_TEST_MOUNT"
ditto "$SHELF_TEST_MOUNT/Arcade Shelf.app" "$SHELF_TEST_ROOT/download/Arcade Shelf.app"
export ARCADE_HOME="$SHELF_TEST_ROOT/arcade" ARCADE_SHELF_HOME="$SHELF_TEST_ROOT/profile" ARCADE_NO_INSTALL_PROMPT=1
printf '%s\n' 'retain settings and content' > "$ARCADE_SHELF_HOME/keep.txt"
"$SHELF_TEST_ROOT/download/Arcade Shelf.app/Contents/MacOS/arcade-shelf" --install --silent --no-launch
SHELF_TEST_APP="$ARCADE_HOME/Applications/Arcade Shelf.app"
test -d "$SHELF_TEST_APP"
"$SHELF_TEST_APP/Contents/MacOS/arcade-shelf" --integration-status
"$SHELF_TEST_APP/Contents/MacOS/arcade-shelf" --repair
"$SHELF_TEST_APP/Contents/MacOS/arcade-shelf" --uninstall --silent
test ! -e "$SHELF_TEST_APP"
test ! -e "$ARCADE_HOME/installs/arcade.shelf.json"
test ! -L "$ARCADE_HOME/.local/bin/arcade-shelf"
test -f "$ARCADE_SHELF_HOME/keep.txt"
echo 'macOS DMG copy, move, repair, uninstall and data preservation passed (CI only).'
