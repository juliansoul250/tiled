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
for the declared Qt 5.15.2/Qt 6 API range. Linux Qt 6.8.2 debug builds and tests
passed as described in `layer-stability-validation.md`. Native Qt 6.8.3 Qbs
builds and focused tests also passed on **macOS arm64** and **Windows x86-64**
(see the exact run below). Qt 5.15.2, Intel macOS, Windows ARM64, signed packaging,
and interactive native-platform behavior remain unverified.

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

## GitHub native verification

`.github/workflows/layer-ui-validation.yml` supports manual `workflow_dispatch`
and pushes **only to `fix/layer-stability-ui`** that change the workflow or build
inputs (`tiled.qbs`, `qbs/`, `src/`, `tests/`, `dist/`, `translations/`). Doc-only
pushes do not rerun it. It uses standard `macos-14` and `windows-2022` hosted runners,
Qt 6.8.3, and Qbs. It builds the editor and focused layer/UI tests, then runs UI
checks at 1x and 2x and uploads text test results. It has read-only repository
permissions, no signing, no deployment, no release creation, and no secret
inputs. Windows selects the actual installed MSVC x64 compiler using `vswhere`
and registers a named Qbs profile instead of assuming an auto-detected profile
name.

[Run 40814104903](https://github.com/juliansoul250/tiled/actions/runs/40814104903)
completed successfully on 2026-10-29 at commit
`8ca31908816d677d103bce3c76700eeb079bfaea`:

- macOS 14.8 / arm64, Xcode 15.4 / Apple LLVM 15, Qbs 3.3.1, Qt 6.8.3.
- Windows Server 2022 / x86-64, MSVC 2022 toolset 14.44.35207, Qbs 3.3.0, Qt 6.8.3.
- Each native job: shared editor debug build succeeded; layer suite **14/14**,
  UI suite **38/38 at 1x and 38/38 at 2x**, with zero failures or skipped tests.

The downloaded text artifacts confirm these counts. Offscreen UI tests have
nonfatal font lookup warnings; these are not hidden. Exact logs, artifact links,
and coverage limits are recorded in `layer-stability-validation.md`.
This is actual native automated validation, not a claim that the user's old
Mac crash was reproduced or that native interactive GUI checks were performed.

The workflow intentionally runs focused regressions, not a replacement for all
upstream tests. `layer-stability-validation.md` documents the unrelated existing
Properties test failure and relative-path CLI issue rather than hiding them.

After a native build, manually verify square/isometric/hex maps, all four layer
types, grouping, delete/undo/redo, undo-branch replacement, save/reopen, light and
dark themes, keyboard focus, retina/high-DPI, menu shortcuts, and native dialogs.
Only after that validation should native interactive behavior be called tested.
The CI run above does not package, sign, notarize, or publish binaries.
