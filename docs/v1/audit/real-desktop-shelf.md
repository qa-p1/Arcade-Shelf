## Shelf 0.4.0

The owner's Shelf is docked on the right edge: a floating, pinned (always-on-top) 30×92 tab at (2520,475), a normal Wayland toplevel (not layer-shell). Its address changes when it is re-created (0x…126cc0 → …f3140 → …17b5a0 during this pass).

### RD-SHELF-1 — UX — Hover-opened docked panel collapses when the pointer leaves (SH-8 confirmed on Hyprland)
Steps: real virtual-pointer motion onto the tab (to 2530,520): panel grows to 640×490 at (1910,475) and takes keyboard focus. Then warp the pointer into the disposable terminal and do one real motion (`pointer move 5 0`). Within 1.5 s the panel is back to the 30×92 tab. Repeated: identical.
Expected (owner rule): a popup never closes only because the pointer left. A deliberate `arcade-shelf --show` (and the global shortcut `hl.dsp.global('dev.arcade.Shelf:arcade-shelf-show')`) did stay open at 640×490 across the same pointer-leave and a focus change, so only the hover-peek path collapses. If hover-peek is meant to be a peek, a click or keystroke inside should promote it to "explicitly opened"; otherwise it contradicts the rule and the Settings text. Evidence: `real/shelf/peek-open.png`. Source: `ui/Main.qml:198-201` (`peekDismissTimer`), `src/Controller.cpp:508-514`.

### RD-SHELF-2 — polish — Settings say hover does not take keyboard focus; on Hyprland it does
Steps: Settings → General "Open docked Shelf on hover — Hovering the tab shows the panel without taking keyboard focus. Click it to type." On the real desktop (`follow_mouse=1`) the hovered panel becomes the focused client (`hyprctl clients` focusHistoryID 0) and an Escape sent to it collapses it, i.e. keys are accepted without a click. Not harmful but the text is wrong for focus-follows-mouse setups. Evidence: `real/shelf/s-general.png`, `peek-open.png`.

### RD-SHELF-3 — wrong result — "Backdrop blur" is ON while saying the desktop doesn't offer one (SH-4 confirmed on Hyprland)
Steps: Settings → General on the owner's desktop. Actual: switch ON; text "Your desktop doesn't offer one here, so Shelf stays opaque." Hyprland has `decoration:blur:enabled = true` (`hyprctl getoption`), so the message is wrong for this compositor, and the switch should not show ON when unsupported. Evidence: `real/shelf/s-general.png`. Source: Shelf blur capability probe (Settings page General).

### RD-SHELF-4 — polish — `--status` reports mode "background" while the panel is open
Steps: `arcade-shelf --show` (panel 640×490 visible and focused), then `--status`. Actual: `"mode":"background"` both when only the tab is visible and with the panel open. The mode field cannot tell scripts whether the panel is showing. (isolated SH-7 saw "foreground" after restart, so the value is not consistent.)

Verified working (Shelf):
- Hover-peek (docked tab → panel) opens quickly; panel layout (Quick Shelf header, filter, Undo/Actions, footer hints "Open / Space Preview in Look / Del Remove / Esc Close") renders cleanly.
- Global shortcut: Settings → Shortcuts shows "Ready · Ctrl+Alt+S"; `hyprctl globalshortcuts` lists `dev.arcade.Shelf:arcade-shelf-show`; dispatching it opens the panel (Activated only; toggling-off not judged); `hyprctl binds` has the matching modmask 12 S bind. Portal registration works on the real compositor (the isolated "No global shortcut" caption does not occur).
- Always on top: the tab and panel are `floating` + `pinned` above T3 and the floating terminal.
- Fullscreen guard: with my disposable terminal set fullscreen by exact address (`fs=2`), the Shelf client disappeared (no mapped Shelf window); after unsetting fullscreen the tab returned at (2520,475).
- `--add <file>` adds a text item; Space on the selected item opens the file in Arcade Look (floating 1040×720) with its name; Esc on Look returns; Delete removes the item (`items` back to 0). Settings (Shortcuts, Storage, Backup and restore, Connected apps, About screenshots in `real/shelf/s-*.png`) open from the tray and close with Escape.
- Escape on the focused panel collapses it to the tab; keys sent by address to an unfocused Shelf are ignored (Qt needs focus), so that is not a bug.
- Idle: 0 CPU ticks in 10 s, 8 threads.

Not tested (Shelf): dragging a real file from Nautilus into/out of the panel (the virtual-pointer drag cannot start Wayland DnD — see the Find note), docking to the other three edges, tray "New shelf…", backdrop blur rendering itself.


Owner config: trigger is bare `Tab` held (Hyprland global shortcut `dev.arcade.Wheel:arcade-wheel-trigger`, `hyprctl binds`: modmask 0 key Tab), hold threshold 0 ms, one deck "Applications" with six real application actions (Zen Browser, kitty, Files, T3 Code Nightly, ANM Player, Calculator). The overlay is a layer-shell surface (`hyprctl layers`: level 3 `arcade-wheel`, 0,0 2560×1440) with `KeyboardInteractivityNone` (`src/platform/linux/LinuxBackend.cpp:978`), so keyboard focus stays on whatever window had it (T3 here) while the wheel is up; I therefore sent no keys while it was visible.

### RD-WHEEL-1 — wrong result — Qt writes a second cache tree `~/.cache/Arcade Wheel/Arcade Wheel/` (W1 root cause)
Evidence: `~/.cache/Arcade Wheel/Arcade Wheel/qtpipelinecache-x86_64-little_endian-lp64/qqpc_opengl` (98 KB, rewritten 2026-11-06 16:31) next to the proper `~/.cache/arcade/wheel/{logs,qmlcache,qtpipelinecache-…}` (last written Nov 3). Cause: `src/main.cpp:120-122` sets `QCoreApplication::setOrganizationName("Arcade Wheel")` and `setApplicationName("Arcade Wheel")`; Qt's scenegraph pipeline cache (and anything else using `QStandardPaths::CacheLocation`) therefore resolves to `~/.cache/<org>/<app>` = the doubled "Arcade Wheel/Arcade Wheel". `WheelPaths::cacheDir()` (`src/core/WheelPaths.cpp:16`) is correct but not used for Qt's own caches. Fix: set the organisation name empty/"arcade" and application name "wheel" (or redirect `QSG_RHI_PIPELINE_CACHE`/`XDG_CACHE_HOME` to `~/.cache/arcade/wheel`), and delete the old tree on migration. The older `~/.cache/arcade/wheel/qtpipelinecache…` copy shows the earlier build wrote there, so the 0.5.0 org/app names moved it.

### RD-WHEEL-2 — UX — Escape does nothing at the top of Settings (WH-1 confirmed on the real desktop)
Steps: tray → Arcade Wheel → Settings (opens tiled 1228×1398 on "Your wheel"); send Escape to its exact address. Actual: window stays. Ctrl+W closes it (`qml/settings/SettingsWindow.qml:59`, `StandardKey.Close`). Expected: Escape closes the window at the top view (the Find, Lens and Shelf settings windows tested in this pass all close on Escape).

### RD-WHEEL-3 — UX — `--press` then moving 150 px then `--release` executes the slice immediately; no harmless way to test "release in the middle"
Not a defect in itself (it is the product): `--press` shows the wheel centred and warps the real pointer to the screen centre (1280,720; `LinuxBackend.cpp:1174-1196`, from (400,1200)). A real pointer motion `pointer move 0 -150` selected slice 0 (`selectedIndex:0`) and `--release` launched Zen Browser within a second. My test thus started a Zen process by mistake (pid 225626, started fresh by the Wheel); I closed that process by PID. The pointer is not returned to where it was after the wheel closes (after right-click cancel and after release the cursor remained at the centre). For a user who holds Tab while working with the mouse elsewhere this means every use jumps the cursor to the middle of the output; check whether that is wanted (spec decision), otherwise restore the cursor on dismiss.

Verified working (Wheel):
- `--preview` and `--show` draw the full-screen layer with the owner's six-slice wheel (icons, "Applications" centre label); the preview/show stays up after a real pointer motion out of the area and over T3 (no pointer-leave dismissal); right-click at the centre cancels it (layer disappears from `hyprctl layers`, status `overlayVisible:false`). `--hide` removes the layer.
- Slice dispatch on the real desktop: pointer move up from centre selects slice 0, release launches the configured application (Zen Browser appeared as a new tiled window).
- Settings page set: General, Shortcuts ("Ready · hold Tab to open", hold threshold 0 ms), Your wheel (live preview, six actions listed clockwise), Actions, Center gestures, Appearance, Behaviour, Installed applications, Connected apps, About all reachable; Ctrl+W closes; resident keeps running afterwards (`--status` running, tray hosted). Screenshots `real/wheel/s-*.png`.
- `--status` JSON is valid and consistent (trigger text, platform wayland, tray hosted).

Not tested (Wheel): the genuine held-key path (global shortcut Activated+Deactivated cannot be generated by `hl.dsp.global`, and I would not press physical keys); wheel on multiple monitors (single monitor); editing/importing a separate profile (not done: the owner's live config was not modified).


Tools (the tray host for all apps) was restarted once with its own `--restart` (see T1); the successor runs in the same systemd unit (`app-arcade\x2dtools@autostart.service`) as the old one and as Box/Lens, so Box and Lens were not killed, and every app (Find, Shelf, Wheel) reported `tray:"hosted"` again after the restart. Tools' window class is `arcade-tools`; its overlay is a layer-shell surface (`arcade-tools-overlay`, exclusive keyboard).

### RD-TOOLS-1 — wrong result — Shortcuts → Conflicts shows 6 false conflicts: every Arcade shortcut "conflicts" with its own Hyprland binding
Steps: Tools → Shortcuts → Conflicts (badge "6"). Actual: "Two bindings on the same keys: only one of them works." lists Show or hide the Arcade Island ⟷ Arcade Box, Open the clipboard picker ⟷ "Arcade Clipboard ACBMESH_SIGNAL_24757_517403", Show or hide Find ⟷ "Lua function (added at runtime)", Start a screen capture ⟷ "Lua function (added at runtime)", Show or toggle Arcade Shelf ⟷ "arcadeShelf_2215da1615a84c6284a6f02f51b3322e", Open the wheel… ⟷ "Arcade Wheel trigger". Each pair is the app's own registered bind (same chord on both sides). Nothing actually clashes (`hyprctl binds` has exactly one bind per chord). Also leaks internal bind descriptions (signal id, hash) as labels.
Expected: an Arcade shortcut matched by the Hyprland bind that the same app created is not a conflict; the badge should be 0 unless two different owners share a chord. Evidence: `real/tools/shortcuts-conflicts.png`, `t-Shortcuts.png`. Source: `crates/arcade-shortcuts/src/index.rs:723-742` (Arcade key vs every `wm.groups` bind via `keys::conflicts`, no check that the WM bind belongs to / was registered by that app), labels from the compositor description.

### RD-TOOLS-2 — broken — Engines page: every installed engine "Check failed … version check timed out" (0 of 11 found)
Steps: Tools → Engines (also after "Check again"). Actual: Tesseract, ripgrep, FFmpeg, Poppler, qpdf, eSpeak NG, libvips, ImageMagick, Ghostscript… all red "Check failed — /usr/bin/<x> version check timed out", Tesseract even offers "Download with Arcade"; after a second "Check again" (spinner ≈ 20 s, i.e. 11 serial 2 s timeouts) 1 of 11 was found (the last row, v15.2.0), the rest still failed. From a terminal the same commands answer in 1–8 ms (`tesseract --version` 8 ms, `rg --version` 1 ms), and Lens/Box/Find use these binaries successfully. Expected: versions listed, nothing "failed".
Evidence: `real/tools/t-Engines.png`, `engines-recheck.png`, `engines-final.png`, `engines-first.png`. Suspected source: `src/engines.rs:212-240` `version_output` (`child.wait_timeout(VERSION_TIMEOUT=2 s)` inside the Tauri/GTK process; a SIGCHLD/`waitpid` race with GLib's child reaping would make `wait_timeout` never observe the exit), engine probing `src/engines.rs:110-130`. Not reproduced under the isolated Xvfb run (Tools 0.4.6 there never opened this page). Related: Box's Engines page misses tools (BR-8) but that is a different list.

### RD-TOOLS-3 — wrong result — App details shows "Latest: Not checked" and an enabled green "Update" while Home says "Updates checked 1 min ago"
Steps: Apps → Arcade Clipboard → Details. Actual: Installed v0.3.2, Latest "Not checked", primary "Update" button; Updates tab says "You're up to date · Last checked 5 min ago" and Home "Updates checked 1 min ago". Expected: the detail page shows the last checked latest version and no Update button when already current. Evidence: `real/tools/detail-after-esc.png` (top part), `t-Updates.png`. Update not clicked.

### RD-TOOLS-4 — UX — Escape does not go back from the app details page (TL-3 confirmed)
Steps: Apps → Details on a card → Escape (sent to the window): stays on "← Back to apps". Back works by click. At the top-level tabs Escape (correctly) leaves the main window open. Evidence: `real/tools/detail.png`, `detail-after-esc.png`. Source: `src-ui/components/AppDetail.svelte` (no Escape handler), `src-ui/App.svelte`.

### RD-TOOLS-5 — UX — Shortcut overlay Escape closes everything even with a filter typed (TL-6 confirmed on Wayland)
Steps: `Arcade-Tools.AppImage --show-shortcuts` (this Wayland session opens a 2560×1440 layer-shell surface with a 1066 px panel, `KeyboardInteractivity::Exclusive`), type `clip` (list narrows to Arcade Clipboard / 37 WM entries), press Escape once. Actual: overlay disappears (process exits) and the query is lost. Footer says "Esc Close". Expected (owner rule): first Escape clears the typed text. Source: `crates/arcade-tools-overlay/src/ui/mod.rs:60`. Evidence: `real/tools/overlay-wide.png`.

### TL-2 / TL-5 / TL-1 re-assessed on the real Wayland desktop
- TL-2 (focus loss closes the overlay): not reproduced. With the exclusive layer, real pointer motion over and out of the panel, over my disposable terminal, and an explicit `hl.dsp.focus` of another window left the overlay open. A real click on another window's area (outside the panel) did close it, which is the allowed "explicit outside click".
- TL-5 (typing crashes in libxkbcommon): not reproduced on Wayland; typed `clip` without any crash (no new coredump). It is an X11/bundled-xkb problem only.
- TL-1 (CLI `--show-shortcuts` SIGBUS): not reproduced here; the CLI exited 0 and the overlay (running from the CLI's own `.mount_…`) stayed up for >10 s, and `coredumpctl` shows no new core. Toggling with a second `--show-shortcuts` closed it. Risk remains if the CLI AppImage's mount goes away while the helper is alive.

### RD-TOOLS-6 — polish — Window class/app_id is `arcade-tools`, every other Arcade app is `dev.arcade.<App>`
`hyprctl clients`: Tools = `arcade-tools`; Box/Look/Find/Lens/Shelf/Wheel/Clipboard = `dev.arcade.<Name>`. STANDARD §10 says the OS identifier is `dev.arcade.*` (Wheel's main.cpp comment cites it). Window rules, icon matching and `hyprctl` scripts for the fleet break for Tools. Evidence: `hyprctl clients` output in this session.

### T4 / TL-4 confirmed: `--status` opens the GUI, prints nothing
`Arcade-Tools.AppImage --status` (real install): exit 0, empty stdout, the Tools window (71,21 1228×1398) opened/focused.

### T1 (idle CPU) — NOT reproduced; likely cause identified
- Original resident Tools (pid 1355, 5 h uptime): 60 s sample → 0.02 % CPU (1 tick), 67 threads (26 tokio workers, 8 tools-tray, 8 arcade-link-con, 6 arcade-tools, 4 notify-rs), 91 fds, 102 MB RSS. The 8.9 % seen earlier is gone.
- After `--restart` (pid 228175): first 60 s after start 1.22 % (startup scan/catalog work), next 60 s 0.00 %, threads 44 (12 tokio workers), 61 fds, 66 MB.
- With the window open on Home, idle: 6.1 % CPU for 30 s (183 ticks; three tokio workers busy ~60 % each), threads 44→78, fds 95, RSS 214 MB. Source: `src-ui/tabs/Home.svelte:68-73` samples `api.dashboard()` every 2 s "only while Home is visible"; each sample opens a Link connection to all seven apps (`src/dashboard.rs:70`, 200 ms connect timeout each) and refreshes sysinfo. After closing the window: 0.00 % for 60 s again, 65 threads, RSS 187 MB (not released).
- Hypothesis for the owner's 9 %: a Tools window left open/hidden (minimised or on another workspace) on the Home tab keeps that 2 s poll running because `active` is the tab state, not window visibility. I did not minimise/move the window to confirm. Fix: sample only while the window is visible/focused, reuse the Link connection, and back off when nothing changed.
- No growth ratchet observed: thread count rises with window open (44→78→65) but does not climb across the idle/closed periods measured.

Verified working (Tools):
- Tray (hosted by Tools via one StatusNotifierItem): per-app submenus for Box (Open, Settings, Open pipelines, Restart, Quit), Clipboard (Open, Settings, Private mode, Restart, Quit), Find (Open, Settings, Rescan index, Restart, Quit), Lens (Open, Settings, Capture, Capture text, Pick color, Restart, Quit), Look (Open, Settings, Restart, Quit), Shelf (Open, Settings, New shelf…, Show shelf, Restart, Quit), Wheel (Open, Settings, Open wheel, Restart, Quit), plus Start all / Quit all / Quit Arcade Tools. Clicked and verified: Find Open / Settings / Rescan, Look Settings, Clipboard Settings, Box Settings (opens the dashboard on Settings), Lens Settings / Capture text / Pick color, Shelf Settings, Wheel Settings, Tools Open. Not clicked: every Restart/Quit, Start all, Quit all, Private mode toggle.
- Tools window pages Home (7 installed, 7 running, 847 MB), Apps (Start-at-login toggles per app), Updates, Shortcuts (Arcade/Hyprland/Apps tabs), Learn, Connections, Engines, Settings all render; the shortcuts overlay (Super+/ bind exists: `hyprctl binds` modmask 64 key slash) opens as a layer surface and its filter works.
- Escape closes the settings windows of Look and (earlier) Find/Lens/Shelf; it does not close Wheel's, Clipboard's or the Box dashboard (top view). Clipboard `Esc` handling for its picker is covered in RD-CLIPBOARD-1.


### C1 not reproduced — a normal `--quit` does not core dump
Steps: record `coredumpctl`, run `Arcade-Clipboard.AppImage --quit` on the real install (resident pid 24757, scope `arcade-clipboard-175236.scope`), wait 4 s. Actual: process exits, `coredumpctl list --since -1min` empty; diagnostics log: `[lifecycle] quitting`. The earlier SIGBUS was the AppImage mount going away under a live process (scope teardown), not the quit path. Relaunched per rule 5.

### RD-CLIPBOARD-3 — broken — `--restart` leaves a live but unreachable successor running from an unmounted AppImage (CB-4 confirmed on the real install)
Steps: `Arcade-Clipboard.AppImage --restart` on the freshly relaunched resident; wait 10–20 s. Actual: a process `/tmp/.mount_ArcadeolgIJc/usr/lib/arcade-clipboard/clipboard --background` (pid 239853) is alive, owns the D-Bus name `dev.arcade.Clipboard`, but `/tmp/.mount_ArcadeolgIJc` no longer exists (`mount | grep` empty, `ls` ENOENT), and `Arcade-Clipboard.AppImage --status` reports `{"running":false,"runningVersion":null,"link":…}` with no Link endpoint; no coredump. The app is effectively gone (no tray menu, no picker). Expected: the successor must start from the AppImage file itself, or the old process must stay until the new one is up.
Source: `apps/flutter_app/lib/platform/desktop_adapter.dart:~195-205` `restart()` uses `Platform.resolvedExecutable` (inside the old mount) with `ARCADE_CLIPBOARD_RESTART_AFTER=<pid>`, then `_quit()`; when the old AppImage runtime exits, FUSE unmounts and the successor loses its files. Fix: relaunch via `$APPIMAGE` (env) when set, as other apps do.
Recovery used: killed pid 239853 and relaunched with the rule-5 `systemd-run --user --scope` command; `--status` then `running:true`.

### Private mode is ON on the owner's Clipboard (observed, not changed by this pass)
The tray item ("Private mode", toggle-state 1), the Settings → Privacy switch and the sidebar footer ("Private mode", amber dot) all show it ON. `~/.local/share/arcade/clipboard/logs/diagnostics.log` has `paused=false` capture events up to 19:13:31 and none afterwards: nothing I copied during this pass (Lens text/image, Find path/file, colour) was captured. Either the owner set it or an earlier audit pass left it on (Box's "Clipboard history" connected-app experiments in `box-real.md` toggled the peer); I did not touch it. The owner should confirm and turn it off if unintended.


### BR-9 re-test (Window Pin): still broken
Steps: disposable floating terminal focused by pointer; `arcade-desktop --show`; type "window pin", Enter, Ctrl+Enter ("Ready to pin the active window"), click "Keep active window on top". Actual: red card "Couldn't finish — No other window is active" together with the green "Done · 1 result"; the terminal's `pinned` stays `false` (`hyprctl clients`), the Island stays open and pinned itself. Evidence: `real/box/wp-search.png`, `wp-tool.png`, `wp-run.png`, `wp-after.png`. Same cause as BR-9 (`hyprland.rs` `foreign_active_window` right after the Island hides). Recent page was already confirmed in round 3 (`real/box/page-recent.png`).

## Fleet-wide Escape / pointer-leave summary (real desktop, this pass)

| App / surface | Pointer leave closes it? | Focus change closes it? | Escape |
|---|---|---|---|
| Find overlay (layer-shell, exclusive kb) | no | not by pointer/click (keeps exclusive focus) | text typed → closes and discards (RD-FIND-1) |
| Lens capture overlay (fullscreen toplevel) | n/a (covers output) | yes (RD-LENS-5) | Esc steps back once; with filter text goes back without clearing (RD-LENS-2) |
| Lens pin menu | no | – | closes whole pin (RD-LENS-3) |
| Shelf docked panel, hover-opened | yes (RD-SHELF-1) | – | closes when focused |
| Shelf panel, explicit `--show` / global shortcut | no | no | closes when focused |
| Wheel preview/show (layer-shell, no kb) | no | n/a | n/a (no keyboard); right-click cancels |
| Tools shortcuts overlay (layer-shell, exclusive kb) | no | no (explicit outside click closes) | text typed → closes (RD-TOOLS-5) |
| Look preview (earlier) | no | no | one step back (RD-LOOK-1 trap) |
| Clipboard picker (earlier) | yes (RD-CLIPBOARD-1) | yes | – |
| Settings windows | – | – | close on Esc: Find, Lens, Shelf, Look; do NOT: Wheel (RD-WHEEL-2), Clipboard, Box dashboard (top view), Tools main window |

## Final state

- Clipboard (Wayland) restored to empty (`wl-copy --clear`; originally "Nothing is copied").
- Disposable windows closed/killed: audit terminals (kitty), OCR kitty, GTK DnD target/source, Zen Browser instance accidentally started by Wheel (killed by PID). Screenshots under `real/<app>/`; helper scripts and logs in the session scratchpad `rd4/`.
- Settings restored: Find `theme` back to `system` (file identical to `settings-find-before.json`). Nothing else changed: Box Start at login untouched (off), Lens history off, no installs/uninstalls/commits, no Hyprland config/bind change.
- Running state: Tools restarted (new pid 228175, tray host, window closed), Clipboard restarted twice (current pids 240250/240257, relaunched in a `systemd-run --user --scope` unit, `running:true`), Find, Look, Lens, Box (background), Shelf (docked tab at (2520,475)), Wheel all still running, as at the start. Lens still has one defunct Settings child (223656) which only goes away when Settings is reopened (LN-3).
- One heads-up for the owner: Clipboard "Private mode" is ON (see above) and was not set by this pass.
