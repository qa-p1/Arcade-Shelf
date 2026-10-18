# Status — 2026-10-12

## Release status (2026-10-18)

- Latest release: [v0.3.0](https://github.com/qa-p1/Arcade-Shelf/releases/tag/v0.3.0), published 2026-10-18 (fixes from the October audit and stress test; see CHANGELOG.md). The CI and e2e notes below were recorded for v0.2.0.
- Previously released: [v0.2.0](https://github.com/qa-p1/Arcade-Shelf/releases/tag/v0.2.0) with a Linux self-installing AppImage, a per-user Windows installer, a macOS dmg, `arcade-release.json` and `SHA256SUMS.txt`.
- Merged to `main` from PR #3 (f3dafd4) after CI passed on Linux, Windows and macOS: [run 38601744132](https://github.com/qa-p1/Arcade-Shelf/actions/runs/38601744132). That covers the build, tests, clippy or analyzers, installer smoke tests on each OS, and the Linux AppImage install, repair and uninstall check in a temporary profile.
- Ecosystem e2e (Arcade Link `tools/e2e.py`, real CI builds of all eight apps): install, tray hosting (hosted by Tools, own icon back within 1 s after Tools is killed, no flash on restart) and shortcuts pass. Tools 0.2.0 backs up and restores Shelf's settings (the backup incompatibility found by the e2e matrix was fixed in Tools, cf8f0c5).
- Not yet verified: a real Windows or macOS desktop (CI runners only), and the owner's real-desktop checks. Builds are unsigned, so SmartScreen and Gatekeeper ask for approval.

## Arcade Tools hub integration

Shelf 0.2.0 vendors Link v0.3.0 (`33650a87`) with its Qt conformance tests. It
implements the seven optional hub methods, receipt-based installation,
portable backups, and tray hosting with disconnect hand-back.

- 2026-10-12: Linux build passed with Qt 6.11.2 and GCC 16.2.1.
- CTest: 15/15 groups passed offscreen and 15/15 under Xvfb/xcb through Link
  v0.3.0's isolated e2e runner. This includes 13 real executable process tests,
  headless CLI validation, portable round trips and the new Link conformance
  suites for install, tray hosting and optional methods.
- Both source provenance sets match released commit `33650a87`: 47 Qt bundle
  files and 12 shared assets. Shortcut data/generated Markdown, docs links,
  shell syntax and release generator tests pass; the optional Python
  `jsonschema` test is skipped when that package is absent.
- CI run 38435099435 at `e4917f2` passed on Linux, Windows and macOS. It
  builds and packages all three, runs the Windows silent installer lifecycle
  (install, repair, update, uninstall in a temporary profile) and the macOS
  bundle lifecycle. This is CI only, not a real desktop.
- The Windows uninstaller deletes only the files the installer staged, then
  removes the folder if it is empty. It keeps everything when a newer install
  owns the receipt. `scripts/test-windows-installer.ps1` checks a custom folder
  with unrelated files and a successor installed into the old folder.
- `tests/appimage_lifecycle.py` checks the self-installing AppImage path:
  receipt, desktop entry, icons, CLI link, repair, update with `previous`,
  re-exec and uninstall, in an isolated home. It passed 2/2 locally against
  the build; CI also runs it against the packaged AppImage.
- None of these additions have been exercised on a real desktop yet.

## Earlier verification

Version 0.1.1. Everything listed under "Verified for 0.1.0" was verified
**headless only** (offscreen, Xvfb and headless Weston/Sway with software
rendering). 0.1.0 had never run on a real desktop, and on Hyprland it was
unusable. 0.1.1 is the first release checked on one; the table below says which.

## Checked on a real desktop for 0.1.1

Hyprland 0.56.2 (Lua configuration) on Arch Linux, 2560×1440, with the user's
own shell, gaps and window rules, using the AppImage and a local build.

| Check | Result |
|---|---|
| AppImage started from the desktop, no environment variables | Starts with hardware GL; no crash, no Fontconfig or EGL warnings |
| First map | Floating 216×76 capsule near the right edge; never tiled |
| Expand / collapse, repeated | 640×490 panel opens inward; returns to the same capsule spot; never maximized |
| Drag capsule to the right edge (real pointer input) | Docks into the 30×92 edge handle |
| Hover handle / click / Escape | Peeks the panel; click focuses it; Escape returns to the handle |
| Drop a file and text on Shelf | Added; grid shows them with thumbnails (images up to the 64 MB decode cap) |
| `--show`, `--toggle`, `--add`, `--status`, `--quit` against the running app | Work |

Not yet checked on a real desktop: drag-out into another app, Arcade Find
drag-onto-Shelf, fullscreen hiding, the global shortcut, multi-monitor/DPI,
GNOME/KDE/Sway, Windows and macOS.

## Implemented

- C++20 / Qt 6.8 native floating capsule and expanded panel, dark/light/system
  themes, selection, filtering, context menus, keyboard actions and preferences.
- Real native MIME drops and copy-only drag-out; a same-profile marker supports
  in-panel reordering. Native move/resize, edge tabs, geometry adapters and
  topmost preferences are implemented.
- SQLite collections, ordering, pinning, transactional reference additions,
  deduplication, bounded owned content, lazy thumbnails, retention, missing-file
  states, startup recovery and Undo for the last addition.
- Per-profile single-instance lifecycle and tray commands. Native/portal shortcuts,
  collision checks and guarded opt-in per-user login startup are implemented.
- Frozen Link v1 `shelf.add`, `shelf.show`, `shelf.pick`; disabled public listener,
  private lifecycle forwarding, explicit picker cancellation and selected exports.
- Cached capability-based peer calls with size validation, progress, bounded
  deadlines, cancellation, preserved handoff ownership and output collection.
- Linux runtime-bundle generation, Windows NSIS/macOS DMG scripts, development CI,
  licensing/provenance checks and isolated shared onboarding patches.

## Verified for 0.1.0 (headless only)

| Check | Actual result | Limit |
|---|---|---|
| GitHub three-platform CI | Green on Linux, Windows and macOS (build, all CTest groups offscreen, Linux xcb CTest, packaging, package smoke, release manifests); [run 15](https://github.com/qa-p1/Arcade-Shelf/actions/runs/38029649624) at `9b623b2` | CI runners, software rendering |
| C++/QML application build | Local Linux with Qt 6.8.2/GCC 14.2 and with Qt 6.8.3 (conda-forge); CI with Qt 6.8.3 | Compilation does not establish native interaction |
| Full CTest run | 10/10 groups pass offscreen and under Xvfb/xcb with Qt 6.8.3 | Software rendering in an isolated X11 session |
| Native two-process X11 drag | Passed with real OS pointer injection and actual Desktop adapter; file and folder MIME bytes received | Not Nautilus or Wayland |
| Actual QML render captures | Passed under Xvfb, dark/light/capsule screenshots saved | Controlled fixture, software rendering |
| Actual QML/Controller GUI suite | 9 QtTest rows passed, 0 failed, 0 skipped on both offscreen and xcb | In-process Qt input injection; OS cross-process drag is the separate test above |
| Real executable process suite | 11/11 passed, including 1,000-reference transaction, restarts, partial batches, authentication, cancellation and owned handoffs | Isolated offscreen profile |
| Headless Wayland executable process suite | 11/11 passed under Weston | Does not test pointer drag transfers to Nautilus |
| Frozen Rust Link CLI → real Shelf | 14/14 isolated checks passed | Offscreen |
| Real Arcade Find ↔ real Shelf | Find's e2e 49/49 (headless Sway and Xvfb): Find's "Add to Shelf" stores references, launches a stopped Shelf, and Shelf's `find.show` opens Find on the file; Link's runner `find` group 3/3; `tst_PeerHub` contract test with Find's real manifest; `tst_Ui` clicks Actions → "Search in Find" and "Find matching files" against Find's manifest | Isolated profiles; no drag and drop between them yet; see [ECOSYSTEM_VERIFICATION.md](ECOSYSTEM_VERIFICATION.md#real-find-against-real-shelf) |
| Reference Rust Link tests | 42 passed, 1 intentionally ignored engine-download test | No downloaded engine |
| Shared onboarding tests (Shelf and Find) | Link Rust 36 + Qt 2/2; runner shelf 3/3 and find 3/3; Tools 15/15 focused, lifecycle 4/4 and 3/3; Wheel CTest 9/9 | Merged; details in [ONBOARDING.md](ONBOARDING.md) |
| Linux runtime package | Rebuilt from the recorded milestone; 11 packaged process checks and real stopped-app Link launch passed with development Qt paths removed | Cloud host runtime bundle; native compositor and distribution compatibility remain separate |

The GUI run exposed and fixed search focus being reset while typing. The
collapsed grid now keeps an empty model and defers thumbnails until expansion;
the item list is cached to avoid repeated conversion during QML indexed reads.
Untrusted markup in item text, collection names and capsule tooltips remains
literal and makes no image requests, verified against an isolated loopback server.
Native drag testing also caught premature destruction of Qt's drag source;
the corrected adapter preserves delayed native MIME retrieval. Building with
Qt 6.8.3 exposed that startup cleanup deleted every live Shelf-owned handoff
export (a descriptor-opened marker reported no modification time) and that
collapsing to a docked tab could stay clamped to the panel's height; both are
fixed, with regression coverage. No fabricated
desktop results or screenshots are used. Commands are in [TESTING.md](TESTING.md).

## Measured performance

[Raw three-run evidence](../benchmarks/cloud-milestone.json) uses the recorded `15a0af1`
Linux RelWithDebInfo executable under offscreen/software rendering. Each run
persists 101 references through 100 add/duplicate cycles, waits one second for
feedback, and measures five seconds without IPC. All samples have the same
binary SHA-256, `9dc2bce580dda7e9cdc0487e0c4fa1b4a1bc66da6975d1e918e0a25c6229c695`.

- Public authenticated endpoint readiness: 41.7–52.1 ms; median 42.3 ms.
- Per-run warm-add medians: 1.01–1.04 ms; p95 at most 1.87 ms, including commit.
- Five-second idle target CPU: 0–0.01 seconds, or 0–0.2% of one core.
- Resident RSS after repeated use and idle: 80,556–80,868 KiB (78.7–79.0 MiB).

Endpoint readiness is not time to a visible GUI frame. Activation measurements
also cover acknowledgement only. These are warm-filesystem cloud measurements,
not real-desktop latency or Windows/macOS benchmarks. The earlier snapshot's
high post-add CPU exposed repeated QML list conversion and led to the cache fix;
the original evidence remains in Git history. Larger visible collections and
native compositor/monitor performance still need profiling.

## Implemented but not interactively verified

- Native drag out to Nautilus and other apps, fullscreen behavior, portal
  shortcut/reload and multi-monitor/DPI transitions (Hyprland placement and
  docking were checked for 0.1.1, above).
- Windows and macOS native drag, shortcut, geometry, tray and login adapters.
- Actual Look/Lens/Clipboard/Wheel desktop workflows and Box engine conversions.
- Final native installers, Tools update/removal and signing/notarization.

## Blocked by existing ecosystem or infrastructure

The cloud environment has no real Hyprland/Nautilus session, Windows/macOS host,
or physical monitor/DPI setup. Headless Wayland startup and X11 pointer tests
cover narrower behaviors. Reproducible desktop steps are in [TESTING.md](TESTING.md).

The shared repositories know Shelf and Find since Arcade Link `v0.2.0`
(2026-10-10): Link, Tools and Wheel are merged and the other apps moved to the
new tag; see [ONBOARDING.md](ONBOARDING.md).

## Remaining implementation/refinement work

- Automatic edge activation/snapping, platform blur and fullscreen-specific
  refinements need further native work; manual edge docking is implemented.
- Native peer flows and platform packaging must be exercised, then address
  any platform-specific defects they expose.
- Broader collection/UI performance under very large visible datasets needs
  benchmarking beyond the core's 10,000-reference test.

## Documents

[README](../README.md) · [Architecture](ARCHITECTURE.md) ·
[Link contracts](ARCADE_LINK.md) · [Testing](TESTING.md) ·
[Packaging](PACKAGING.md) · [Ecosystem evidence](ECOSYSTEM_VERIFICATION.md) ·
[Ecosystem onboarding](ONBOARDING.md) · [Changelog](../CHANGELOG.md) ·
[Dependency notices](../THIRD_PARTY_NOTICES.md)
