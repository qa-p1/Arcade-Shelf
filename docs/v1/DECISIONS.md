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
