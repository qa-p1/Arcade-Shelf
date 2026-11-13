# Arcade Shelf 0.4.0 audit

Environment: isolated HOME/XDG/runtime + private dbus, Xvfb :93 without a window manager (so no focus-follows-mouse, no real always-on-top; resumed with a private GTK drag-and-drop source/target). Inputs: `--add` CLI, X11 clipboard paste (Ctrl+V) with a GTK clipboard owner, xdotool clicks/keys/wheel. Evidence: AUDIT/shelf/n-*.png (plus older files from the first attempt).

## Coverage
| Feature | Tested? | Method / input | Result |
|---|---|---|---|
| Start, empty state, footer key hints | yes | GUI | pass |
| Add file/folder/image/unicode name/empty file/duplicate | yes | `--add` | pass; see SH-1, SH-2 |
| Add symlink, broken symlink, nonexistent path | yes | `--add` | SH-1, SH-2 |
| 60 rapid `--add` calls | yes | CLI loop | 8 s total, 70 items, idle 0.0 % CPU |
| Paste text, URL, files, image | yes | xdotool ctrl+v with X clipboard | pass; link tile, text tile, image tile |
| Select, Delete (remove), Undo | yes | GUI | remove works; undo only for additions (SH-5) |
| Filter field, scroll with many items | yes | GUI | scroll works |
| Panel menu (collect, select all, dock, always on top, settings) | yes | GUI | opens; works |
| Collections: new (empty name blocked), switch, menu | yes | GUI | works; SH-6 |
| Delete collection, rename, pin, move | no | needs more time | not exercised |
| Capsule (216x76), `--hide`, `--show`, `--toggle` | yes | CLI + shot | pass |
| Dock to right edge, hover open, hover close | yes | menu + xdotool | works (30x92 tab, opens 620x480) |
| Dock left/top/bottom, 125 % font scale | no | | unconfirmed |
| Settings window (General, Shortcuts, Storage, Backup, Connected apps, About) | yes | `--settings` + clicks | renders; SH-3, SH-4, SH-7 |
| Backup export/import | yes | native save/open + include owned content + Replace confirmation | 1,221 B backup, restore reports 3 items, referenced file remains (r-backup-*.png, r-restore-*.png) |
| Real drag in/out: file, text, URL | yes | GTK XDND source and drop target on :93 | pass, exact URI/text payloads; r-after-drops.png, r-drag-out.jsonl |
| `--restart`, `--quit`, persistence across restart | yes | CLI | items persisted; see SH-7 |
| Idle CPU / memory | yes | /proc 30 s | 0.00 % CPU, 7 threads, 89 MB |
| Tray, global shortcut, backdrop blur, fullscreen guard | no | needs real compositor | unconfirmed |

## Findings

### SH-1 (UX) Misleading error for a nonexistent path
Method: CLI. `Arcade-Shelf.AppImage --add /nonexistent/x` prints "Arcade Shelf can't open this kind of content." exit 1. Expected "Couldn't find /nonexistent/x" (the kind of content is not the problem).

### SH-2 (wrong result) Symlinks are collected as 0 B "any" items; broken symlinks are accepted
Method: CLI `--add content/good-link` (link to a 6-byte text file) and `--add content/broken-link`. Tiles show "good-link  any - 0 B" and "broken-link  any - 0 B" with a generic icon (n-items.png). Expected: good link resolved to the target (text, 6 B, preview), broken link rejected with a "Couldn't add" message.

### SH-3 (polish) Footer feedback text overlaps the key hints
Method: GUI. Pressing Ctrl+V with nothing collectable shows "Couldn't collect this. Shelf keeps files, folders, images, text and links." drawn over the "Open / Space Preview / Del Remove / Esc Close" hints (n-footer-overlap.png). The message needs its own line, elision, or a toast.
Suspect: ui/Main.qml footer row (feedback Label anchored right without width limit).

### SH-4 (polish) Settings wording and values
- General: "Backdrop blur" switch shows ON while its text says "Your desktop doesn't offer one here, so Shelf stays opaque" (n-settings-general.png). It should be disabled/off when unsupported.
- Shortcuts: global shortcut recorder shows "Ctrl Alt S" with the caption "No global shortcut" under it (contradictory).
- Storage: default quota shows "536 MB". Default is 512 MiB (536870912) divided by 1,000,000 (ui/SettingsWindow.qml:192) and labelled MB. Show 512 MiB or use a round decimal default. The spec says MiB.
- About: "Open logs folder" points to data/arcade/shelf/logs which does not exist, and Shelf writes no log file at all in normal use (no stderr output either) so field problems cannot be diagnosed.

### SH-5 (UX) Removing items is immediate with no undo; "Undo" belongs only to the last addition
Method: GUI. Delete removes the selected item with no confirmation and no undo offer; the header "Undo" stays visible on unrelated collections (e.g. an empty "Work" collection shows Undo for the Quick Shelf addition, col4 screenshot). Source: src/link/Service.cpp:373,394-399 (m_undo only holds addedIds). Rename the button "Undo add" or extend undo to removals.

### SH-6 (polish) Collection menu offers actions that do nothing
Method: GUI. With a single collection "Move up", "Move down" are enabled; "Pin collection" on the only collection. (n-collection-menu.png shows them enabled with two collections too; with one they cannot act.)

### SH-7 (polish) Restart re-opens the panel
Method: CLI. Capsule mode (after `--hide`), then `--restart`: successor started as `--show --wait-for-exit <pid>` and status reports mode "foreground". A restart should keep the previous mode.

### SH-8 (UX) Hover-opened shelf collapses when the pointer leaves
Method: private GUI / Xvfb. Panel menu → Dock → Left edge. Move pointer onto the 30×92 tab, wait 650 ms: panel expands to 620×480. Move pointer to (1000,1000), wait 650 ms: panel collapses to 30×92 without any explicit dismissal. Expected owner rule: popups never close just because the pointer leaves. Explicit `--show` does survive an explicit focus change to the private receiver, so this is specifically the hover-peek path.
Evidence: `shelf/r-docked-left.png`, `r-hover-open-left.png`, `r-hover-close-left.png`, `r-explicit-blur.png`. Cause: `ui/Main.qml:198-201` starts a 320 ms `peekDismissTimer` that collapses whenever `!surfaceHover.hovered && !dragHover && !focusPanel`; hover calls `Controller::peek` which sets focusPanel=false (`src/Controller.cpp:508-514`).

## Backend
- Idle 30 s: 0.00 % CPU, 7 threads, 89 MB RSS, 21 fds.
- Files written only under data/arcade/shelf (sqlite, instance lock, objects), data/arcade/apps/arcade.shelf.json, cache/arcade/handoff, run/arcade sockets (stale arcade.shelf.sock/endpoint left in the private runtime dir after `--quit`? the sockets from the first attempt remained; the live instance rewrote them).
- No coredumps on `--quit` or `--restart`.
- `--bogus` prints "dev.arcade.Shelf: Unknown option 'bogus'." exit 1 (fine).

## Unconfirmed / needs real desktop
- Docking on the other three edges, always-on-top, backdrop blur, fullscreen guard, tray, global shortcut Ctrl+Alt+S, Wayland/Hyprland focus behaviour, panel closing on focus loss (not testable without a WM: in Xvfb the docked panel stayed open when the pointer left while a dialog was open).
- Rename now works (Quick Shelf → Quick renamed, r-renamed-result.png); delete remains unconfirmed; backup export/import now exercised.

## Resume checkpoint
The fresh resumed profile collected a Unicode/spaced file, multiline Unicode text and URL through actual GTK → Qt XDND. Each then dragged back out to a GTK drop target with exact UTF8_STRING/percent-encoded text/uri-list payload preserved. Original file still exists. No real application was opened by these actions. Source for drag handlers: ui/Main.qml and src/Controller.cpp. Evidence `shelf/r-drag-out.jsonl`.
