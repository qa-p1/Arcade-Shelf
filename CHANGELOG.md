# Changelog

All notable changes are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed
- Shelf's Actions menu shows Arcade Link's shared answer to "what can I do with this?" instead of a list of its own:
  the same actions, order, names and limits as your file manager's menu and the browser extension. Actions the
  catalog renames or that you hid elsewhere follow along; an app's own best matches come first. Files selected
  together with a folder get no actions, as everywhere else in Arcade.
- Arcade Box 1.0 lists each saved pipeline as its own action, so Shelf no longer asks Box for its pipelines.
- "Add to Shelf" runs straight from file manager menus and the browser extension, listed after what the item is for;
  "Show Shelf" and "Pick from Shelf" appear only in searchable lists.
- Arcade Link 1.1.1 (Qt module, design kit, release tools); GitHub Actions updated (checkout 7, setup-python 7,
  upload-artifact 7, dependency-review 5, install-qt 4.5.0).

### Fixed
- Shelf keeps every kind of file Arcade Link knows. SVG drawings, Markdown, JSON, CSV, HTML, subtitles and e-books
  were refused with "unsupported type" once their kinds became distinct in Link 1.1, and a file whose kind is a
  narrower one than the app said (an SVG sent as an image) is kept as its own kind.

## [1.0.1] - 2026-11-22

### Fixed
- On X11 without a compositing manager (openbox or i3 alone), the capsule sat in a black box: the see-through margin
  around its rounded card and its shadow can't be drawn there. Shelf now checks whether something composites and, when
  nothing does, fills its window with a square surface; it checks again every few seconds, so starting or stopping a
  compositor changes the look while Shelf runs.

### Changed
- Release, gates and CI call Arcade Link's reusable workflows at v1.0.15.

## [1.0.0] - 2026-11-17

### Changed
- Shelf now runs from an unpacked copy of its AppImage, kept in your data folder under `arcade/runtime`, so the helper process that kept the AppImage mounted no longer stays running in the background. The first start after an update takes a moment longer while the copy is made. Uninstalling Shelf removes its copies.
- Shelf gives memory back a few seconds after it starts and 2 seconds after you close the panel. Opening the panel again before that cancels it.
- Thumbnails use less memory: the cache holds up to 16 MB (64 MB at most, whatever the stored setting says) and is emptied when the panel closes, so thumbnails load again the next time you open it.
- The Linux AppImage is built on Ubuntu 22.04, so it starts on distributions with an older C library (glibc 2.35 and later).
- The app and tray icons are installed under Shelf's desktop id (`dev.arcade.Shelf`), so desktops find the right icon.
- `SECURITY.md` describes what Shelf keeps, what it asks before and how to report a problem; the third-party notices list exactly what each package ships, and the AppImage no longer carries Qt's unused TLS plugins.
- Arcade Link 1.0.13.

### Fixed
- An unknown command-line option now exits with code 2 and prints the usage, as every Arcade app does; it exited 1.

### Security
- Shelf asks before it runs a connected app's action that can run commands or control windows. It already asked before actions that use the network, keep, copy, write or delete anything.

## [0.4.0] - 2026-11-01

### Added
- `--hide` closes the panel; `--status` prints one line with the version and Link state.
- Development: `scripts/check`, `dev`, `package` and `sync-design`; clang-format; qmllint at zero warnings; compiler warnings as errors in CI; `ci.yml`, `tag.yml` and `release.yml`; developer docs in `docs/dev/`.

### Changed
- New fuchsia accent; Inter and the shared Arcade kit (Arcade Link 0.4.15) on the capsule, docked tab, panel, menus, dialogs and the install prompt, in light and dark.
- Settings is its own window with the standard pages: General, Shortcuts, Storage, Backup and restore, Connected apps and About. It opens and closes without touching the panel. New options: backdrop blur and opening a docked Shelf on hover.
- Peers that aren't installed show **Install** in Connected apps. Delete asks **Keep** or **Delete collection**, and peer actions list their effects one per line.
- Shelf's folder moves to `~/.local/share/arcade/shelf` (`%APPDATA%\Arcade\Shelf`, `~/Library/Application Support/Arcade/Shelf`). The copy is verified, then the old folder is removed; if anything can't be copied, the old folder is kept and used. The launcher, login entry and window class are now `dev.arcade.Shelf`. The launcher carries the tagline, keywords and **Settings**; existing launchers and login entries are moved, and one switched off stays off.
- New app icons from the kit and a monochrome tray icon. The tray menu reads **Open Arcade Shelf**, **Collect from clipboard**, **Settings**, **Quit Arcade Shelf**.
- Errors read "Couldn't …" with the reason. Sizes are shown in kB/MB/GB, and the storage quota is set in MB. "Copied" shows briefly, and a filter with no results says Nothing matches "…".

### Fixed
- Starting Shelf right after quitting it no longer fails with "Couldn't contact the resident Shelf instance".
- The install prompt is no longer cut off at its title.

## [0.3.3] - 2026-10-19

### Fixed
- A helper an app starts from its own image (such as a settings window) keeps
  working after the app quits: the previous release stopped passing the AppImage
  runtime's keep-alive pipe to every program, its own helpers included, so
  quitting could unmount the image under them. Now only programs from another
  image (the installed copy, other Arcade apps, a restart through the AppImage
  file) drop it, so installing from Downloads still leaves nothing behind.
- Restarting, and starting other Arcade apps, no longer keep Shelf's image
  mounted.

## [0.3.2] - 2026-10-19

### Fixed
- Installing or updating from your Downloads folder no longer leaves the
  downloaded AppImage mounted and its runtime running. The previous release
  stopped passing on the handle on the AppImage's mount, but the runtime also
  waits on a pipe the app inherits, and the installed copy still inherited that;
  now no inherited handle is passed on.
- On a desktop without a portal for global shortcuts (common on X11), the
  shortcut status says so and how to bind `arcade-shelf --toggle`, instead of
  asking you to install a launcher that is already installed.

## [0.3.1] - 2026-10-18

### Fixed
- Updating or installing from your Downloads folder, starting other Arcade apps
  no longer leave the old AppImage mounted and its runtime (about 20 MB)
  running. A program the app started inherited an open handle on its AppImage
  mount and kept it busy.
- Preferences shows whether Shelf really starts at login (for example after
  installing it from Arcade Tools), not only what was last set there.
- A file item's size updates when you come back to the open shelf, not only when
  it opens.

## [0.3.0] - 2026-10-18

### Changed
- The picker hides items whose original file is gone and says so; file items
  show their current size.
- Lower idle memory on Linux: Shelf renders with Qt's software renderer (about
  65 MiB instead of 127 MiB, identical output). Set `QSG_RHI_BACKEND=opengl` to
  switch back.
- Vendored Arcade Link Qt client re-vendored at commit `2fc7c91`.

### Fixed
- Large rich text added from Arcade Wheel keeps its formatting, and backups that
  include staged rich text work.
- Logs are kept when Shelf starts without a terminal.

## [0.2.0] - 2026-10-14

### Added
- Tools can open settings, restart Shelf, show or create shelves, rebind the
  global shortcut and export/import portable settings. Manifests advertise
  tray extras, docs version, logs and installation method.
- AppImage self-install/update/repair/uninstall, receipt-based headless CLI,
  macOS move-to-Applications and CLI/login integration, and Windows per-user
  installer shims and receipts. Development builds never self-install.
- Portable collection/preferences backups with optional owned content and
  size disclosure; validated restore with a private database backup. Tools
  exports omit owned content and referenced originals remain untouched.
- User guides, shortcut sheets, local docs checks, Link conformance tests,
  real executable CLI/process tests and isolated Windows installer CI.

### Changed
- Vendored Arcade Link v0.3.0 Qt transport, installer, tray watcher, shortcut
  helpers and shared Windows/release tools at commit `33650a87`, with hashes.
- Shelf hides its own tray while Tools hosts it, restores it on disconnect
  or when connections are disabled, and waits for the startup ownership answer.

## [0.1.1] - 2026-10-11

First release checked on a real desktop (Hyprland 0.56.2, Arch Linux, NVIDIA/Mesa
drivers from the host). 0.1.0 had only been tested headless.

### Changed
- Linux ships as a single-file AppImage, `Arcade-Shelf-<version>-x86_64.AppImage`,
  instead of a tarball. It leaves the C++ runtime, graphics, fonts, Wayland/X11
  and D-Bus libraries to the host, which stops the `EGL not available` abort and
  the Fontconfig warning flood from the 0.1.0 bundle.
- Panels open inward from screen edges and return exactly to the capsule or tab
  they came from; the first launch places the capsule at the right edge.
- Dragging the capsule onto or past a screen edge docks it, measured against
  the compositor's usable area (bars excluded).
- Docked Shelf is a slim handle flush with the screen edge, in the style of a
  stashed picture-in-picture: a chevron toward the screen and a dot when items
  are held. Escape and the minimize button return to it without an immediate
  re-peek; clicking it gives the panel keyboard focus.
- Text items show only their first line under the tile instead of spilling
  across the panel.
- The version is written once, in `CMakeLists.txt`.

### Fixed
- Never aborts on a broken GL driver: Shelf probes OpenGL and falls back to
  software rendering unless `QT_QUICK_BACKEND` or `QSG_RHI_BACKEND` is set.
- Hyprland: the capsule and tab map floating at their own size from the first
  frame instead of as a tile (which showed a large blurred rectangle). Placement
  uses Hyprland 0.55+ Lua dispatchers, with the classic dispatchers as a fallback,
  and is reapplied after every map and every capsule/panel/tab change.
- Hyprland: expanding no longer maximizes the panel (a redundant `show()` sent
  `unset_maximized`, which Hyprland answers by maximizing).
- On Hyprland the capsule and panel fill the window and match the compositor's
  corner rounding, so its blur no longer shows square margins.

## [0.1.0] - 2026-10-10

Packages are unsigned. Platform interaction and ecosystem verification
boundaries are recorded in [status](docs/STATUS.md).

### Added
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
- Edge hover peeking without focus theft, inward dock expansion and persistent
  tab return; native move-completion snapping and fullscreen capsule policies.
- Platform backdrop adapters with explicit compositor and transparent-window
  fallbacks; two-worker thumbnail decoding with a configurable bounded cache.
- Protected asynchronous preference edits, storage replacement and special-file
  guards, and preflighted Link response limits before committing additions.
- Relocatable Linux runtime launch, required SQLite-only driver deployment,
  per-user Windows packaging and bounded CI failure diagnostics.
- Works with Arcade Find both ways, verified with both real apps; contract
  test against Find's real manifest, and a UI test that clicks Shelf's own
  Actions menu into Find. CI green on Linux, Windows and macOS.
- Releases from a version tag, with `arcade-release.json` for Arcade Tools.

### Changed
- Arcade Link `v0.2.0`: Shelf is a registered family app (Connected apps,
  Tools installs, Wheel slots); the vendored Qt module follows the new tag.

### Fixed
- Corrected search focus, native drag-source lifetime, and rapid-add GUI overhead;
  untrusted display strings remain literal text.
- Kept Shelf-owned handoff exports across restarts: startup cleanup read an
  invalid age for every export and deleted them (Qt 6.8.3).
- Collapsing to a docked tab no longer stays clamped to the panel's height.
