# Arcade Shelf v1 progress

Detail for `qa-p1/Arcade-catalog` › `dev/v1/PROGRESS.md`. Newest first. "Tested locally" names the command;
"not run" says why.

## 2026-11-14 (cloud session)

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
