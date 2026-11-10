# Spec 8: Arcade Shelf 1.0.0

**Outcome:** Shelf 1.0.0. It delivers:
- the docked panel never collapses on its own;
- symlinks and errors handled correctly;
- undo for removals;
- honest settings;
- real logs;
- C++ contract tests from Link passing.

Starting point: `qa-p1/Arcade-Shelf`, Shelf 0.4.0 (C++/Qt6 QML). Read `AUDIT/shelf.md` and
`AUDIT/real-desktop.md` §Shelf.

## Findings to fix

| ID | P | What | Where |
|---|---|---|---|
| SH-8 / RD-SHELF-1 | P0 | A hover-opened docked panel collapses 320 ms after the pointer leaves. Owner decision: it never collapses by itself; it closes on Escape, the shortcut, the tab or an explicit outside click. | `ui/Main.qml:195-201` (`peekDismissTimer`), `src/Controller.cpp:508-514` |
| WH-5 (shared) | P1 | Escape can't cancel destructive dialogs. Fixed in Link's QML kit (`specs/LINK.md` §2); bump the vendored kit. | `vendor/arcade-link/design/qml/Arcade/Dialog.qml:21` |
| SH-2 | P1 | Symlinks are collected as 0 B "any" items, and broken links are accepted. Resolve good links; reject broken ones with a message. | add path |
| SH-5 | P1 | Remove is immediate with no undo, and "Undo" refers only to the last addition, even in other collections. Add undo for removal, scoped to the current collection. | `src/link/Service.cpp:373,394-399` |
| SH-4 / RD-SHELF-3 | P1 | "Backdrop blur" ON while the text says it isn't available; the shortcut recorder shows "Ctrl Alt S" above "No global shortcut"; the quota shows MiB/10⁶ labelled MB; "Open logs folder" points at a folder Shelf never writes. Add real logs. | `ui/SettingsWindow.qml:190-194` |
| SH-1 | P2 | A nonexistent path says "can't open this kind of content". | CLI add |
| SH-3 | P2 | The footer feedback text overlaps the key hints. | `ui/Main.qml` footer |
| SH-6 | P2 | The collection menu offers actions that do nothing ("Move up" with one collection). | collection menu |
| SH-7 / RD-SHELF-4 | P2 | Restart reopens the panel instead of keeping its mode; `--status` reports "background" while the panel is open. | restart / status |
| RD-SHELF-2 | P2 | Settings say hover takes no keyboard focus; on Hyprland it does. | settings text |

## Also

- Add a LICENSE file (missing today) and `THIRD_PARTY_NOTICES` (Qt LGPL with relinking info).
- Pass the Link C++ contract suite (`spec/contract/`): restart via `$APPIMAGE`, single instance, `--status`, paths,
  logs, crash reports and child reaping.
- Automated drag and drop in and out (files, text, URLs) under the harness. The audit couldn't drive DnD with a
  virtual pointer.
- Test docking on all four edges.

## Memory

Items U1, W0 and SH1: the thumbnail `QCache` default goes from 64 MiB (`src/ThumbnailProvider.cpp:22`) to 16 MiB, cleared on hide. Budget: background 40 PSS / 48 RSS.

## CI

CI1: the apt step hung for more than 25 minutes in the Link fleet e2e. Add a timeout and retry on every apt and network step, and use a Qt install cache.

## Acceptance

All `PLAN.md` exit gates, plus: a hover-open → pointer-leave → still-open test, a removal undo test, and the DnD
tests.
