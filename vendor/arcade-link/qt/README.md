# Arcade Link for Qt

C++20 and Qt 6.8 Core/Network are required. Qt Test is needed only for tests.
Protocol and manifest schema stay 1. Existing `ArcadeLink.h` APIs remain valid.

`ArcadeLink::Server` opts into the seven new methods by assigning `settings`,
`restart`, `menu`, `menuInvoke`, `shortcutsSet`, `settingsExport` and
`settingsImport`. `describe.methods` advertises assigned callbacks. Matching
`Client` helpers expose results and `Error::isUnsupported()` detects older
servers. `notifyMenuChanged()` publishes `app.changed {app, menu:true}`.
Manifests are JSON objects and preserve `menu`, `logs`, `docs`, `install` and
unknown fields. The app owns menu validation, live registration, persistence,
settings backups and successor launch/waiting.

`ArcadeTrayHost` mirrors Rust's app-side watcher. `start()` returns immediately;
its worker watches the registry and Tools socket. Wait for `startupReady` before
creating/showing the icon (300 ms cap); hide an already constructed icon for
`hosted`, show for `own`. Signals run on the object's owning thread through
queued delivery. Crash/quit returns ownership immediately, restart has a bounded
five-second grace, and exclusions/Link-off show the app's icon. There are no idle
timers. Set `Server::trayState = [&watcher] { return watcher.state(); };` for
`status.tray`. Call lifecycle methods on the owning thread. `stop()` is
asynchronous and returns icon ownership without joining the worker.

`ReceiptStore`, `validateReceipt` and `validateNativeReceiptPaths` mirror the
Rust receipt module: portable parsing, native mutation preflight, atomic writes,
0600 on Unix and private inherited user/SYSTEM ACLs on Windows. `ArcadeInstall`
uses an explicit `Environment`, `Runtime`, `AppInfo` and `InstallOptions`; use
`Environment::under` for tests. Run its file operations on a worker. It provides
Linux install/update/rollback/repair/uninstall, integration status/PATH hints,
Windows shims and last-shim PATH removal, and macOS bundle/translocation/move
helpers that retain quarantine. Foreign integration and previous files survive;
skipped entries appear in receipts and diagnostics. Receipt and detection JSON
have the Rust field names. Platform registration errors remain explicit.

`Accelerator::{normalize,isCanonical,display,conflicts}` implements SPEC §8.5,
including XKB and Hyprland aliases. `Shortcuts::{validate,effective,markdown,
sheetMatches}` handles app docs and catalog sheets with the same vectors as
Rust and the stdlib Python validator. Invalid inputs return a diagnostic; files
must contain canonical notation. Install strings are embedded in
`install-strings.inc`, verified against `assets/strings/install.json`.

Build with `add_subdirectory(vendor/arcade-link/qt)` and link `arcade_link_qt`.
Set `ARCADE_LINK_BUILD_TESTS=OFF` in an app build. All new headers/sources and
`install-strings.inc` must be copied together; see `tools/vendor_qt.py` for a
commit-pinned bundle and verification. Wheel's existing two-source checksum
mapping and Shelf's provenance list should expand to every copied file when
adopting v0.3.0. No Qt Tools-side host is required in this release.

The five test executables run every old/new vector, native platform helpers,
Qt client ↔ Rust methods and Qt watcher → Rust fake-host interop. Set
`ARCADE_LINK_CLI` to the built Rust CLI for those interop tests.
