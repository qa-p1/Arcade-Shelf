# Arcade Shelf v1 progress

Detail for `qa-p1/Arcade-catalog` › `dev/v1/PROGRESS.md`. Newest first. "Tested locally" names the command;
"not run" says why.

## 2026-11-18 (cloud session) - 1.0.0 promoted

`v1.0.0-rc.4` (e8b4a26, Link 1.0.13, Tier 2 from Link 1.0.14) passed
[Gates](https://github.com/qa-p1/Arcade-Shelf/actions/runs/44222853395) 13/13 on its published files: published files,
delta from v0.4.0, memory and performance budgets, Linux conformance, Windows and macOS installer smoke, N-1, the upgrade
from 0.4.0 with the user's data, and both desktop suites (Tier 1 Hyprland; Tier 2 sway, KWin, Mutter, X11).
[Promote](https://github.com/qa-p1/Arcade-Shelf/actions/runs/44226166895) made it
[1.0.0](https://github.com/qa-p1/Arcade-Shelf/releases/tag/v1.0.0), now the latest release. The AppImage, dmg, setup.exe
and SHA256SUMS.txt have the same SHA-256 digests as rc.4; `manifest.json` and `arcade-release.json` differ only in the
channel (`test` to `stable`), the tag and the notes link, and `manifest.json.sig` is new.

What the earlier rcs' Gates found, each fixed before the next rc:
- rc.1 (d416f14): the gates runner lacked libraries the AppImage takes from the desktop (`libEGL.so.1`), so every CLI
  conformance check exited 127. #22 installs the full list.
- rc.2 (c619159): CLI-2, an unknown option exited 1 instead of 2 with the usage. Fixed in #23 (changelog "Fixed").
- rc.3 (0e5da77): the upgrade job could not start 0.4.0 without libOpenGL (#24 installs every library both AppImages
  take), and Tier 2's Mutter job timed out installing packages from a slow mirror (Link 1.0.14 caches and prefetches
  them, #24).

## 2026-11-15 - step 5, memory (local session, branch `local/shelf-step5`)

- Done (DECISIONS D6): Link v1.0.8 to v1.0.10 (`2748ebf`); U1 (resident starts run from the unpacked copy,
  `Shelf::startsResident` decides); W0 (`arcade_trim()` 3 s after start and 2 s after each close, cancelled by a
  reopen); SH1 (thumbnail cache 16 MiB default, 64 MiB cap, cleared with the trim); uninstall's removal of Shelf's own
  copies confirmed to go through Link's `ArcadeInstall::uninstall` and covered in `appimage_lifecycle.py`; CI's inline
  AppImage step replaced by `tests/appimage_resident.py`, which follows the resident. W1, W3b, OPEN1 and WH1 do not apply.
- Tested locally, Qt 6.8.3, Ubuntu 24.04, final tree:
  - `xvfb-run -a python3 scripts/check.py --build <dir>`: build with warnings as errors, clang-format 18, qmllint at
    zero warnings, `check-vendor.py` (v1.0.10, 114 Qt and kit files, 25 shared), `check-shortcuts.py`,
    `check-doc-links.py`, `test_arcade_release.py`, and 20/20 ctest (offscreen, software). New: `Memory`, `LinkTrim`,
    `LinkUnpacked`, and two cases in `Thumbnail`.
  - `check-vendor.py --upstream /home/user/Arcade-Link`: all hashes match the pinned commit.
  - `QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software xvfb-run -a ctest`: 20/20 (CI's "Linux X11" step).
  - New tests fail without the change: `startsResident` forced true (all 23 one-shot rows of the table fail), `HideRelease` not
    cancelled by a reopen (`aReopenBeforeTheDelayCancelsTheRelease` fails), thumbnail defaults and `clearCache`
    reverted (both new `Thumbnail` cases fail), and with the relaunch call disabled `appimage_resident.py` fails on
    an AppImage built that way ("launcher is still running after 120 s"). CI's old inline step run against the new
    AppImage fails (the launcher is gone), which is why it was replaced.
  - Packaged, as CI's package job does: `scripts/package-linux.py` (AppImage 0.4.0, 99 libraries), `--appimage-extract`
    then `tests/process_smoke.py --binary <extracted>` (14 tests), `xvfb-run -a python3 tests/appimage_resident.py
    --appimage <file>` (passes: launcher exits 0, resident runs from `<data>/arcade/runtime/shelf-0.4.0-<16 hex>/`,
    `--status` and `--quit` follow it, no runtime process left) and `dbus-run-session -- python3
    tests/appimage_lifecycle.py --appimage <file>` (2 tests, including the new uninstall-removes-copies check). All
    with `APPIMAGE_EXTRACT_AND_RUN=1`, as CI.
  - W0 spot check: a `--background` resident on the extracted binary went from 66 MB RSS (50 MB file pages) at 1.5 s
    to 19.5 MB (3.6 MB file pages) at 6.5 s. A lab number, not the audit's budget.
- Not run: Windows and macOS builds and installers (CI only; the changed code is Linux-guarded or portable Qt);
  the AppImage on a real FUSE mount (extract-and-run only, as CI); the audit's lab harness and the 40 PSS / 48 RSS
  budget; a real desktop or GPU. Link's `v1.0.10` tag was not yet on the remote when this was vendored, so the pin
  was made from the commit through a scratch clone tagged locally; the reusable-workflow pins (`@v1.0.10`) resolve
  only once Link publishes that tag.

## 2026-11-15 - step 4 (cloud session, branch `claude/sharp-cori-29w5eg`)

- Dead code removed, one commit each (DECISIONS D5, with how each was proved unused): `Store::add` split into stages
  (D4, `e2b6fa8`, not a removal); `isIntegrationCommand` (`3412589`); `Desktop::Native` `bound*` fields (`f3ea316`);
  `Controller::backdropStatus` (`d0e76b0`); `Controller::backdrop()` (`0b62e5e`); `scripts/release-notes.py`
  (nothing calls it since the release workflow moved to Link's publish); four unused QML ids in `Main.qml`.
  Kept after checking: `arcade-release.py` and its test (CI and `check.py` use them), the `licenses/` directory
  (installed wholesale), every other script, asset, QML file, Qt component and link library.
- Tested locally: `xvfb-run -a python3 scripts/check.py --build /home/user/shelf-step4-build` with Qt 6.8.3 (build
  with warnings as errors, clang-format 18, qmllint, vendor/shortcut/doc-link checks, `test_arcade_release.py`,
  17/17 ctest including `tst_Ui`): All checks passed, 100% tests passed, 0 tests failed out of 17. Windows and macOS
  code compiles only in CI.

## 2026-11-14 (cloud session)

- Link v0.3.0 → v1.0.8 (DECISIONS D3): `scripts/vendor-link.py --revision v1.0.8`; `check-vendor.py` passes (111 Qt
  and kit files, 25 shared). Tested locally: `scripts/check.py` with Qt 6.8.3 (format, build, 17/17 ctest). The
  real-fleet XFAIL `arcade_shelf_update_retires_old_desktop_ids` should pass once this is on main. The format check
  also caught the split's `ownHyprlandWindow` continuation indent (fixed in its own commit).
- Step 2, untangle: `src/platform/Desktop.cpp` (1,852 lines, four concerns) is split by concern behind the unchanged
  public `Desktop` interface (DECISIONS "Untangle"): `DesktopPlacement.cpp`, `DesktopFiles.cpp`,
  `DesktopShortcut.cpp`, `DesktopLogin.cpp`, with `Native` in the private `DesktopPrivate.h`. Moved mechanically
  from line ranges; a line-multiset check against the old file shows only the intended edits (a default argument
  moved to its declaration, a forward declaration dropped, the moc include renamed, one new private method that
  wires the shortcut portal from the constructor). Tested locally with Qt 6.8.3 (aqt): `scripts/check.py` (build
  with warnings as errors, clang-format 18, qmllint, docs, every ctest suite, 17/17, `tst_Ui` under `xvfb-run`
  included), and the five files compile without DBus/X11 too. Windows and macOS compile only in CI.

## 2026-11-13 (local session) - every spec finding fixed

- PR #12 (merged): `45c6868` SH-5 removal undo in its own collection; a pending removal is committed before any
  other store change (an add would otherwise find the hidden items). Tests: `tst_Service` and the UI remove/undo test.
- PR #13: `8233b69` SH-8 (P0) a peeked panel never collapses by itself (UI tests: hover and drag leave keep it open);
  `3d8db5f` SH-2 links sized/typed by target, dangling refused (`broken_link`), SH-1 `--add` names a missing file;
  `8a18519` SH-4 quota in MiB both ways, "Not registered yet" shortcut caption, honest blur text; `c86f195` SH-6
  collection menu enables only possible moves; `7d5e29c` SH-3 feedback no longer overlaps the hints, RD-SHELF-2 hover
  text; `089488f` SH-7/RD-SHELF-4 `--status` mode follows the panel and restart keeps it (process test).
- Tested locally: all 16 non-UI ctest suites; `tst_Ui` under `xvfb-run` (QT_QPA_PLATFORM=xcb) passes except
  `shortcutRecorderSurvivesIncomingAddsAndCommits`, which fails the same way on the unchanged code here (local
  display quirk; CI runs it).
- Open: RD-SHELF-3's second half (Hyprland does blur with a window rule; Shelf installs no compositor rules, so it
  can't offer it); the footer's long-message elision belongs in Link's `HintBar`. Then steps 2 and 4-8.

## 2026-11-10 (code-writing pass)

- Step 0: brief copied (SPEC, audit, DECISIONS, PROGRESS). Not code; nothing to run.
