#!/bin/sh
set -eu
SHELF_BUILD=${1:-build}
SHELF_VERSION=${2:-0.2.0}
SHELF_OUTPUT=${3:-dist}
SHELF_ARCH=${4:-$(uname -m)}
: "${QT_ROOT_DIR:?Set QT_ROOT_DIR to the Qt 6 installation}"
SHELF_SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SHELF_STAGE=$(mktemp -d "${TMPDIR:-/tmp}/arcade-shelf-package.XXXXXX")
trap 'rm -rf "$SHELF_STAGE"' EXIT HUP INT TERM
cmake --install "$SHELF_BUILD" --config Release --prefix "$SHELF_STAGE"
SHELF_APP=$(find "$SHELF_STAGE" -maxdepth 1 -name '*.app' -type d -print)
[ -n "$SHELF_APP" ] || { echo 'No installed app bundle' >&2; exit 1; }
if [ "$SHELF_APP" != "$SHELF_STAGE/Arcade Shelf.app" ]; then
  mv "$SHELF_APP" "$SHELF_STAGE/Arcade Shelf.app"
fi
SHELF_APP="$SHELF_STAGE/Arcade Shelf.app"
if [ "$SHELF_ARCH" = universal ]; then
  lipo "$SHELF_APP/Contents/MacOS/arcade-shelf" -verify_arch arm64 x86_64
fi
"$QT_ROOT_DIR/bin/macdeployqt" "$SHELF_APP" "-qmldir=$SHELF_SOURCE/ui" -always-overwrite
mkdir -p "$SHELF_APP/Contents/Resources/licenses"
cp "$SHELF_SOURCE/LICENSE" "$SHELF_SOURCE/THIRD_PARTY_NOTICES.md" "$SHELF_APP/Contents/Resources/licenses/"
cp -R "$SHELF_SOURCE/licenses/." "$SHELF_APP/Contents/Resources/licenses/"
cp -R "$SHELF_SOURCE/vendor/arcade-link/LICENSE-MIT" "$SHELF_APP/Contents/Resources/licenses/Arcade-Link-MIT.txt"
cp "$SHELF_SOURCE/vendor/arcade-link/provenance.json" "$SHELF_SOURCE/vendor/arcade-link/shared-provenance.json" "$SHELF_APP/Contents/Resources/licenses/"
mkdir -p "$SHELF_APP/Contents/Resources/docs"
cp -R "$SHELF_SOURCE/docs/user" "$SHELF_APP/Contents/Resources/docs/user"
if [ -d "$QT_ROOT_DIR/licenses" ]; then cp -R "$QT_ROOT_DIR/licenses" "$SHELF_APP/Contents/Resources/licenses/Qt"; fi
"$SHELF_APP/Contents/MacOS/arcade-shelf" --version
ln -s /Applications "$SHELF_STAGE/Applications"
mkdir -p "$SHELF_OUTPUT"
[ "$SHELF_ARCH" != aarch64 ] || SHELF_ARCH=arm64
[ "$SHELF_ARCH" != x86_64 ] || SHELF_ARCH=x64
hdiutil create -volname 'Arcade Shelf' -srcfolder "$SHELF_STAGE" -ov -format UDZO "$SHELF_OUTPUT/Arcade-Shelf-$SHELF_VERSION-macos-$SHELF_ARCH.dmg"
echo 'Development DMG created. Signing and notarization have not been performed.'
