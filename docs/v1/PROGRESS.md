# Arcade Shelf v1 progress

Detail for `qa-p1/Arcade-catalog` › `dev/v1/PROGRESS.md`. Newest first. "Tested locally" names the command;
"not run" says why.

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
