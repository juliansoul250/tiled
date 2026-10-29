# macOS and Windows source/build handoff

This branch contains the **shared C++/Qt editor source for both macOS and
Windows**, not a Linux-only rewrite. Base: official `v1.12.2`, commit
`1fd984638f9898af920103799bcd68b467287295`. The upstream Qbs project, macOS bundle
resources/entitlements, Windows resources/installer definitions, AppVeyor, and
existing GitHub workflows are retained unchanged.

## Compatibility and verification status

Tiled v1.12.2 declares **Qt >= 5.15.2**, **Qbs >= 1.18**, and C++17. Do not infer a
newer minimum from the moving master branch. Qt 6 is recommended for a new build;
Qt 6.8.3 is the fixed version in the new optional validation workflow.

The production changes use existing cross-platform Qt APIs: QAbstractItemModel,
QList/QHash, QPalette/QColor, QPainter/QPixmap, QStyle, and QSettings. No Linux
system call, filesystem convention, platform-specific crash workaround, or
conditional removal of a Mac/Windows feature was added. The source was reviewed
for the declared Qt 5.15.2/Qt 6 API range, but **only Linux Qt 6.8.2 was actually
compiled and run here**. Qt 5.15.2, macOS, and Windows builds and behavior are
**pending platform verification**, not certified by the Linux tests.

The shared new-user palette is only initialized for an empty settings profile;
existing system/native/light preferences remain available. It does not replace
native menus, keyboard shortcuts, or the map formats.

## macOS: supported upstream Qbs path

Prerequisites: Xcode/Command Line Tools, a compatible Qt desktop kit, and Qbs.
The upstream README supports Homebrew (`brew install qbs qttools`, which also
installs Qt), or use the official Qt installer to choose a fixed Qt version.
Keep the Qt kit and compiler architecture consistent (Intel or Apple Silicon).

```sh
# From the checked-out source directory. Point this to YOUR installed Qt kit.
export QTDIR=/absolute/path/to/Qt/6.8.3/macos
export PATH="$QTDIR/bin:$PATH"
qbs setup-toolchains --detect
qbs setup-qt "$QTDIR/bin/qmake" tiled-qt
qbs config profiles.tiled-qt.baseProfile xcode
qbs build profile:tiled-qt config:debug
QT_QPA_PLATFORM=offscreen qbs run profile:tiled-qt config:debug -p test_layers
QT_QPA_PLATFORM=offscreen qbs run profile:tiled-qt config:debug -p test_uistyle
qbs run profile:tiled-qt config:debug -p tiled
```

Check `qbs config --list` if the detected compiler profile is not named `xcode`,
and substitute the returned profile name. To prepare an unsigned local app,
follow the upstream `qbs install`/`macdeployqt` steps. Signing/notarization require
the owner's Apple credentials and are **not performed or bypassed** here.

## Windows: supported upstream Qbs path

Install Visual Studio 2022 C++ desktop tools, the **matching MSVC2022 x64 Qt kit**
(including its private development headers as required by the existing Windows
Qbs configuration), and Qbs. Use a Developer PowerShell/command prompt. Do not
mix a MinGW Qt kit with an MSVC compiler. The upstream project also supports a
matching MinGW kit; its existing workflows remain present.

```powershell
# Set this to YOUR installed Qt kit.
$QtDir = 'C:\Qt\6.8.3\msvc2022_64'
$env:PATH = "$QtDir\bin;$env:PATH"
qbs setup-toolchains --detect
qbs setup-qt "$QtDir\bin\qmake.exe" tiled-qt
qbs config profiles.tiled-qt.baseProfile MSVC2022-x64
qbs build profile:tiled-qt config:debug
$env:QT_QPA_PLATFORM = 'offscreen'
qbs run profile:tiled-qt config:debug -p test_layers
qbs run profile:tiled-qt config:debug -p test_uistyle
Remove-Item Env:QT_QPA_PLATFORM
qbs run profile:tiled-qt config:debug -p tiled
```

Use the actual MSVC profile name printed by `qbs config --list` if different.
Packaging/signing is separate from building the editor; no installer certificate
or upstream release secret is included or requested by this patch.

## Optional GitHub verification

`.github/workflows/layer-ui-validation.yml` adds a **manual-only**
`workflow_dispatch` using standard `macos-14` and `windows-2022` hosted runners,
Qt 6.8.3, and Qbs. It builds the editor and focused layer/UI tests, then runs UI
checks at 1x and 2x and uploads text test results. It has read-only repository
permissions, no signing, no deployment, no release creation, and no secret
inputs. This workflow has been syntax-reviewed but **has not run** while GitHub
write authentication is unavailable. It must not be presented as a green CI run.

The workflow intentionally runs focused regressions, not a replacement for all
upstream tests. `layer-stability-validation.md` documents the unrelated existing
Properties test failure and relative-path CLI issue rather than hiding them.

After a native build, manually verify square/isometric/hex maps, all four layer
types, grouping, delete/undo/redo, undo-branch replacement, save/reopen, light and
dark themes, keyboard focus, retina/high-DPI, menu shortcuts, and native dialogs.
Only after that validation should platform binaries be called tested.
