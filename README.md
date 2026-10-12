# Arcade Shelf

Arcade Shelf is a floating place to collect files, folders, images, text and links while you work. Keep useful references together, then drag or send selected content to another app.

![Arcade Shelf dark theme, captured from the running Qt application](docs/images/expanded.png)

## Install

Install or update Shelf from **Arcade Tools → Apps**, or install it by hand using the [getting-started guide](docs/user/getting-started.md). Tools can host Shelf's tray controls in the shared Arcade tray icon; Shelf keeps its own icon when Tools is closed.

## Works with other Arcade apps

Add Find results to Shelf, preview items with Look, or send selected content to Box, Clipboard, Lens and Wheel. See [how the Arcade apps connect](docs/user/with-other-apps.md).

Read the [Arcade Shelf user guide](docs/user/) for setup, features, shortcuts and help.

## Development

### Build and run

Requirements: CMake 3.25+, a C++20 compiler, Qt 6.8+ with Core, Gui, Widgets,
Quick, QuickControls2, Network, Sql, Svg, Test, the SQLite driver, and runtime QML
modules. Linux also uses Qt DBus and X11 when available. No Electron, bundled
browser, account, or telemetry is used.

On Debian with Qt 6.8 packages:

```sh
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-declarative-dev \
  qt6-svg-dev qt6-wayland libqt6sql6-sqlite libx11-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtqml-workerscript
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
./build/arcade-shelf
```

For other platforms, use a Qt 6.8+ SDK and set `CMAKE_PREFIX_PATH` to its root.
Windows uses MSVC; macOS uses Clang. Platform CI and package scripts are included;
native Windows/macOS interaction still needs verification.

### Command line and lifecycle

```sh
./build/arcade-shelf --show
./build/arcade-shelf --add /absolute/path/to/file
./build/arcade-shelf --background
./build/arcade-shelf --settings
./build/arcade-shelf --status
./build/arcade-shelf --quit
```

A second launch forwards to the resident instance. `--background` starts the
service without showing a window. `--version` and `--arcade-manifest` have no
registration or filesystem side effects. Shelf deliberately does not implement
one-shot `--arcade-invoke`.

### Keyboard and Arcade Link

The default global shortcut is `Ctrl+Alt+S`; it is configurable, and clipboard
shortcuts are used only inside Shelf. Global shortcut collisions are checked
against cached Arcade Link metadata. Where the portal cannot bind a shortcut, a
compositor binding can run `arcade-shelf --toggle`.

Shelf exposes `shelf.add`, `shelf.show`, and `shelf.pick` through frozen Arcade
Link v1. A picker returns only what the user explicitly selects. Installed peers
are discovered by capabilities; unavailable integrations are hidden. See
[the contracts and ownership rules](docs/ARCADE_LINK.md),
[ecosystem verification](docs/ECOSYSTEM_VERIFICATION.md), and
[ecosystem onboarding](docs/ONBOARDING.md).

### Data and limits

Metadata and preferences live in SQLite under Qt's per-user application-data
directory (`qa-p1/ArcadeShelf`). `ARCADE_SHELF_HOME` overrides Shelf's profile;
`ARCADE_HOME` independently overrides Link's registry, runtime, and handoffs.
Tests must isolate both.

The default owned-content quota is 512 MiB. Exported temporary handoffs have a
separate budget of the same size and a 24-hour lifetime; metadata and SQLite
overhead are additional. Temporary shelf items expire after 30 days by default
at startup; pinned collections are retained. Retention 0 disables expiration.
Requests accept at most 1,000 values, 256 KiB inline text, and up to 16 MiB text
through private handoffs. Thumbnails are generated lazily and their memory cache
defaults to 64 MiB with at most two decoder workers. The saved
`thumbnailQuotaBytes` setting bounds this cache; zero disables it. Qt Quick's
visible textures are additional memory. There is no idle filesystem polling or
clipboard history.

### Verify and package

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ctest --test-dir build --output-on-failure
python3 tests/process_smoke.py --binary build/arcade-shelf
python3 scripts/check-vendor.py
```

Linux native drag tests require Xvfb and xdotool. Wayland startup can be tested
with headless Weston; real Hyprland/Nautilus behavior requires a desktop session.
See [testing instructions](docs/TESTING.md), [packaging](docs/PACKAGING.md),
[architecture](docs/ARCHITECTURE.md), and [changelog](CHANGELOG.md).

Shelf is [MIT licensed](LICENSE). Qt and other dependencies retain their own
licenses; see [dependency provenance](THIRD_PARTY_NOTICES.md). Development packages
are unsigned. A version tag publishes a GitHub release through the CI workflow.
