# Packaging and delivery

Build with Qt 6.8.3 or later and CMake 3.24+. These scripts do not publish a
release. Windows/macOS native package builds run on their respective OS.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 scripts/check-vendor.py
```

Linux portable bundle (the Qt prefix contains `bin/qtpaths6`, `plugins/`
and `qml/`; its original installation may instead use distribution paths):

```sh
python3 scripts/package-linux.py --build build --qt-prefix /path/to/Qt/6.8.3/gcc_64 --version 0.1.0 --output dist
tar -xzf dist/Arcade-Shelf-0.1.0-linux-x64.tar.gz
./Arcade-Shelf-0.1.0-linux-x64/arcade-shelf
```

The launcher resolves its bundle directory, loads separate dynamic libraries,
and forwards all command arguments. No install script, privileged operation or
desktop registration is needed. Keep the entire directory when moving it.
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
  --notes https://github.com/qa-p1/Arcade-Shelf/blob/main/CHANGELOG.md dist
```

The generator is unmodified Arcade Link `v0.1.0` code and writes
`arcade-release.json` plus `SHA256SUMS.txt`. SHA-256 establishes integrity, not
publisher authentication. The checked-in CI builds/tests/packages all three
OS families and uploads development artifacts; it has no release or tag
publishing permissions. A workflow definition is not a successful CI run.
Tools currently accepts Linux tarballs only for Clipboard; the isolated Tools
onboarding patch adds Shelf bundle installation with bounded safe extraction.
Apply and ship that change before claiming Tools can install this package.

## Cloud evidence — 2026-10-09

The first Linux runtime bundle was generated from the current working tree
using Qt 6.8.2, CMake 3.31.6 and GCC 14.2.0. It is an intermediate development
artifact, not a tagged or published release. Final source fixes require a new
bundle and fresh checksum metadata before distribution.

| Asset | Size | Validation |
|---|---:|---|
| `dist/Arcade-Shelf-0.1.0-linux-x64.tar.gz` | 60,376,771 bytes | Created; 131 copied dynamic libraries; isolated `--version` and `--arcade-manifest` smoke passed |
| Windows per-user `.exe` | Not built in this Linux workspace | NSIS/windeployqt scripts and CI implemented; native run pending |
| macOS `.dmg` | Not built in this Linux workspace | macdeployqt/hdiutil scripts and CI implemented; native run pending |

Intermediate Linux archive SHA-256:
`30167241ed018dbbe195eea45e28305969cc8f211e1e052bf78b152cd3133399`.
This value identifies the intermediate file only, and changes after repackaging.

The package smoke did not exercise interactive compositor behavior. For final
runtime-bundle validation, extract it into a fresh directory and run the real
process suite without development Qt paths:

```sh
mkdir package-smoke
tar -xzf dist/Arcade-Shelf-0.1.0-linux-x64.tar.gz -C package-smoke
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
  QT_QPA_PLATFORM=offscreen python3 tests/process_smoke.py \
  --binary package-smoke/Arcade-Shelf-0.1.0-linux-x64/arcade-shelf
```

The CI workflow includes this packaged process check. Native Nautilus drag-in,
cross-application drag-out, Hyprland movement/docking, multi-monitor/DPI,
Windows OLE drag-out and macOS pasteboard drag-out remain distinct platform
validation tasks. No code signing, Windows signing or macOS notarization has
been performed; no stable release or shared Link tag was published.

Shared Link/Tools/Wheel onboarding changes are reviewable patches under
[`integration-patches/`](integration-patches/README.md). Their existence does
not make the currently published Tools installer accept Shelf. Shipping these
changes and validating actual update/removal with the final assets is required
for full ecosystem distribution.
