# Native material and fullscreen behavior

Shelf keeps an ordinary Qt native toplevel so native drag-and-drop, system move
and resize, focus and hit testing stay in Qt. `Backdrop` and `FullscreenGuard`
supplement that window; neither creates an overlay, changes global compositor
configuration or runs a periodic scan.

## Material

On macOS, `Backdrop_mac.mm` adds an `NSVisualEffectView` as a sibling below the
original Qt content view. It does not replace/reparent Qt's view or replace its
Metal layer. The non-interactive material is clipped to the actual rounded
capsule/panel rectangle in logical pixels, follows Shelf's light/dark appearance
and is removed when hidden or detached. macOS Reduce Transparency disables it;
the adapter listens for accessibility display changes. The normal near-opaque
Shelf tint is the fallback, and the lighter material tint is used only when the
adapter reports an applied effect.

Windows has an official `DWMWA_SYSTEM_BACKDROP_TYPE` adapter for compatible
full-window surfaces on Windows 11 build 22621 or later. It uses the transient
window material and honors high contrast and the user's transparency setting.
The current transparent, frameless capsule is **not a compatible surface**:
Qt 6.8.2 sets `WS_EX_LAYERED` for frameless alpha windows, while the official
system backdrop applies to the native window background rather than an
arbitrary inset rounded region. Shelf preserves its transparent margins and
rounded shape and reports a tint fallback. It does not strip Qt's layered style
or use the undocumented `SetWindowCompositionAttribute` API to claim blur.

On Linux, the compositor owns blur. The adapter reports a translucent tint and
does not claim that an app-owned effect is active. Users may use their desktop's
existing blur preferences; Shelf does not install Hyprland rules or change those
preferences.

## Fullscreen

The passive collapsed capsule can be suppressed without changing the user's
visibility preference. The interactive expanded panel is exempt. When fullscreen
ends, the capsule returns through the normal visibility binding without an
activation request. Observers stop when Shelf is user-hidden, expanded or
detached. They remain connected while a fullscreen app suppresses the capsule:
Qt Wayland can destroy its `wl_surface` on hide, so surface destruction must not
clear suppression and create a hide/show loop.

On Windows, foreground and foreground-window geometry WinEvent hooks trigger
bounded checks against Shelf's native monitor bounds. Own-process windows,
desktop/taskbar classes, minimized and normally maximized windows are excluded.
This is a geometry heuristic for a foreground fullscreen app, not an inspection
of every resident window. A cached monitor permits restoration while Shelf's
surface is unmapped. No timer runs while the desktop is idle.

On Hyprland, the `.socket2.sock` event stream triggers bounded asynchronous
`hyprctl -j monitors` and `clients` snapshots for fullscreen, workspace, monitor
and relevant window changes. Only visible fullscreen mode `2` on Shelf's monitor
can suppress it; mode `1` is maximized. Own-process, hidden and unmapped clients
are excluded. Both numbered workspace IDs and current named workspace addresses
are supported; current Hyprland's authoritative `visible` field takes precedence.
Each process has an 1800 ms deadline and 2 MiB output limit. At most one snapshot
is active; additional events coalesce into one follow-up. Disconnects or malformed
snapshots clear suppression. A disconnected stream retries on the next explicit
Shelf/monitor activation rather than repeatedly waking the desktop.

Other Wayland compositors expose no portable public protocol for inspecting
other apps' fullscreen state. X11 currently delegates fullscreen stacking to its
window manager. These are explicit fallbacks, not verified automatic suppression.
On macOS, ordinary Qt `Window` behavior keeps Shelf in its desktop Space. Shelf
does not adopt Wheel's `FullScreenAuxiliary`/`CanJoinAllSpaces` overlay behavior
or request Screen Recording permission to inspect other apps.

## Validation

`SurfaceEffects` tests cover invalid/clipped material bounds, detach and hidden
surface lifetime, physical monitor coverage, Hyprland monitor/workspace/ownership
filtering, named workspace visibility, malformed/oversized snapshots and the
interactive/user-hidden exemption. These tests validate policy and lifetime
logic. They do not establish native blur appearance or real fullscreen behavior.
Build/test results belong in [TESTING.md](TESTING.md) and [STATUS.md](STATUS.md).

Final native validation still requires:

- macOS: confirm that material stays behind Quick text/tiles during resize,
  docking, theme and focus changes; toggle Reduce Transparency; drag files both
  directions; enter another app's fullscreen Space and return.
- Windows: confirm the current capsule uses the documented tint fallback; open
  a borderless fullscreen app on each monitor, switch focus, expand Shelf and
  leave fullscreen; verify restoration does not activate Shelf.
- Hyprland: enter/exit fullscreen on the same and another monitor, switch normal
  and named/special workspaces, hide/remap Shelf, disconnect/restart the event
  stream, and verify capsule restoration and native file dragging.

The implementation was checked against Qt 6.8.2's `qquickwindow.cpp` alpha-format
handling, `qcocoawindow.mm`/`qnsview_drawing.mm` content-view ownership,
`qwindowswindow.cpp` layered alpha handling, and `qwaylandwindow.cpp` hide/reset
lifecycle. Current Hyprland IPC workspace/visibility fields and fullscreen enum
were inspected at upstream commit `663b29b846e4bad17cfa29941f6d2fbb6b61f139`.
