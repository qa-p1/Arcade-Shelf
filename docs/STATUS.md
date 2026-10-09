# Status — 2026-10-09

This is the first working 0.1.0 development milestone. Source, tests, package
scripts and documentation are being delivered on `feature/shelf-v0.1`. It is not
a stable release or a claim that every original product requirement has been
interactively verified.

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

## Evidence available before the final milestone run

| Check | Actual result | Limit |
|---|---|---|
| Linux C++/QML application build | Succeeded, Qt 6.8.2, GCC 14.2, CMake 3.31.6 | Latest GUI refinements require final rerun |
| Core/service/peer/Link/Desktop CTest groups | 7 groups passed | NativeDrag skipped under offscreen |
| Native two-process X11 drag | Passed with real injected pointer events and actual Desktop adapter | Not Nautilus or Wayland |
| Actual QML render captures | Passed under Xvfb, dark/light/capsule screenshots saved | Controlled fixture, software rendering |
| Latest full GUI suite | 6 QtTest rows passed; 2 failures under investigation | MIME scene readiness and keyboard filtering |
| Frozen Rust Link CLI → real Shelf | 14/14 isolated checks passed | Offscreen; Find payload contract, not a real Find service |
| Reference Rust Link tests | 42 passed, 1 intentionally ignored engine-download test | No downloaded engine |
| Actual Find workspace | 64 tests passed | Its main remains a placeholder; Link/service not implemented |
| Shared onboarding tests | Link Rust 36 + Qt 2; Tools 12; Wheel 2 targets; Shelf e2e 3 checks passed | Local patch candidates, not shipped shared releases |
| Linux runtime package | Created; intermediate CLI/manifest smoke passed | Must rebuild from final milestone commit |

Final commands, measurements and commit IDs will replace this interim evidence
when the milestone is committed. No fabricated desktop results or screenshots
are used.

## Implemented but not interactively verified

- Hyprland/Nautilus native drag in/out, docking/pinning, nonfocus capsule feedback,
  fullscreen behavior, portal shortcut/reload and multi-monitor/DPI transitions.
- Windows and macOS native drag, shortcut, geometry, tray and login adapters.
- Actual Look/Lens/Clipboard/Wheel desktop workflows and Box engine conversions.
- Final native installers, Tools update/removal and signing/notarization.

## Blocked by existing ecosystem or infrastructure

Find commit `5700d0d` contains no runnable Link service. Real Find-to-Shelf tests
must wait for Find's designed integration to become code. Shelf accepts the exact
handoff payloads now; it does not invent a Find service or search index.

The cloud environment has no real Hyprland/Nautilus session, Windows/macOS host,
or physical monitor/DPI setup. Headless Wayland startup and X11 pointer tests
cover narrower behaviors. Reproducible desktop steps are in [TESTING.md](TESTING.md).

Released Wheel/Tools app allowlists do not recognize Shelf yet. The included
backward-compatible Link/Tools/Wheel patches remain local candidates and must
be reviewed, synchronized and shipped before ecosystem onboarding is complete.
No shared tags, stable releases, or main merges were performed.

## Remaining implementation/refinement work

- Resolve the outstanding GUI checks before marking the milestone verified.
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
[Integration patches](INTEGRATION_PATCHES.md) · [Changelog](../CHANGELOG.md) ·
[Dependency notices](../THIRD_PARTY_NOTICES.md)
