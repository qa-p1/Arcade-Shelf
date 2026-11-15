# Arcade Shelf v1 decisions

Decisions the plan left open, made by the v1 code-writing pass. Each cites the part of the catalog `dev/v1/README.md`
or `ARCADE_APP.md` it follows. Newest last.

## D1 — Spec and audit copied at catalog `dev/v1`

`docs/v1/SPEC.md` is `specs/SHELF.md`; `docs/v1/audit/` holds `AUDIT/shelf.md`, its text evidence (`r-*.json*`,
`r-install.txt`, `r-source-revision.txt`, `resume-cli.txt`) and the Shelf section of `AUDIT/real-desktop.md`
(`real-desktop-shelf.md`). Line numbers in the spec are for Shelf 0.4.0 (`3f3d1fd`).

## D2 — Local builds use Qt 6.4, CI uses Qt 6.8.3

The sandbox has Qt 6.4.2 only and no way to fetch 6.8. Local verification builds a scratch copy of the tree with
four mechanical shims (the `QProcess` fd-closing API, `QQmlApplicationEngine::loadFromModule`, `QTimeZone::utc()`,
the Qt 6.5 resource prefix). The repository itself still requires Qt 6.8; nothing for 6.4 is committed. Tests that
need Qt 6.8 behaviour (some `Thumbnail`, `LinkInstall` and `ProcessSmoke` cases) are recorded as "not run locally"
in PROGRESS.

## D3 — Link is vendored at the tag v1.0.8
`python3 scripts/vendor-link.py --link ../Arcade-Link --revision v1.0.8` replaced the v0.3.0 copy (commit `5679854`)
with the tag's (`34f8030`): the Qt module, the QML kit, Inter and the shared Windows, shortcut and release files;
`scripts/check-vendor.py` confirms all 136. Why: the fleet rule "One launcher per app". The 1.0.7 Qt installer
retires the entries named in `legacyDesktopIds` (`arcade.shelf`, `io.github.qa_p1.ArcadeShelf`) on update and carries
Start at login over. 1.0.8 adds the eliding HintBar (SH-3) and `arcade_trim.h` (RT-5, for step 5).

## Untangle (step 2): what changes together

`src/platform/Desktop.cpp` (1,852 lines) is one class with four reasons to change, sharing one private `Native`
struct defined in the .cpp:
- window placement: move, snap, dock, arrange, and the Hyprland reconciliation that follows the compositor;
- opening and revealing files, and the MIME data for dragging items out;
- the global shortcut: validation, the portal session, and the Hyprland binding;
- start at login: the per-OS entry paths, ownership checks against the install receipt, and the legacy-id migration.

Each is reached from `Controller` through a few methods only. The fix: move `Native` into a private header, then
split the class's implementation by concern (`DesktopPlacement.cpp`, `DesktopFiles.cpp`, `DesktopShortcut.cpp`,
`DesktopLogin.cpp`) behind the same `Desktop` interface, so a change to one concern reads one file. Done on
2026-11-14: `Native` and the two shared helpers (`runProgram`, `isolatedDesktop`) are in `DesktopPrivate.h`;
everything else that only one concern used moved with it, and `Desktop.cpp` keeps construction, errors and the
native event filter (it serves both the move gesture and the Windows hotkey).

## D4 — `Store::add` in stages (untangle)
`Store::add` was one 360-line function: option checks, spreading `[]` inputs, sizing the response, checking and
reading each input against its declared type (paths, symlinks, handoff ownership, text and HTML files, URLs, limits),
the duplicate check, staging handoff copies, the insert, and the summary message, with skip reasons and limit
bookkeeping threaded through local flags. It now reads in stages, each a function in `Store.cpp`:
`expandInputs`, `responseFits`, `resolveInput` (one input to an `Input`, or a skip reason with the size limit that
applied; it touches no database), then `add`'s loop (duplicates, quota, staging, insert) and `addMessage`. The
statements are unchanged: each `skip(...); continue;` in the checks became a returned reason, and where a limit was
lowered (`rejectedLimit = qMin(rejectedLimit, X)`) the `Input` carries `X`. `tst_Store` (59 `add` calls) passes
unchanged.

## D5 — Dead code (step 4)
Everything below was proved unused by a textual search over `src/`, `tests/`, every `*.qml`, `scripts/`, `packaging/`,
`CMakeLists.txt`, `.github/` and `docs/`, including the Windows, macOS and `.mm` branches that compile only in CI.
A `Q_PROPERTY` or `Q_INVOKABLE` counted as used when any QML file names it (`shelf.<name>`, `on<Name>Changed`).
Removed, one commit each:

- `Store::add` was split in D4 (`e2b6fa8`); that is a refactor, not a removal.
- `ArcadeApp::isIntegrationCommand` (`AppInstaller`): no caller; `main.cpp` uses `isHeadlessIntegrationCommand` and
  `runIntegrationCommand`.
- `Desktop::Native::boundKey`, `boundModifiers`, `boundTarget`: never read or written, in any `#ifdef` branch; Hyprland
  binding state is `hyprBound` and `bindingToken`.
- `Controller::backdropStatus` (Q_PROPERTY): no QML, test, script or doc names it, and no `property("...")` access
  exists. `Backdrop::status()` stays: `copyDiagnostics` prints it.
- `Controller::backdrop()` accessor: no caller; the controller uses `m_backdrop` directly.
- `scripts/release-notes.py`: no workflow, script, CMake or packaging file and no document names it (`grep` for
  `release-notes` finds only the file itself). Its only caller, the old tag-to-release job, went in `5287f11`.
- Four unused QML ids in `ui/Main.qml` (`card`, `capsuleMenu`, `surfaceMenu`, `itemMenu`): `grep -w` over `ui/` and
  `tests/` finds only the declarations.

Checked and kept (each has a real use):

- `scripts/arcade-release.py` and `scripts/test_arcade_release.py`: `ci.yml` runs the first for the nightly manifest,
  `docs/dev/packaging.md` documents it, and `scripts/check.py` already runs the second. Both are also listed in
  `scripts/vendor-link.py`.
- Every other script: called by a workflow, another script or a doc (`scripts/dev`, `package` and `sync-design` are
  the developer entry points named in `CHANGELOG.md`).
- `licenses/*.txt`: the whole directory is installed and copied into every package (`Packaging.cmake`,
  `package-linux.py`, `package-macos.sh`), whether or not a file is named elsewhere.
- Icons, `assets/glyph.svg` and the Link files: vendored (`scripts/vendor-link.py`, the VENDORED pins) or referenced
  from CMake, the packaging scripts or the README.
- All four QML files are instantiated (`Main` loads `SettingsWindow` and `DockMenu`; `AppInstaller` and `tst_Ui` load
  `InstallPrompt`); every Qt component in `find_package` and every linked library has an include, a link-time or a
  runtime user (`Svg` for the tray glyph, `X11` and `DBus` for the `ARCADE_SHELF_HAVE_*` paths).
- `Q_PROPERTY` and `Q_INVOKABLE` markers on `Backdrop`, `Desktop`, `FullscreenGuard` and `PeerHub`: those classes are not
  exposed to QML, but they are metadata on members that C++ calls, so removing them would not remove code.
  `Controller::clearSelection` and `newCollectionFromTray` are `Q_INVOKABLE` but called only from C++; the functions
  are used, so they stay.
