#!/bin/bash
# Unsigned/ad-hoc fork handoff. Never uses signing credentials or disables OS checks.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${QT_ROOT_DIR:?Set QT_ROOT_DIR to the absolute Qt 6.11.3 desktop kit}"
QT_ROOT_DIR=$(cd "$QT_ROOT_DIR" && pwd)
QMAKE="$QT_ROOT_DIR/bin/qmake"
DEPLOY="$QT_ROOT_DIR/bin/macdeployqt"
[[ $("$QMAKE" -query QT_VERSION) == 6.11.3 ]] || { echo '必须使用完整的 Qt 6.11.3 套件' >&2; exit 1; }
[[ -x "$DEPLOY" ]] || exit 1
QT_TRANSLATIONS=$("$QMAKE" -query QT_INSTALL_TRANSLATIONS)
[[ -f "$QT_TRANSLATIONS/qtbase_zh_CN.qm" ]] || { echo 'Qt 套件缺少官方简体中文词典 qtbase_zh_CN.qm' >&2; exit 1; }
python3 "$ROOT/tests/check_chinese_translation.py"
python3 "$ROOT/tests/check_unity_importer.py"
# mktemp prevents reuse of products from another SDK or diagnostic configuration.
WORK=$(mktemp -d "${TMPDIR:-/tmp}/tiled-package.XXXXXXXX")
export TILED_PACKAGE_WORK="$WORK"
echo "全新构建目录：$WORK"
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
    echo "Qt 部署报告错误，请查看 $WORK/macdeployqt.log" >&2
    exit 1
fi
(cd "$WORK/stage" && ruby "$ROOT/dist/macos/fixup-install-names.rb")
# Ship the same kit's official Chinese Qt catalogs alongside Tiled's catalog.
python3 - "$APP" "$QT_TRANSLATIONS" <<'PYTHON'
import configparser, pathlib, shutil, sys
app, translations = map(pathlib.Path, sys.argv[1:])
destination = app / 'Contents/Translations'
destination.mkdir(exist_ok=True)
for catalog in translations.glob('*_zh_CN.qm'):
    shutil.copy2(catalog, destination / catalog.name)
conf = app / 'Contents/Resources/qt.conf'
settings = configparser.ConfigParser()
settings.optionxform = str
settings.read(conf)
if not settings.has_section('Paths'):
    settings.add_section('Paths')
settings['Paths']['Translations'] = 'Translations'
with conf.open('w') as stream:
    settings.write(stream)
PYTHON
cp "$ROOT"/AUTHORS "$ROOT"/COPYING "$ROOT"/LICENSE.* "$APP/Contents/Resources/"
if [[ -n "${TILED_SOURCE_VALIDATION_RECORD:-}" ]]; then
    cp "$TILED_SOURCE_VALIDATION_RECORD" "$APP/Contents/Resources/source-validation.json"
fi
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
export TILED_EXPECT_CHINESE=1
python3 "$ROOT/tests/native_app_smoke.py"
ZIP="${TILED_PACKAGE_ZIP:-$ROOT/Tiled-macOS-Qt6.11.3-ad-hoc.zip}"
ditto -c -k --sequesterRsrc --keepParent "$DEST/Tiled.app" "$ZIP"
shasum -a 256 "$ZIP" | tee "$ZIP.sha256"
echo "中文应用包已生成（临时签名，未经 Apple 公证）：$ZIP"
