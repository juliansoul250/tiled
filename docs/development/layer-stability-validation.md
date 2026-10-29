# Layer stability and editor polish validation

Baseline: official Tiled **v1.12.2**, commit
`1fd984638f9898af920103799bcd68b467287295`.

## Changes

- Invalidate cached object-tree children for a removed group and all nested
  groups. Previously, clearing/truncating undo history could free a group while
  the object tree retained its pointer as a cache key. Reusing that address for
  a new empty group exposed stale child pointers.
- Enforce valid row/column bounds and first-column-only children in both layer
  tree models. The unmodified baseline fails Qt's model contract checker.
- Refine the native Qt editor's dark surfaces, spacing, dock headers, tabs, and
  toolbar states. New empty profiles get graphite/teal defaults; saved profiles
  and the native/light styles remain available. Tooltips, shortcuts, commands,
  document behavior, and file formats are not replaced.

This is a Linux reproduction and repair of demonstrated defects. It does **not**
establish that either defect caused a particular crash in an unknown, deleted
macOS installation. No macOS crash report, affected project, or macOS runtime
was available for validation.

## Automated regression coverage

`tests/layers` adds:

- Both `QAbstractItemModelTester` checks, including grouping, removal, undo,
  and redo, plus invalid-column and non-name-column child assertions.
- 200 iterations of nested object groups, removal, clearing the undo history,
  and creating empty groups. The baseline exposes stale children; the patched
  version does not. A further 200 iterations cover creating a new edit after
  undoing, which discards and frees the redo branch.
- 40 create/group/delete/undo/redo/TMX-save/reopen cycles for each of square,
  isometric, and hexagonal maps, with finite and infinite variants: **240 cycles**.
  Layer types, names, IDs, visibility, lock state, hierarchy, and map geometry
  are compared after loading.

`tests/uistyle` covers palette switching, font-aware row heights, toolbutton
states, tab orientations, painting-state restoration, and actual lock/visibility
icon color and alpha behavior, at 1x and 2x scale.

Standard upstream build commands (configure an appropriate Qt Qbs profile first):

```sh
qbs build profile:qt6 config:debug
QT_QPA_PLATFORM=offscreen qbs run profile:qt6 config:debug -p test_layers
QT_QPA_PLATFORM=offscreen qbs run profile:qt6 config:debug -p test_uistyle
QT_QPA_PLATFORM=offscreen qbs build profile:qt6 config:debug -p tests
```

### Validation environment and build limitation

Debian 13 x86-64, GCC 14, Qt 6.8.2, debug builds. Signed Debian tool/development
packages were extracted into the private workspace, not installed system-wide.
The execution sandbox does not permit AF_UNIX socket creation, which Qbs 2.6.1's
process launcher requires. Qbs resolved the project but could not execute a
build. A separate qmake/Make harness, derived from the same explicit upstream
source lists, definitions, Qt dependencies, and resources, was therefore used
for the executed tests and GUI binaries. Qbs itself and its security environment
were not modified. The normal Qbs path and macOS build are **not verified here**.

The fallback builds the editor, all 16 default C++ format plugins, translations,
and the test executables. The optional Python plugin is not built. The original
baseline executable and libraries are retained separately from the patched build.

## Results and known baseline limitations

- Added layer suite: **14 passed, 0 failed**. The unmodified baseline fails the
  targeted tree-contract and stale-cache regressions.
- The complete added layer suite also passes in 25 independent processes
  (6,000 TMX lifecycle cycles and 10,000 group-cache/history cycles).
- Added UI suite: **38 passed, 0 failed** at both 1x and 2x, including
  dark checkbox edge contrast and lock icon palette/alpha checks.
- Existing AutoMapping: **15 passed**; MapReader: **3 passed**;
  StaggeredRenderer: **16 passed**.
- Existing Properties: **21 passed, 1 failed**, identically before and after the
  patch. `loadCircularReference` expects class A's `b` member to be removed, but
  it remains present. This unrelated baseline failure is not silenced or changed.
  The aggregate upstream suite is therefore **not fully green**.
- Bundled square, isometric, and hexagonal examples pass
  TMX → JSON → TMX → JSON equality when supplied as absolute paths, including
  tile counts and image/tileset references.
- A separate baseline CLI issue occurs with relative paths during the same
  conversion of embedded image tilesets: image references can be rebased
  incorrectly. It reproduces before this patch and remains unchanged. The
  compatibility checks use absolute filenames, as the GUI file dialogs do.

The map readers/writers and serialization formats are unchanged by this patch.

For the shared macOS/Windows source, native build instructions, portability
review, and branch-scoped/manual platform workflow (native results pending),
see `mac-windows-build.md`.
