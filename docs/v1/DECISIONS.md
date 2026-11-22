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

## D6 — Step 5: memory

Follows the audit's `AUDIT/memory.md` items U1, W0 and SH1 (and SPEC "Memory": background budget 40 PSS / 48 RSS).
Link moves to v1.0.10 first (`2748ebf`, `scripts/vendor-link.py --revision v1.0.10`; `check-vendor.py` also passes
against the Link checkout; the three reusable-workflow pins and the stale `v0.3.0` lines in `VENDORED.md` and
`docs/dev/` follow). 1.0.9 touches only Link's own harness and workflows. 1.0.10 adds `qt/arcade_unpacked.h` and makes
`ArcadeInstall::uninstall` remove the app's own unpacked copies.

**Done**

- **U1, a resident start runs unpacked.** `main()` calls `arcade_relaunch_unpacked("arcade.shelf", version, 0, argv)`
  right after `ArcadeLink::onStartup()` and before any `QApplication`; a code >= 0 is returned at once (the launcher
  exits 0), -1 (not an AppImage, or already the copy) carries on. The decision is `Shelf::startsResident` in
  `src/Startup.h`, a pure function tested in `tst_Memory`. It is an allow-list: only `--background`, `--show`,
  `--toggle`, `--settings`, `--add <path>` and `--wait-for-exit <pid>` (and no arguments) relaunch. Everything else
  runs from the mount as before: the install commands, `--status`, `--quit`, `--hide`, `--restart`, `--version`,
  `--help`, the manifest, a malformed command line and any unknown option. Box's rule is "one-shot commands never
  relaunch"; an allow-list errs the other way from a deny-list, so a command line Shelf does not know can never hand
  its error to a copy whose exit status the launcher drops.
- **The relaunch comes before the install prompt.** `offerInstall` needs a `QApplication`, so it runs in the copy; the
  copy sees the AppImage as `$APPIMAGE` and its own `$APPDIR`, so the prompt, an install and the receipt behave as
  before (`appimage_lifecycle.py` passes against the real image). The price is one extraction for a user who is shown
  the prompt and installs at once.
- **Known cost of wait=0.** The launcher cannot tell a first start from `--show`, `--toggle`, `--settings` or `--add`
  forwarded to a running Shelf, so a forwarded call that fails (for example `--add` of a missing file) prints its
  error from the copy but the launcher's exit status is 0. The audit and Link's RT-7 take the same trade for a
  resident start.
- **W0, trim.** `arcade_trim()` runs once 3 s after the event loop starts and 2 s after the panel closes. Shelf's
  window stays up as a capsule or docked tab, so "hide" is the panel closing: `Shelf::HideRelease` is driven by
  `Controller::changed` with "expanded and visible" (this also covers a hover peek ending and the fullscreen guard
  hiding Shelf). Opening again cancels the pending release; there is no periodic timer. Glibc only, like Wheel.
- **SH1, thumbnails.** Default cache 16 MiB (the provider and the `thumbnailQuotaBytes` default), `setCacheLimitBytes`
  clamps to 0..64 MiB, and the cache is cleared with the trim 2 s after a close. Thumbnails decode again on reopen.
  The preference's accepted range (up to 1 GiB) is unchanged on purpose: tightening it would reject backups and
  settings files that already hold a larger value. `saveSettings` stores every default with the first change, so an
  install that has saved any setting keeps its stored 64 MiB: it is honoured (it is the cap), the clear on hide is
  what frees the memory for it.
- **Uninstall.** `--uninstall` goes through `ArcadeInstall::uninstall` (`AppInstaller.cpp`, the Linux and macOS path; the
  removal is Linux code, and Windows uses its own uninstaller and has no unpacked copies), which in 1.0.10 removes `<data>/arcade/runtime/shelf-*` and interrupted
  `.shelf-*.tmp-*` copies and leaves other apps'. Shelf adds no second removal. `appimage_lifecycle.py` checks it.
- **Packaged checks follow the resident.** CI's inline "show, `kill -0` the launcher, quit" step assumed the launcher
  is the app and failed on a working build; `tests/appimage_resident.py` replaces it (see `docs/dev/testing.md`).
  Link's e2e harness sets `ARCADE_UNPACKED=1`, which makes the relaunch do nothing there.

**Not done, and why**

- W1 (release after the last window), W3b (page-out of hidden WebKit heaps) and OPEN1 (trim while a window is open)
  are for WebKit apps; Shelf has no WebKit. `arcade_page_out_hidden` is not called.
- WH1 (software renderer) is Wheel's; Shelf already forces Qt's software renderer (`main.cpp`).
- No second trimmer while the panel is open: the audit measured the open panel as +0...2 MB.
- The budget (40 PSS / 48 RSS) is not measured here: the lab harness and a real session were not available.

## D7 — 1.0.1: a square surface when nothing composites

- **Found in the owner's real-user run of Tools 1.0.1** (X11, openbox, no compositing manager): the capsule's rounded
  card sat in a black box. The window is transparent around the card for its corners and the kit's shadow; without a
  compositor X11 shows those pixels black.
- **Decision:** `Desktop::composited()` is false only on X11 when no client owns `_NET_WM_CM_S<screen>` (the
  compositing manager selection, as GTK and Qt themselves check it); Wayland, Windows and macOS always composite. It is
  checked at start and every 5 s, so a compositor started later (picom from a session script) changes the look. When
  false the surface fills the window with square corners, so the shadow (drawn only in the margin) is gone too. The
  docked tab and Hyprland's rounding are unchanged. Test: `withoutACompositorTheSurfaceFillsTheWindow` (CI's Xvfb runs
  no compositor).
- Link's reusable workflows at v1.0.15 (they were at 1.0.13 and 1.0.14).
