#!/bin/bash
# Build an accepted upstream fix from verified sources, preserving the full kit.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${QT_ROOT_DIR:?Select an official complete Qt 6.11.3 desktop kit}"
ORIGINAL=$(cd "$QT_ROOT_DIR" && pwd)
[[ $("$ORIGINAL/bin/qmake" -query QT_VERSION) == 6.11.3 ]]
WORK=$(mktemp -d "${TMPDIR:-/tmp}/tiled-fixed-qt.XXXXXXXX")
KIT="$WORK/kit"
ditto "$ORIGINAL" "$KIT"
[[ $("$KIT/bin/qmake" -query QT_INSTALL_PREFIX) == "$KIT" ]] || { echo 'Copied Qt kit did not relocate' >&2; exit 1; }
URL=https://download.qt.io/official_releases/qt/6.11/6.11.3/submodules/qtbase-everywhere-src-6.11.3.tar.xz
curl --fail --location --retry 2 "$URL" -o "$WORK/qtbase.tar.xz"
echo "46de0710b594945a5d446ae44354daa386141c6903c53781b2ca547bc62f8f8f  $WORK/qtbase.tar.xz" | shasum -a 256 -c -
echo "e0f0ddcb7deff7e01094814880ec3835bd4e63012d16af50bc81e39e7cad92ea  $ROOT/dist/macos/qt-cocoa-fix/QTBUG-149612.patch" | shasum -a 256 -c -
tar -xf "$WORK/qtbase.tar.xz" -C "$WORK"
SOURCE="$WORK/qtbase-everywhere-src-6.11.3"
(cd "$SOURCE" && git apply --check "$ROOT/dist/macos/qt-cocoa-fix/QTBUG-149612.patch" && git apply "$ROOT/dist/macos/qt-cocoa-fix/QTBUG-149612.patch")
ARCHS=$(lipo -archs "$ORIGINAL/plugins/platforms/libqcocoa.dylib" | tr ' ' ';')
TARGET=$(otool -l "$ORIGINAL/lib/QtCore.framework/Versions/A/QtCore" | awk '$1=="minos" {print $2}' | sort -u)
[[ -n "$TARGET" && "$TARGET" != *$'\n'* ]] || { echo 'Ambiguous SDK deployment target' >&2; exit 1; }
PRIVATE_CMAKE="$KIT/libexec/qt-cmake-private"
[[ -x "$PRIVATE_CMAKE" ]] || PRIVATE_CMAKE="$KIT/bin/qt-cmake-private"
[[ -x "$PRIVATE_CMAKE" ]]
"$PRIVATE_CMAKE" -S "$ROOT/dist/macos/qt-cocoa-fix" -B "$WORK/build" -G Ninja \
    -DQTBASE_SOURCE="$SOURCE" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$KIT" -DCMAKE_INSTALL_PREFIX="$WORK/plugin-install" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$TARGET" \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build "$WORK/build" --target QCocoaIntegrationPlugin --parallel 3
python3 "$ROOT/dist/macos/qt-cocoa-fix/record-kit.py" "$ORIGINAL" "$KIT" "$WORK" "$ARCHS" "$TARGET"
echo "Fixed complete Qt kit: $KIT"
if [[ -n "${GITHUB_ENV:-}" ]]; then
    printf 'QT_ROOT_DIR=%s\nQT_PLUGIN_PATH=%s/plugins\nQML2_IMPORT_PATH=%s/qml\n' "$KIT" "$KIT" "$KIT" >> "$GITHUB_ENV"
fi
printf '%s\n' "$KIT" > "$WORK/kit-path.txt"
if [[ -n "${TILED_FIXED_KIT_RESULT:-}" ]]; then
    printf '%s\n' "$KIT" > "$TILED_FIXED_KIT_RESULT"
fi
