# Architecture

## Process and modules

Shelf is one C++20 Qt application with an ordinary native Qt Quick toplevel.
SQLite stores metadata and preferences. Qt provides platform MIME transport;
the vendored Arcade Link Qt module supplies frozen v1 IPC and discovery.

| Module | Responsibility |
|---|---|
| `src/main.cpp` | Cheap CLI, per-profile lock, secondary launch forwarding, QML engine, tray |
| `src/Controller.*` | GUI state, selection, explicit clipboard operations, input gestures, worker dispatch |
| `src/core/Store.*` | Validation, schema, transactions, collections, owned storage, quota and recovery |
| `src/link/Service.*` | Public actions, resident lifecycle, picker jobs, undo and active output leases |
| `src/link/PeerHub.*` | Cached discovery and capability offers; off-thread peer calls |
| `src/link/ShelfInvoke.*` | App-owned bounded invocation adapter over Link's wire/authentication |
| `src/platform/Desktop.*` | Native drag, move/resize, reveal, placement, shortcuts and opt-in login startup |
| `src/ThumbnailProvider.*` | Asynchronous scaled images and a bounded memory cache |
| `ui/` | Capsule, panel, collections, menus, picker, preferences and accessible interaction |

The GUI thread renders and handles gestures. One event-loop worker owns Store
and the two local servers. Another event-loop worker owns the watched registry.
A temporary invocation thread handles one active peer call; thumbnail work uses
Qt's thread pool only when images are requested. No discovery, filesystem scan,
or clipboard polling timer runs while idle. A cancellation timer exists only
while an interactive picker is outstanding.

UI item notifications are separate from selection and feedback notifications.
Selecting a tile keeps its MouseArea alive, preserving the pointer gesture that
must initiate a native drag. Full text is prepared on a worker after explicit
selection; list snapshots carry bounded previews.

Qt Quick reference sequences may call a property getter again for each indexed
read. Controller therefore caches the converted, filtered item list and
invalidates it only for content, filter, or picker changes. Selection count is
read directly from the selected ID set. Hidden grids and dynamic preference/menu
models are deferred until their surface is used. This avoids rebuilding
invisible controls and repeated JSON-to-variant conversion after rapid additions.

Content names, plain text, URLs, and peer messages render as literal text.
They cannot opt into Qt's automatic rich-text/image loading by containing markup.

## Data model and recovery

SQLite schema version 1 contains `shelves`, `items`, and `meta`. Items record
stable IDs, collection, type, name, original or owned path, text/HTML, byte size,
ownership, order, timestamp, and reference deduplication key. An active collection
and preferences are durable metadata. Foreign keys, WAL, and transactional
additions preserve batches across restarts. Future schemas and malformed databases
fail explicitly rather than being silently replaced.

Original files and folders are referenced. Paths are absolute and validated
with `lstat` on Unix; symlinks retain their spelling and are not canonicalized
into another reference. Unsupported kinds, missing paths, invalid strings and
invalid handoffs are skipped with structured reasons. A request containing
usable values commits them together; duplicate-only requests succeed.

Raw data and temporary peer files use `objects/it_<random-id>/` inside a private
profile. Atomic file writes precede the metadata commit; startup recovery removes
unreferenced Shelf-owned objects. Removal deletes only generated owned objects.
It never deletes an original reference. Startup retention applies to temporary
collections, excluding pinned collections.

Exported owned data receives a reusable private 24-hour handoff. This keeps an
external file copy valid after the user removes an item. Handoff cleanup checks
Shelf's ownership marker and never deletes another creator's directory. Durable
owned content and temporary exports have separate configured quotas.

## Wayland surface decision

Shelf uses an ordinary `xdg-toplevel` through Qt's Wayland backend. It accepts
native MIME drops and starts native `QDrag` transfers with `text/uri-list`.
Movement and resizing use `QWindow::startSystemMove` and `startSystemResize` from
real pointer gestures. This avoids recreating Qt's data-device protocol on a
layer-shell overlay. Wheel's fullscreen, anchored, keyboard-unfocused layer-shell
surface is appropriate to Wheel but provides no verified movable drag precedent.

Ordinary Wayland does not let clients force arbitrary global coordinates or
topmost stacking. On Hyprland, the adapter uses bounded asynchronous `hyprctl`
calls restricted to a client whose PID and exact title identify this Shelf
window. It floats, places, sizes, and pins that client, and explicitly observes
its real coordinates for persistence. No compositor configuration file is
edited. Pinning on Hyprland also spans workspaces; fullscreen behavior remains
part of the real-desktop validation checklist.

On other Wayland compositors, native movement and MIME transfer remain available;
absolute restore, forced edge position, and forced topmost behavior remain under
compositor control. The UI reports that limitation. Transparency is implemented;
there is no app-owned blur adapter in this milestone. Native blur depends on
compositor settings.

X11, Windows, and macOS use Qt's native toplevel movement, resizing, stacking,
and drag backends. Monitor restore is clamped to available geometry. Global
shortcuts use X11 grabs, Windows RegisterHotKey, macOS Carbon, or the Linux
GlobalShortcuts portal. The Hyprland portal adapter uses owned runtime bindings
and checks collisions. Real Windows/macOS interaction has not been validated.

## Native drag lifetime

The drag contains local file URLs, plain text, HTML where available, and an
optional private collection-reorder marker. Copy is the only allowed drag
action. Reordering consumes only known IDs from the current profile.

Qt's X11 drag manager owns a heap `QDrag` after `exec()` returns: deleting it
immediately can leave the target with advertised formats but empty data. The
implementation leaves that lifetime to the native backend. A real two-process
X11 test covers the delayed retrieval and original-file safety.

## Dependencies and evidence

Arcade Link Qt source is unchanged from code commit
`337b85fe85fca3e68ec472a6337aa9ebd77fc7ef` (`v0.1.0`). Hash checks and all protocol
vectors are included. ShelfInvoke adapts Wheel's existing bounded lifecycle
pattern because the shared Qt client can wait indefinitely for a noncompliant
job. No frozen protocol or release tag is changed.

See [status](STATUS.md), [test reproduction](TESTING.md),
[ecosystem evidence](ECOSYSTEM_VERIFICATION.md), and
[dependency notices](../THIRD_PARTY_NOTICES.md).
