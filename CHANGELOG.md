# Changelog

## 0.2.0 — 2026-10-14

- Vendored Arcade Link v0.3.0 Qt transport, installer, tray watcher, shortcut
  helpers and shared Windows/release tools at commit `33650a87`, with hashes.
- Tools can open settings, restart Shelf, show or create shelves, rebind the
  global shortcut and export/import portable settings. Manifests advertise
  tray extras, docs version, logs and installation method.
- Shelf hides its own tray while Tools hosts it, restores it on disconnect
  or when connections are disabled, and waits for the startup ownership answer.
- AppImage self-install/update/repair/uninstall, receipt-based headless CLI,
  macOS move-to-Applications and CLI/login integration, and Windows per-user
  installer shims and receipts. Development builds never self-install.
- Portable collection/preferences backups with optional owned content and
  size disclosure; validated restore with a private database backup. Tools
  exports omit owned content and referenced originals remain untouched.
- User guides, shortcut sheets, local docs checks, Link conformance tests,
  real executable CLI/process tests and isolated Windows installer CI.

## 0.1.1 — 2026-10-11

First release checked on a real desktop (Hyprland 0.56.2, Arch Linux, NVIDIA/Mesa
drivers from the host). 0.1.0 had only been tested headless.

- Linux ships as a single-file AppImage, `Arcade-Shelf-<version>-x86_64.AppImage`,
  instead of a tarball. It leaves the C++ runtime, graphics, fonts, Wayland/X11
  and D-Bus libraries to the host, which stops the `EGL not available` abort and
  the Fontconfig warning flood from the 0.1.0 bundle.
- Never aborts on a broken GL driver: Shelf probes OpenGL and falls back to
  software rendering unless `QT_QUICK_BACKEND` or `QSG_RHI_BACKEND` is set.
- Hyprland: the capsule and tab map floating at their own size from the first
  frame instead of as a tile (which showed a large blurred rectangle). Placement
  uses Hyprland 0.55+ Lua dispatchers, with the classic dispatchers as a fallback,
  and is reapplied after every map and every capsule/panel/tab change.
- Hyprland: expanding no longer maximizes the panel (a redundant `show()` sent
  `unset_maximized`, which Hyprland answers by maximizing).
- Panels open inward from screen edges and return exactly to the capsule or tab
  they came from; the first launch places the capsule at the right edge.
- Dragging the capsule onto or past a screen edge docks it, measured against
  the compositor's usable area (bars excluded).
- Docked Shelf is a slim handle flush with the screen edge, in the style of a
  stashed picture-in-picture: a chevron toward the screen and a dot when items
  are held. Escape and the minimize button return to it without an immediate
  re-peek; clicking it gives the panel keyboard focus.
- On Hyprland the capsule and panel fill the window and match the compositor's
  corner rounding, so its blur no longer shows square margins.
- Text items show only their first line under the tile instead of spilling
  across the panel.
- The version is written once, in `CMakeLists.txt`.

## 0.1.0 — 2026-10-10

- Floating capsule and expanded native Qt Quick surface with dark/light themes,
  collection filtering, multi-selection, menus, keyboard controls and preferences.
- Files and folders staged by reference; text, raw images and temporary handoffs
  held in bounded private owned storage.
- SQLite persistence, multiple collections, ordering, pinning, retention,
  deduplication, crash recovery and Undo for the last addition.
- Native copy-only drag-out, URI/text/HTML drag-in, internal item reordering,
  resizing, edge tabs and compositor-aware placement adapters.
- Arcade Link v1 `shelf.add`, `shelf.show`, and explicit interactive `shelf.pick`;
  capability-driven peer operations with progress and cancellation.
- Isolated tests, cross-platform build/package workflows, dependency provenance,
  and reviewable shared ecosystem onboarding patches.
- Corrected search focus, native drag-source lifetime, and rapid-add GUI overhead;
  untrusted display strings remain literal text.
- Edge hover peeking without focus theft, inward dock expansion and persistent
  tab return; native move-completion snapping and fullscreen capsule policies.
- Platform backdrop adapters with explicit compositor and transparent-window
  fallbacks; two-worker thumbnail decoding with a configurable bounded cache.
- Protected asynchronous preference edits, storage replacement and special-file
  guards, and preflighted Link response limits before committing additions.
- Relocatable Linux runtime launch, required SQLite-only driver deployment,
  per-user Windows packaging and bounded CI failure diagnostics.

- Kept Shelf-owned handoff exports across restarts: startup cleanup read an
  invalid age for every export and deleted them (Qt 6.8.3).
- Collapsing to a docked tab no longer stays clamped to the panel's height.
- Works with Arcade Find both ways, verified with both real apps; contract
  test against Find's real manifest, and a UI test that clicks Shelf's own
  Actions menu into Find. CI green on Linux, Windows and macOS.

- Arcade Link `v0.2.0`: Shelf is a registered family app (Connected apps,
  Tools installs, Wheel slots); the vendored Qt module follows the new tag.
- Releases from a version tag, with `arcade-release.json` for Arcade Tools.

Packages are unsigned. Platform interaction and ecosystem verification
boundaries are recorded in [status](docs/STATUS.md).
