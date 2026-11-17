# Third-party notices

Arcade Shelf is MIT licensed (see `LICENSE`). It includes the open-source parts below, each under its own licence.
Packages carry these notices in `share/arcade-shelf/licenses/` (Linux), next to the program (Windows) or in the app
bundle (macOS).

## In this repository

- **Arcade Link** (MIT OR Apache-2.0; Shelf uses the MIT option): the Qt Link module, the QML design kit, the shared
  Windows installer includes, the release generator and the shortcut tools, copied unchanged from
  https://github.com/qa-p1/Arcade-Link tag `v1.0.13` (commit `a24146fd87ff77ed362f518a414eaf266512f839`). Every file's
  SHA-256 is in `vendor/arcade-link/VENDORED.json` and `vendor/arcade-link/shared-provenance.json` (`VENDORED.md`,
  checked by `scripts/check-vendor.py`). Licence text: `vendor/arcade-link/LICENSE-MIT`.
- **Inter** (SIL Open Font License 1.1), the typeface of the design kit, carried by Arcade Link:
  `vendor/arcade-link/design/fonts/InterVariable.ttf`, licence text `vendor/arcade-link/design/fonts/OFL.txt`.
- **Arcade Wheel** (MIT): `src/link/ShelfInvoke.{h,cpp}` adapts Wheel's `WheelInvoke` at commit
  `550efe1a56270ed85ad1bb1fe080b37005e65c84` (`src/link/PROVENANCE.md`). Licence text: `licenses/Arcade-Wheel-MIT.txt`.

## Linked at run time

- **Qt 6.8.3** (https://www.qt.io/), LGPL-3.0: Core, Gui, Widgets, Network (local sockets only), Sql, Quick, Qml,
  QuickControls2, Svg, Concurrent and, on Linux, DBus and the Wayland and XCB platform plugins; QtTest is used only by
  the tests. Shelf links Qt dynamically and ships its libraries as separate, replaceable files, so a compatible Qt can
  be swapped in or Shelf rebuilt against one with the CMake commands in `README.md`. Licence texts: `licenses/Qt-LGPL-3.0.txt`
  and `licenses/Qt-GPL-3.0.txt` (which LGPL-3.0 builds on); the packages also carry the licence folder of the Qt
  installation they were built with, including the notices of the third-party code inside Qt.
- **SQLite** (public domain), through Qt's QSQLITE driver plugin, the only SQL driver packaged.

## Linux AppImage

The AppImage also bundles shared libraries from its build host (Ubuntu 22.04 in CI) that Qt and its plugins need:
`scripts/package-linux.py` copies what `ldd` resolves, except the libraries every desktop provides (the C library,
`libstdc++`, OpenGL/EGL/Vulkan, fontconfig, freetype, harfbuzz, D-Bus, xkbcommon, Wayland and the X11/XCB client
libraries), which stay the system's. Each build records them in the package itself:
`share/arcade-shelf/licenses/runtime-inventory.json` lists every bundled library with its source path and SHA-256, and
`share/arcade-shelf/licenses/distribution/` holds the Debian copyright file (authors and licence) of every Ubuntu
package they came from. Those files, not this list, are the notice for that build, because the set depends on the host.

## Releases

Release candidates are published with their SHA-256 sums; a stable release republishes the same files with a
`manifest.json` signed by Arcade's release key, which Arcade Tools checks before it installs or updates Shelf
(`SECURITY.md`).
