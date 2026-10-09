# Testing

Build the actual application and tests with the [README commands](../README.md).
Tests create generated content and private profiles; they do not use the owner's
files, clipboard history, login items, or desktop shortcuts.

## Automated suites

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ctest --test-dir build --output-on-failure
python3 scripts/check-vendor.py
python3 scripts/test_arcade_release.py
QT_QPA_PLATFORM=offscreen python3 tests/process_smoke.py --binary build/arcade-shelf
```

| Target | Behavior covered |
|---|---|
| Store | Reference safety, strict input validation, symlinks, deduplication, partial batches, restart persistence, quota, recovery, collections, retention, owned handoffs and 10,000-reference collections |
| Service | Real authenticated endpoints, exact manifest, synchronous persistence, explicit picker results, owner cancellation and disabled-Link private lifecycle |
| PeerHub | Capability filtering, version/array/size guards, progress, deadlines, cancellation, malformed replies, stopped-peer launch and peer death |
| LinkVectors / LinkServer | Unchanged upstream protocol vectors, auth, errors, jobs, registry and lifecycle |
| Desktop | MIME URLs/text/HTML, path safety, monitor clamping, shortcut validation and isolated autostart refusal |
| NativeDrag | Two OS processes, real X11 pointer events, delayed native MIME retrieval, copy-only file/folder transfer and original safety |
| Ui | Actual QML/Controller surface, pointer and keyboard actions, collection persistence, filter/preferences, explicit picker, rendered views and a loopback probe confirming untrusted markup triggers no image requests |

NativeDrag deliberately skips under the offscreen/Wayland plugins. A passing
offscreen CTest target does not mean native drag was executed.

## X11 and genuine rendering

Install Xvfb, xauth and xdotool. Run with the actual native xcb plugin:

```sh
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  xvfb-run -a ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  xvfb-run -a ./build/tst_Drag
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  xvfb-run -a python3 tests/process_smoke.py --binary build/arcade-shelf
ARCADE_SHELF_CAPTURE_DIR="$PWD/docs/images" QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  xvfb-run -a ./build/tst_Ui renderedViews
```

The checked-in screenshots are actual `QQuickWindow::grabWindow()` captures of
controlled fixture content. They are not synthetic UI images and do not prove
behavior on a physical desktop or graphics driver.

## Wayland startup

A headless Weston session can load Qt's actual Wayland plugin and run the
process suite. Start Weston with a headless backend, private XDG runtime,
pixman renderer and a test shell. Set `WAYLAND_DISPLAY` to its test socket:

```sh
QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software \
  python3 tests/process_smoke.py --binary build/arcade-shelf
```

This validates executable startup, protocol and persistence through a Wayland
surface. It does not inject compositor-native pointer serials and does not
validate drag-out, Hyprland extensions, keyboard focus, DPI or multiple monitors.

## Real desktop checklist

Use a dedicated test profile. `ARCADE_HOME` and `ARCADE_SHELF_HOME` isolate different
roots; both must be set. A native clipboard operation still uses the test session's
clipboard. Do not run this checklist automatically on the owner's live session.

```sh
export ARCADE_HOME="$PWD/test-profile/link"
export ARCADE_SHELF_HOME="$PWD/test-profile/shelf"
./build/arcade-shelf --show
```

1. On Hyprland, drop a file and a folder from Nautilus onto a collapsed capsule;
   verify count feedback and retained originals. Test spaces, Unicode and symlinks.
2. Expand, select several tiles, drag into Nautilus and another native application;
   verify copy semantics, unchanged originals and correct MIME content.
3. Drag tiles within the panel, then restart and verify ordering. Resize, move,
   dock every edge, switch monitors and change scale; check persisted geometry.
4. Add through Link while another app has focus. The capsule may pulse but must
   not activate. Check topmost/pinning with fullscreen applications and workspaces.
5. Exercise the configured shortcut, portal rejection, registry collisions,
   compositor reload and explicit opt-in login startup with an installed path.
6. Open an explicit picker, apply filters, select one/several items and cancel.
   Verify that no unselected item reaches the caller and exported owned bytes
   remain valid after removing the item.
7. Run actual Look preview/inspect, Lens capture/recognition/pin, Box transformations
   and cancellation, Clipboard picking/Private mode/device confirmation, and Wheel
   named-shelf slots after onboarding patches are synchronized.
8. Repeat movement, drag and selection checks with Windows OLE and macOS pasteboard,
   and test their shortcut/login adapters. Build/package success alone is insufficient.
9. Install/update/remove with Tools after its onboarding patch ships. Verify data
   preservation and stable executable paths with a checksummed package.

## Performance

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  python3 scripts/measure.py --binary build/arcade-shelf --runs 3 --cycles 100 \
  --idle-seconds 5 --output benchmarks/local.json
```

Startup measures process spawn to authenticated public `app.status`. Warm add
uses one connection and includes SQLite commit. RSS and CPU measure the target
process, including its threads and excluding children; invalid counters fail.
The idle sample begins after the capsule feedback settles. Activation measures
the request acknowledgement, not visible compositor/frame latency. Filesystem
caches are not flushed. A zero CPU sample does not establish zero wakeups.

`benchmarks/cloud-debug.json` is an early RelWithDebInfo measurement taken before
the settled-idle refinement and UI notification fixes. Its elevated CPU numbers
describe that intermediate run; its filename is historical, not its build type.
The final milestone measurement is recorded separately in [status](STATUS.md).

## Cloud environment reproduction

The cloud host lacked development tools. Qt 6.8.2 and CMake were unpacked into
`/workspace/toolchain` without system package/profile changes. In that specific
workspace, source `/workspace/toolchain/env.sh` for process-scoped build variables.
Those absolute paths are not runtime dependencies or an installation recipe.
Normal builds use an installed Qt SDK. Native startup was independently probed
under offscreen, Xvfb/xcb and headless Weston; final application evidence is listed
in [status](STATUS.md) and [ecosystem verification](ECOSYSTEM_VERIFICATION.md).
