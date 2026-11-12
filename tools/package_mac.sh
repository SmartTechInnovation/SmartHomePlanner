#!/bin/bash
# Builds a Release version of Smart Home Planner and makes a .dmg that others can install.
#
#   ./tools/package_mac.sh                       -> dist/SmartHomePlanner-<version>-macOS.dmg
#
# Optional environment variables:
#   QT_DIR=/Users/<you>/Qt/6.11.2/macos          Qt installation (default: ~/Qt/6.11.2/macos)
#   SIGN_ID="Developer ID Application: Name (TEAMID)"   sign with an Apple Developer ID
#   NOTARY_PROFILE=<keychain profile>            notarize (xcrun notarytool store-credentials ...)
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
QT_DIR="${QT_DIR:-$HOME/Qt/6.11.2/macos}"
BUILD="$SRC/build/release-macos"
STAGE="$BUILD/install"
DIST="$SRC/dist"

CMAKE="$(command -v cmake || true)"
[ -z "$CMAKE" ] && CMAKE="$HOME/Qt/Tools/CMake/CMake.app/Contents/bin/cmake"
[ -x "$CMAKE" ] || { echo "cmake not found (install it or use the one from Qt: ~/Qt/Tools/CMake)"; exit 1; }
[ -d "$QT_DIR" ] || { echo "Qt not found in $QT_DIR (set QT_DIR=...)"; exit 1; }

VERSION="$(sed -n 's/^project(SmartHomePlanner VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")"
echo "== Smart Home Planner $VERSION   Qt: $QT_DIR"

# 1. Release build, universal (Apple Silicon + Intel), libraries only from inside the app
"$CMAKE" -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DSHP_DEV_LIBRARY_PATH=OFF \
    -DSHP_BUILD_TESTS=OFF
"$CMAKE" --build "$BUILD" --config Release --parallel

# 2. Install = copy the app + Qt frameworks / plugins into it (macdeployqt)
rm -rf "$STAGE"
"$CMAKE" --install "$BUILD" --config Release --prefix "$STAGE"
APP="$STAGE/SmartHomePlanner.app"
[ -d "$APP/Contents/Resources/Libraries" ] || { echo "Libraries missing in the app"; exit 1; }

# 3. Signature: Developer ID if given, otherwise ad-hoc (needed to run on Apple Silicon)
if [ -n "${SIGN_ID:-}" ]; then
    codesign --force --deep --options runtime --timestamp --sign "$SIGN_ID" "$APP"
else
    codesign --force --deep --sign - "$APP"
fi
codesign --verify --deep --strict "$APP" && echo "signature OK"

# 4. Disk image with a shortcut to /Applications
mkdir -p "$DIST"
DMG="$DIST/SmartHomePlanner-$VERSION-macOS.dmg"
TMP="$BUILD/dmg"
rm -rf "$TMP" "$DMG"
mkdir -p "$TMP"
cp -R "$APP" "$TMP/"
ln -s /Applications "$TMP/Applications"
hdiutil create -volname "Smart Home Planner" -srcfolder "$TMP" -ov -format UDZO "$DMG"

# 5. Notarization (only with a Developer ID)
if [ -n "${SIGN_ID:-}" ] && [ -n "${NOTARY_PROFILE:-}" ]; then
    codesign --force --sign "$SIGN_ID" "$DMG"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
fi

echo
echo "Done: $DMG"
