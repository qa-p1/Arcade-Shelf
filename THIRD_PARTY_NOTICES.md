# Dependency provenance and redistribution

Arcade Shelf is MIT licensed. Dependencies retain their own licenses.

* **Arcade Link:** https://github.com/qa-p1/Arcade-Link, immutable annotated
  tag `v0.3.0`, code commit `33650a87b6999fbd553ac678cc7bde7a308cb852`.
  Upstream workspace declares `MIT OR Apache-2.0`; Shelf uses the MIT option.
  The unmodified Qt implementation, conformance vectors and release metadata
  generator are tracked by `vendor/arcade-link/provenance.json`. Upstream at
  this tag has no standalone LICENSE file. Its declared licensing and the
  standard MIT notice are preserved in `vendor/arcade-link/LICENSE-MIT`.
* **Arcade Wheel:** small lifecycle/invocation reference code is adapted from
  https://github.com/qa-p1/Arcade-wheel, MIT licensed. The upstream notice is
  preserved in `licenses/Arcade-Wheel-MIT.txt`.
* **Qt 6:** https://www.qt.io/, dynamically linked. Core, Gui, Widgets, Network,
  Sql, Quick, Qml, QuickControls2, Svg, Concurrent and Linux DBus/Wayland modules are used
  under LGPL-3.0 where available (or a suitable commercial Qt license).
  Test is build-time only. See `https://doc.qt.io/qt-6/licensing.html`.
  Qt distributions may contain additional third-party components; package
  scripts preserve Qt's installed license directory and their notices.
* **SQLite:** https://sqlite.org/, through Qt's dynamically loaded QSQLITE
  plugin. SQLite is public domain. QtSql and the QSQLITE wrapper retain Qt's
  licensing.
* **Platform libraries:** system X11/Wayland, D-Bus and their Qt adapters on
  Linux; OS frameworks on macOS/Windows. The Linux bundler inventories every
  copied library and stores package copyright notices when supplied by the
  distribution. Platform dependencies and their versions vary by build host.

Packages include the source repository URL and build provenance. Qt libraries
remain separate, replaceable files; Shelf does not statically link Qt. Rebuild
Shelf with an alternate compatible Qt using the documented CMake commands.
Release packages are unsigned: SHA-256 sums establish integrity, not an
authenticated publisher. Review generated dependency inventories and applicable
Qt/module license texts before redistribution.
