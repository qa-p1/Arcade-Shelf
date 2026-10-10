# Packaging and delivery

Build with Qt 6.8 or later and CMake 3.25+. Cloud checks used Qt 6.8.2;
the CI matrix selects Qt 6.8.3. These scripts do not publish a release.
Windows/macOS native package builds run on their respective OS.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 scripts/check-vendor.py
```

Linux portable bundle (the Qt prefix contains `bin/qtpaths6`, `plugins/`
and `qml/`; its original installation may instead use distribution paths).
Install `patchelf` on the build machine; it is not a runtime dependency:

```sh
python3 scripts/package-linux.py --build build --qt-prefix /path/to/Qt/6.8.3/gcc_64 --version 0.1.0 --output dist
tar -xzf dist/Arcade-Shelf-0.1.0-linux-x64.tar.gz
./Arcade-Shelf-0.1.0-linux-x64/arcade-shelf
```

The launcher resolves its bundle directory and forwards all command arguments.
The packaged ELF also has inheritable `$ORIGIN:$ORIGIN/../lib` `DT_RPATH`, so
Arcade Link can launch its registered executable directly without development
Qt variables or the launcher. Qt plugins, including QSQLITE, are collected from
the configured SDK's reported and relocated runtime directories. The bundler
requires QSQLITE and verifies a clean registered-ELF background launch before
writing the archive. No install script, privileged operation or desktop
registration is needed. Keep the entire directory when moving it.
glibc and graphics drivers come from the OS. Build on the oldest supported
distribution, then test newer X11 and Wayland systems. The generated library
inventory records the exact host dependency hashes; it is not a portability
test. `cpack --config build/CPackConfig.cmake` creates a smaller developer TGZ
requiring a system Qt installation, with no claim of runtime bundling.

Windows: install NSIS and run from PowerShell:

```powershell
scripts/package-windows.ps1 -Build build -QtPrefix $env:QT_ROOT_DIR -Version 0.1.0 -Output dist
```

This runs `windeployqt`, smokes the staged executable, and builds a per-user
NSIS installer. It requests no administrator access and defaults to
`%LOCALAPPDATA%\Programs\Arcade Shelf`. `/S` and final `/D=...` are supported
by NSIS for Tools. Uninstall quits Shelf, removes its installed manifest and
program files, and preserves collections. CPack ZIP is a developer artifact.

macOS:

```sh
QT_ROOT_DIR=/path/to/Qt/6.8.3/macos scripts/package-macos.sh build 0.1.0 dist
```

This deploys Qt into `Arcade Shelf.app` and creates a DMG. The app is copied to
`~/Applications` for per-user installation. No signing or notarization occurs;
Gatekeeper behavior remains a release-validation task. Do not disable or strip
quarantine as an installation workaround.

Create Tools-compatible metadata once actual package files exist:

```sh
python3 scripts/arcade-release.py --id arcade.shelf --version 0.1.0 --channel nightly \
  --notes https://github.com/qa-p1/Arcade-Shelf/blob/feature/shelf-v0.1/CHANGELOG.md dist
```

The generator is unmodified Arcade Link `v0.2.0` code and writes
`arcade-release.json` plus `SHA256SUMS.txt`. SHA-256 establishes integrity, not
publisher authentication. The checked-in CI builds/tests/packages all three
OS families and uploads development artifacts; it has no release or tag
publishing permissions. A workflow definition is not a successful CI run.
Tools currently accepts Linux tarballs only for Clipboard; the isolated Tools
onboarding patch adds Shelf bundle installation with bounded safe extraction.
Apply and ship that change before claiming Tools can install this package.

## Cloud evidence — 2026-10-10

The Linux runtime bundle was rebuilt from the tested stable executable
using Qt 6.8.2, CMake 3.31.6, GCC 14.2.0 and patchelf 0.18.0. It is a local
development artifact, not a tagged or published release. The build input
`build/arcade-shelf` SHA-256 was
`9dc2bce580dda7e9cdc0487e0c4fa1b4a1bc66da6975d1e918e0a25c6229c695`;
packaging changes only its installed copy's runtime search path.

| Asset | Size | Validation |
|---|---:|---|
| `dist/Arcade-Shelf-0.1.0-linux-x64.tar.gz` | 63,289,608 bytes | 145 copied dynamic libraries; clean registered-ELF startup and full packaged process suite passed |
| Windows per-user `.exe` | Built by Windows CI | `fadd592` compiled, tested, packaged and smoked successfully; uploaded as a development artifact |
| macOS `.dmg` | Not built in this Linux workspace | macdeployqt/hdiutil scripts and CI implemented; native run pending |

Linux archive SHA-256:
`0ec5d26102dc44aeb4a3d20051db8862c743398482149df910953cfd18cff963`.
The unmodified release generator produced `dist/arcade-release.json` with
channel `nightly` and `dist/SHA256SUMS.txt` for this exact file. No upload or
release publication occurred.

All **11 packaged process tests passed in 5.660 seconds**, executing the
extracted ELF directly with development Qt paths removed. This covers resident
lifecycle, malformed/authentication requests, reference batches, partial
successes, duplicates, owned handoffs, symlinks, disabled integration, picker
cancellation and crash recovery. A separate real immutable Link CLI invoked
`shelf.add` while Shelf was stopped; it launched the registered ELF through
`launch.background`, added one reference successfully and shut down cleanly.
Direct `--version`, `--arcade-manifest` and background/status commands also
passed with those development variables absent. The wrapper forwarded
`--version` successfully and exports no Qt or library search variables. This
prevents Shelf-specific runtime paths from propagating into launched peers.

These checks do not exercise interactive compositor behavior. Reproduce the
packaged process suite with:

```sh
mkdir package-smoke
tar -xzf dist/Arcade-Shelf-0.1.0-linux-x64.tar.gz -C package-smoke
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
  QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software python3 tests/process_smoke.py \
  --binary package-smoke/Arcade-Shelf-0.1.0-linux-x64/bin/arcade-shelf
```

The CI workflow includes this packaged process check. Native Nautilus drag-in,
cross-application drag-out, Hyprland movement/docking, multi-monitor/DPI,
Windows OLE drag-out and macOS pasteboard drag-out remain distinct platform
validation tasks. No code signing, Windows signing or macOS notarization has
been performed; no stable release or shared Link tag was published.

The first GitHub [CI run](https://github.com/qa-p1/Arcade-Shelf/actions/runs/37986290672)
for snapshot `a41a4ed` failed in Qt installation before compilation. The
[subsequent run](https://github.com/qa-p1/Arcade-Shelf/actions/runs/37988927517)
for `15a0af1` compiled on Linux, Windows and macOS, then failed validation or
packaging in all three jobs. No successful end-to-end CI run is claimed.
The [repair run](https://github.com/qa-p1/Arcade-Shelf/actions/runs/38024585867)
for `fadd592` completed: the Windows job passed compilation, immutable-vendor
checks, CTest, metadata-generator tests, NSIS packaging, packaged executable
smoke, nightly metadata generation and artifact upload. Its
[development artifact](https://github.com/qa-p1/Arcade-Shelf/actions/runs/38024585867/artifacts/11660007845)
ZIP is 53,864,461 bytes with archive digest
`sha256:fc44f42db52ce467628002f86d4489820575ec6bee232384199e47d5bc3b27ce`.
This digest identifies the uploaded ZIP, not the enclosed installer. Native
interactive Windows drag-and-drop was not exercised. Linux passed both CTest
phases and failed its package step; macOS compiled and failed one Store test
whose nested path exceeded the OS limit. These failures are being corrected;
that repair run is not an all-platform success.

The workflow selects Linux's `linux_gcc_64` archive and only the optional
`qtimageformats` module; QtSvg is included in Qt essentials. The Qt installer
uses the built Node action pinned at v4.2.1 commit
`12aa0336e1e410b3c6dff5e0722547f7a37e8c60`, avoiding a floating nested action.
CI installs `xdotool` and runs Linux native pointer-event drag tests under Xvfb
with `QT_QUICK_BACKEND=software`. Failed jobs emit bounded diagnostic
annotations from their actual CTest logs. Successful native package and
smoke-test results must be checked before publishing distributable releases.

Shared Link/Tools/Wheel onboarding changes are reviewable patches under
[`integration-patches/`](integration-patches/README.md). Their existence does
not make the currently published Tools installer accept Shelf. Shipping these
changes and validating actual update/removal with the final assets is required
for full ecosystem distribution.
