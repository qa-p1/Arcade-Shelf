# Status — 2026-10-10

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

## Verified development milestone

| Check | Actual result | Limit |
|---|---|---|
| C++/QML application build | Local Linux succeeded with Qt 6.8.2/GCC 14.2; GitHub Linux, Windows and macOS compiled `15a0af1` with Qt 6.8.3 | Compilation does not establish native interaction or successful packaging |
| Full Xvfb/xcb CTest run | 8/8 groups passed, 0 failures, 7.54 seconds | Software rendering in an isolated X11 session |
| Native two-process X11 drag | Passed with real OS pointer injection and actual Desktop adapter; file and folder MIME bytes received | Not Nautilus or Wayland |
| Actual QML render captures | Passed under Xvfb, dark/light/capsule screenshots saved | Controlled fixture, software rendering |
| Actual QML/Controller GUI suite | 9 QtTest rows passed, 0 failed, 0 skipped on both offscreen and xcb | In-process Qt input injection; OS cross-process drag is the separate test above |
| Real executable process suite | 11/11 passed, including 1,000-reference transaction, restarts, partial batches, authentication, cancellation and owned handoffs | Isolated offscreen profile |
| Headless Wayland executable process suite | 11/11 passed under Weston | Does not test pointer drag transfers to Nautilus |
| Frozen Rust Link CLI → real Shelf | 14/14 isolated checks passed | Offscreen; Find payload contract, not a real Find service |
| Reference Rust Link tests | 42 passed, 1 intentionally ignored engine-download test | No downloaded engine |
| Actual Find workspace | 64 tests passed | Its main remains a placeholder; Link/service not implemented |
| Shared onboarding tests | Local Link Rust 36 + Qt 2, Tools 12, Wheel 2 targets and Shelf e2e 3 checks passed; public Link six-job and Tools three-OS matrices passed | Draft PRs, not shipped shared releases; Wheel passed three package jobs and failed Intel macOS packaging |
| Linux runtime package | Rebuilt from the recorded milestone; 11 packaged process checks and real stopped-app Link launch passed with development Qt paths removed | Cloud host runtime bundle; native compositor and distribution compatibility remain separate |
| GitHub three-platform CI | Initial download failure corrected; subsequent `15a0af1` builds succeeded but later checks failed on each OS | Windows vendor checkout bytes, macOS tests and Linux package checks are being corrected; no green Shelf matrix claim |

The GUI run exposed and fixed search focus being reset while typing. The
collapsed grid now keeps an empty model and defers thumbnails until expansion;
the item list is cached to avoid repeated conversion during QML indexed reads.
Untrusted markup in item text, collection names and capsule tooltips remains
literal and makes no image requests, verified against an isolated loopback server.
Native drag testing also caught premature destruction of Qt's drag source;
the corrected adapter preserves delayed native MIME retrieval. No fabricated
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
backward-compatible patches now have public draft PRs in
[Link](https://github.com/qa-p1/Arcade-Link/pull/1),
[Tools](https://github.com/qa-p1/Arcade-tools/pull/1) and
[Wheel](https://github.com/qa-p1/Arcade-wheel/pull/5). They must be reviewed and
shipped before ecosystem onboarding is complete.
No shared tags, stable releases, or main merges were performed.

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
[Integration patches](INTEGRATION_PATCHES.md) · [Changelog](../CHANGELOG.md) ·
[Dependency notices](../THIRD_PARTY_NOTICES.md)
