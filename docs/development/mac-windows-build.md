# macOS / Windows installation handoff for this fork

> **Mac SDK selection (2026-10-30):** use the complete **Qt 6.11.3** kit.
> Do not rebuild this fork with Qt 6.8.3: native accessibility stress reproduces
> a crash on that historical kit; the user's matching plugin independently maps
> to the known cache teardown. Qt 6.11.3 passed the targeted native
> regression. Final packaged-app validation is a separate required gate below;
> a passed source test does not mean every possible crash is fixed.

Repository: https://github.com/juliansoul250/tiled
Branch: **`fix/layer-stability-ui`**
Base: Tiled **v1.12.2**, `1fd984638f9898af920103799bcd68b467287295`.

This is source code, **not a verified downloadable Mac installer**. There is no
new signed/notarized release from this fork. Use only an artifact from a successful exact-commit validation run, or build
locally below; record `git rev-parse HEAD` with any crash report.
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
- Mac validation now uses Qt **6.11.3**, with required macOS 14 and `xcode-27`
  hosted runners. Each run records actual `sw_vers`, architecture and Xcode:
  runner labels alone are not OS-version evidence. Windows retains its existing
  Qt 6.8.3/MSVC configuration. Intel/physical Retina/GPU combinations remain
  unverified; hosted-runner success is not a guarantee for all user environments.

## Mac: reproducible production package

Install Xcode/Command Line Tools, Qbs (`brew install qbs`) and the complete
**Qt 6.11.3 desktop macOS kit**, including Widgets, QML, SVG and image formats.
Never mix frameworks or deployment tools from a different kit. Do not use a
moving Homebrew Qt release as a substitute.

```sh
git clone --branch fix/layer-stability-ui --single-branch \
  https://github.com/juliansoul250/tiled.git
cd tiled
git rev-parse HEAD
export QT_ROOT_DIR="$HOME/Qt/6.11.3/macos"  # actual installed desktop kit
bash dist/macos/build-fork-package.sh
```

The script rejects every Qt version except 6.11.3, uses absolute `qmake` and
`macdeployqt` paths from that single kit, and creates fresh temporary build and
install directories. Production symbol visibility is unchanged (the internal
widget-test option is off). It deploys Qt, applies upstream install-name fixups,
audits all bundled Qt framework versions, requires the Cocoa plugin, rejects
external developer library paths, records the build identity, and ad-hoc signs.
It then copies the `.app` outside the build tree, launches that copy directly
without Qt/DYLD lookup environment overrides, opens the sample map, captures
the real window, and requires a clean normal quit before creating the ZIP.
Temporary build/delivery folders are printed and retained for diagnostics.

Output: `Tiled-macOS-Qt6.11.3-ad-hoc.zip` and `.zip.sha256`.
This is **ad-hoc signed, NOT Developer ID signed or notarized**. It is not a
notarized installer. No signing credentials are needed. If macOS blocks opening,
stop and report the message; do not disable or bypass OS protections.

### Secretary handoff

1. In the branch's **Validate layer fixes on macOS and Windows** Actions run,
   verify the exact commit and that **all jobs**, including `macos-package`, pass.
2. Download artifact **Tiled-macOS-Qt6.11.3-ad-hoc**, verify the ZIP using
   `shasum -a 256` against the accompanying checksum, and extract it.
3. Quit the old app, retain a backup, and copy the new `Tiled.app` to the chosen
   Applications folder. Do not launch a stale Dock shortcut/build-folder copy.
4. Finder → Show Package Contents → `Contents/Resources/fork-build.json` records
   source commit, dirty status, Qt version, architecture, builder OS and signing.
   About also shows runtime/build Qt and the packaged commit. Confirm these
   match the intended CI run before diagnosing another crash.
5. Keep map/project backups. If opening is blocked, or a crash occurs, send the
   OS message or `.ips` with `fork-build.json` and the exact last action.

For a development launch only (not portable packaging):

```sh
qbs setup-toolchains --detect
qbs setup-qt "$QT_ROOT_DIR/bin/qmake" tiled-qt
qbs config profiles.tiled-qt.baseProfile xcode
qbs build profile:tiled-qt config:release products.python.condition:false
qbs run profile:tiled-qt config:release products.python.condition:false -p tiled
```

Use the detected Apple compiler profile instead of `xcode` if named differently.
The kit and compiler must match the target architecture. The upstream generic
`packages.yml` is not the sanctioned fork handoff; its legacy Qt matrix is not
this crash regression's validation matrix.

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

[Run 41036743240](https://github.com/juliansoul250/tiled/actions/runs/41036743240)
at `d366d8087439c694807f4c4c4329de9008b6547f` selected Qt 6.11.3:
- Full native suite **8 passed**, separate Reduce Transparency suite **3 passed**.
- All **160 native accessibility cycles** passed; actual sample app quit exited
  zero. The earlier native-view warning was absent.
- Qt 6.8.3 remains a historical failing diagnostic, not an allowed-failure gate
  that can make the deliverable green. It reproduced SIGSEGV in the targeted
  accessibility test; no old-kit package is approved by this record.

The new required workflow adds ordinary map save/reopen coverage, runs one continuous native process with repeated editing for at least
**15 minutes** on `xcode-27`,
and separately builds/audits/smokes a relocated production `.app`. The separately deployed production app stays open on the sample map
for a further **15 minutes** before normal quit. That production-app hold is
idle stability coverage, not automated editing inside the packaged app. A timeout, native assertion, crash, nonzero quit or hierarchy
warning fails validation. Check the latest exact-commit run for these results;
the previous run above did not execute these newly added package/soak gates.

Historical [run 41031506290](https://github.com/juliansoul250/tiled/actions/runs/41031506290)
at `b47fbde402afcb564be286d1b29f77f5b1e187d5` established native material
screenshots on Qt 6.8.3 but remained red due to the old Quit harness. It is
retained as evidence, **not** permission to install that vulnerable kit.

The unrelated baseline Properties test failure and full-suite limits remain
in `layer-stability-validation.md`. No finite test run establishes that every
possible application crash has been eliminated.
