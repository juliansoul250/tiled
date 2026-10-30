#!/bin/bash
# Unsigned/ad-hoc fork handoff. Never uses signing credentials or disables OS checks.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${QT_ROOT_DIR:?Set QT_ROOT_DIR to the absolute Qt 6.11.3 desktop kit}"
QT_ROOT_DIR=$(cd "$QT_ROOT_DIR" && pwd)
QMAKE="$QT_ROOT_DIR/bin/qmake"
DEPLOY="$QT_ROOT_DIR/bin/macdeployqt"
[[ $("$QMAKE" -query QT_VERSION) == 6.11.3 ]] || { echo 'Qt 6.11.3 is required' >&2; exit 1; }
[[ -x "$DEPLOY" ]] || exit 1
# mktemp prevents reuse of products from another SDK or diagnostic configuration.
WORK=$(mktemp -d "${TMPDIR:-/tmp}/tiled-package.XXXXXXXX")
export TILED_PACKAGE_WORK="$WORK"
echo "Fresh package workspace: $WORK"
python3 "$ROOT/tests/test_macos_package_audit.py"
qbs setup-toolchains --detect
qbs setup-qt "$QMAKE" tiled-package-6113
qbs config profiles.tiled-package-6113.baseProfile xcode
qbs install -f "$ROOT/tiled.qbs" -d "$WORK/build" --install-root "$WORK/stage" \
    profile:tiled-package-6113 config:release qbs.installPrefix:"" \
    projects.Tiled.nativeWidgetTests:false products.python.condition:false
APP="$WORK/stage/Tiled.app"
# Debug-symbol bundles are not loadable plug-ins. Preserve them outside the app
# so macdeployqt/codesign do not mistake their DWARF payloads for runtime dylibs.
python3 - "$APP" "$WORK/symbols" <<'PYTHON'
import pathlib, shutil, sys
app, symbols = map(pathlib.Path, sys.argv[1:])
for bundle in list(app.rglob('*.dSYM')):
    destination = symbols / bundle.relative_to(app)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(bundle), str(destination))
PYTHON
"$DEPLOY" "$APP" -verbose=2 2>&1 | tee "$WORK/macdeployqt.log"
if grep -q '^[[:space:]]*ERROR:' "$WORK/macdeployqt.log"; then
    echo "macdeployqt reported an error; see $WORK/macdeployqt.log" >&2
    exit 1
fi
(cd "$WORK/stage" && ruby "$ROOT/dist/macos/fixup-install-names.rb")
cp "$ROOT"/AUTHORS "$ROOT"/COPYING "$ROOT"/LICENSE.* "$APP/Contents/Resources/"
python3 "$ROOT/dist/macos/verify-fork-package.py" "$APP" "$QT_ROOT_DIR" "$ROOT"
# Deployment changes Mach-O load commands; replace stale signatures with ad-hoc.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
# Move outside both source and build tree before launch, with no kit lookup paths.
DEST=$(mktemp -d "${TMPDIR:-/tmp}/tiled-delivery.XXXXXXXX")
ditto "$APP" "$DEST/Tiled.app"
export TILED_SMOKE_APP="$DEST/Tiled.app"
export TILED_SMOKE_CWD="$DEST"
export TILED_WIDGET_ARTIFACTS="${TILED_WIDGET_ARTIFACTS:-$DEST/validation}"
mkdir -p "$TILED_WIDGET_ARTIFACTS"
python3 "$ROOT/tests/native_app_smoke.py"
ZIP="${TILED_PACKAGE_ZIP:-$ROOT/Tiled-macOS-Qt6.11.3-ad-hoc.zip}"
ditto -c -k --sequesterRsrc --keepParent "$DEST/Tiled.app" "$ZIP"
shasum -a 256 "$ZIP" | tee "$ZIP.sha256"
echo "Ad-hoc signed, NOT notarized: $ZIP"
