# macOS / Windows installation handoff for this fork

> **macOS installation hold (2026-10-30):** a supplied crash has now been
> independently mapped to Qt 6.8.3's Cocoa accessibility column-cache teardown.
> Do not package/install another Mac build with the historical Qt 6.8.3 kit below.
> Native accessibility regression comparisons are in progress; a green rendering
> test alone does not certify this crash path. No replacement kit is certified yet.

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
The CI runner also launches the actual built Tiled application with the included
`examples/desert.tmx`, captures its desktop window, sends a standard Quit
AppleEvent, and requires the actual application process to exit with code zero.
This uses no production test hook or Accessibility input automation. A timeout,
permission denial, nonzero application exit, or AppKit hierarchy warning fails
validation. [Qt 6.8.3's Cocoa delegate](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/cocoa/qcocoaapplicationdelegate.mm#L98-L103)
returns `NSTerminateCancel` even after accepting quit, to unwind through `main()`.
The harness records that specific AppleEvent `-128` reply but only accepts it
when the independently awaited process exits zero; forced cleanup is never a
successful close test. Artifacts include the reply, exit result and app console.

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

[Run 41031506290](https://github.com/juliansoul250/tiled/actions/runs/41031506290)
at `b47fbde402afcb564be286d1b29f77f5b1e187d5` established macOS 14.8.9 arm64 /
Qt 6.8.3 native material evidence:
- Cocoa widget suite **6 passed**, no failures/skips; eight actual popup/action
  cycles, floating/redocking, rename/document lifecycle and native material.
- Separate real Reduce Transparency suite **3 passed**, with original runner
  preference restored and read back (`1` to `1`). No AppKit hierarchy warning.
- Compositor samples changed from RGB `(221,201,196)` to `(196,211,221)` when the
  background changed; opaque canvas pixels stayed identical.
- Actual built Tiled displayed `desert.tmx`, readable controls and two native
  material docks; full-app screenshot is in `macos-cocoa-widget-results`.

That historical run remained red because its initial smoke harness rejected
Qt's `-128` Quit reply before checking process exit. The corrected harness above
requires a clean actual exit. **Use the latest run for the exact installed
commit's overall result**, including both platform builds, layer regressions
and drawing UI regressions at 1x/2x. A material screenshot alone is not an overall
pass. CI uses standard hosted runners, with no deployment or notarization.

Focused success does not reproduce or resolve an unidentified user Mac crash.
Keep the `.ips`, exact commit/app version, macOS version and last action when
reporting one. The unrelated baseline Properties test failure and full-suite
limits remain documented in `layer-stability-validation.md`.
