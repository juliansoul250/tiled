# macOS / Windows installation handoff for this fork

Repository: https://github.com/juliansoul250/tiled
Branch: **`fix/layer-stability-ui`**
Base: Tiled **v1.12.2**, `1fd984638f9898af920103799bcd68b467287295`.

This is source code, **not a verified downloadable Mac installer**. There is no
new signed/notarized release from this fork. Build on the secretary's Mac using
the instructions below; record `git rev-parse HEAD` with any crash report.
Do not disable Gatekeeper, SIP, code-signing checks, or other OS protections.

## Changes and limits

- Donation entry in Help, donation button in About, and timed donation reminders
  are removed, including their implementation and scheduling. Legacy settings
  are no longer read for this purpose. **Original author/contributor attribution,
  About copyright, AUTHORS, COPYING/GPL, and source license headers remain.**
- macOS uses AppKit **NSVisualEffectView / sidebar material**, with behind-window
  blending, beneath each native dock inside Qt's content hierarchy. Docked sidebar backgrounds expose
  that material; map/document canvases remain opaque. This is genuine native
  frosted vibrancy, **not macOS 26 Liquid Glass / NSGlassEffectView**.
- Floating docks intentionally use an opaque fallback: Qt cannot safely change
  an already-created native window's alpha format. Redocking restores sidebar
  material. No Qt native content view is replaced or reparented.
- macOS **Reduce Transparency** automatically selects the opaque path and is
  observed for changes while running. An explicit launch fallback is
  `TILED_DISABLE_NATIVE_GLASS=1 qbs run ... -p tiled`. This is also useful when
  diagnosing remote-desktop/compositor problems. Ordinary non-Mac rendering is
  unchanged; the AppKit implementation is not compiled on Windows/Linux.
- Qt 6.8.3 is the CI version. With that kit, [macOS 12+ is the Qt-supported target](https://doc.qt.io/qt-6.8/macos.html);
  this fork's native runtime tests use macOS 14.8.9 arm64. Intel Mac, older macOS,
  Qt 5, signed deployment and physical Retina/GPU combinations are not certified.

## Mac: clone, build and launch locally

Install Xcode Command Line Tools/Xcode, Qbs, and a matching **Qt 6.8.3 desktop
macOS kit** (Widgets, QML, SVG and development tools) using the official Qt
installer. Qbs may be installed via Homebrew (`brew install qbs`). A moving
Homebrew Qt version is not the exact tested kit; do not silently mix kits.

```sh
git clone --branch fix/layer-stability-ui --single-branch \
  https://github.com/juliansoul250/tiled.git
cd tiled
git rev-parse HEAD

# Replace this path with the actual installed Qt 6.8.3 kit.
export QTDIR="$HOME/Qt/6.8.3/macos"
export PATH="$QTDIR/bin:$PATH"
qmake -v
qbs setup-toolchains --detect
qbs setup-qt "$QTDIR/bin/qmake" tiled-qt
qbs config profiles.tiled-qt.baseProfile xcode
qbs config --list

# Normal production symbol visibility, not the diagnostic test configuration.
qbs build profile:tiled-qt config:release products.python.condition:false
qbs run profile:tiled-qt config:release products.python.condition:false -p tiled
```

Use the detected Apple compiler profile instead of `xcode` if the toolchain list
uses another name. The Qt kit and compiler must match the machine's architecture.
For daily use on the build machine, the `qbs run` command is the supported launch
route and retains correct build-time library paths.

If a self-contained `.app` is needed, prepare it separately using the upstream
packaging sequence: `qbs install --install-root local-install ...`, `macdeployqt`
on the resulting `Tiled.app`, and the upstream install-name fixup. See
`.github/workflows/packages.yml` for the complete platform packaging recipe.
That deployment/signing/notarization sequence is **not validated by our focused
CI**; do not present a raw build-folder `.app` as a tested portable installer.
No signing certificate or Apple-account credentials are requested here.

## Mac: native diagnostic tests

Run the model and drawing regressions in the ordinary configuration, or enable
the additional internal-widget diagnostics in a separate configuration:

```sh
qbs build profile:tiled-qt config:native-check qbs.buildVariant:debug \
  projects.Tiled.nativeWidgetTests:true -p tiled,test_layers,test_uistyle,test_layerwidgets
QT_QPA_PLATFORM=cocoa TILED_REQUIRE_COCOA=1 \
  TILED_WIDGET_ARTIFACTS="$PWD/native-widget-results" \
  qbs run profile:tiled-qt config:native-check qbs.buildVariant:debug \
  projects.Tiled.nativeWidgetTests:true -p test_layerwidgets
```

`nativeWidgetTests` is **off by default**. When explicitly enabled it exports
internal editor symbols for tests; this diagnostic binary is not byte-identical
to a normally packaged production binary. Tests require Cocoa and genuinely
exposed/active windows; no offscreen fallback is accepted. They exercise actual
LayerDock actions/menus, all four layer types, rename accept/cancel, document
switch/detach, nested deletion/undo/redo, floating/redocking, and the themed
material/opaque fallback. Compositor images with changing colored backgrounds
are used to check visible material response and an unchanged opaque canvas.

## Windows

Use Visual Studio 2022 C++ desktop tools, Qbs and the matching Qt 6.8.3
`msvc2022_64` kit (including the upstream-required private development headers).
Run in a Developer PowerShell; do not mix MinGW and MSVC libraries.

```powershell
$QtDir = 'C:\Qt\6.8.3\msvc2022_64'
$env:PATH = "$QtDir\bin;$env:PATH"
qbs setup-toolchains --detect
qbs setup-qt "$QtDir\bin\qmake.exe" tiled-qt
# Substitute the actual detected MSVC x64 profile from qbs config --list.
qbs config profiles.tiled-qt.baseProfile MSVC2022-x64
qbs build profile:tiled-qt config:release products.python.condition:false
qbs run profile:tiled-qt config:release products.python.condition:false -p tiled
```

## Verification record

Before the material/donation change, [run 41018503219](https://github.com/juliansoul250/tiled/actions/runs/41018503219)
at `f00d5bd66cf86d250f6e5e18342cb1bbd92d7381` passed:
- macOS 14.8.9 arm64 / Qt 6.8.3: Cocoa widget suite **5 passed** (three scenarios
  plus setup/cleanup), eight actual popup/action cycles, no failures/skips.
- macOS and Windows: layer **14/14**, drawing UI **38/38 at 1x and 2x**.

Those are **pre-material** results, not evidence that the new visual change
passed. Review the latest run for the exact installed commit and its compositor
screenshots before treating the new material as verified. CI uses standard
macOS/Windows hosted runners, with no purchase, deployment or notarization.

Focused success does not reproduce or resolve an unidentified user Mac crash.
Keep the `.ips`, exact commit/app version, macOS version and last action when
reporting one. The unrelated baseline Properties test failure and full-suite
limits remain documented in `layer-stability-validation.md`.
