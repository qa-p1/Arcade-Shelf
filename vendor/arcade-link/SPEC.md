# Arcade Link specification

**Protocol version 1 (frozen) · manifest schema 1**

Arcade Link is how the seven independent apps (Box, Lens, Look, Wheel,
Clipboard, Find and Shelf) and the optional host, Tools, recognize each other and work together. It is a
protocol and a small library, not a process: there is no broker. Apps find
each other through a file-based registry and talk directly over a local
socket. Nothing has to run except the two apps in a conversation.

Implementations: the Rust crate in `crates/arcade-link` (Box, Lens, Look,
Clipboard's core, Find, Tools) and the Qt module in `qt/` (Wheel and Shelf).
Both implementations share the original and additive v0.3 conformance
vectors. Protocol and manifest schema remain 1.

## 1. Principles

1. **Standalone first.** Every app works exactly as it does alone. Every
   integration is additive and hidden when the peer is missing.
2. **Never show a broken entry.** An entry is shown only if the peer is
   installed, its Link is enabled, the action supports this input and this
   OS, and the manifest reports it `available`.
3. **No idle discovery polling.** Link targets no idle timers or polling and
   at most one thread blocked in `accept`. Whole-app idle cost includes the
   UI toolkit and other features; measure it separately from Link overhead.
4. **Never block a UI thread** on discovery, IPC or launching a peer.
5. **Each app keeps its safety model.** A request arriving over the Link goes
   through the same checks as the app's own UI (Lens's secret guard, Box's
   grants and never-overwrite rule, Clipboard's Private mode and "receiving
   never writes your clipboard", Look's no-execution sandbox, Wheel's "never
   run shell strings").
6. **Delegate to the owner.** If a peer owns a verb, use it when present and
   use your own fallback when not.
7. **Content moves by reference.** Files travel as paths; in-memory content is
   written once to a handoff file. Bulk data never goes inline.
8. **The user confirms persistence and outbound actions** in the owner's UI,
   or the caller marks them ↗ with a payload preview.

## 2. Locations

`ARCADE_HOME`, if set, replaces every root: `$ARCADE_HOME/apps`,
`$ARCADE_HOME/run`, `$ARCADE_HOME/handoff`.

Receipts live beside `apps/`, in `installs/`: Linux
`${XDG_DATA_HOME:-~/.local/share}/arcade/installs/`, macOS
`~/Library/Application Support/Arcade/installs/`, Windows
`%LOCALAPPDATA%\Arcade\installs\`, or `$ARCADE_HOME/installs/`.
Install operations take an explicit environment. `ARCADE_HOME` also isolates
application, launcher, icon and CLI paths; it never adds temporary paths to
the real user Path or a shell profile. XDG overrides are honored on Linux.

| | Registry (manifests) | Runtime (endpoints, sockets) | Handoff |
|---|---|---|---|
| Linux | `${XDG_DATA_HOME:-~/.local/share}/arcade/apps/` | `${XDG_RUNTIME_DIR}/arcade/` (fallback `${XDG_CACHE_HOME:-~/.cache}/arcade/run/`) | `${XDG_CACHE_HOME:-~/.cache}/arcade/handoff/` |
| macOS | `~/Library/Application Support/Arcade/apps/` | `$TMPDIR/arcade/` | `~/Library/Caches/Arcade/handoff/` |
| Windows | `%LOCALAPPDATA%\Arcade\apps\` | `%LOCALAPPDATA%\Arcade\run\` (endpoint files only) | `%LOCALAPPDATA%\Arcade\handoff\` |

The runtime and handoff directories are created with mode 0700 and must be
owned by the current user; an implementation refuses a runtime directory that
is a symlink or belongs to someone else.

**Engines.** Helper programs the apps use but never ship (today only
Tesseract) live beside the registry, in `engines/` under the registry's parent
(`${XDG_DATA_HOME:-~/.local/share}/arcade/engines/`,
`~/Library/Application Support/Arcade/engines/`, `%LOCALAPPDATA%\Arcade\engines\`,
or `$ARCADE_HOME/engines`). Launchers go in `engines/bin/`, which every app
searches after `PATH` and the platform's usual install folders, so a copy one
app downloads serves all of them. A download is per user, pinned and
SHA-256-checked before it runs; the user's own install always wins. The Rust
crate implements this behind the `engines` feature.

## 3. Manifests

One file per app: `<registry>/<arcade-id>.json`. Canonical IDs:
`arcade.box`, `arcade.lens`, `arcade.look`, `arcade.wheel`,
`arcade.clipboard`, `arcade.tools`, `arcade.shelf`, `arcade.find`. They are separate from platform bundle
IDs, which never change.

```jsonc
{
  "schema": 1,
  "id": "arcade.look",
  "name": "Arcade Look",
  "version": "0.4.0",
  "link": { "protocol": [1] },
  "executable": "/home/u/Applications/Arcade-Look.AppImage", // $APPIMAGE, never the mount
  "launch": { "background": ["--background"], "invoke": ["--arcade-invoke"] }, // invoke: only with headless actions
  "icon": "/…/arcade.look.png",                              // optional
  "shortcuts": [{ "id": "preview-selection", "accelerator": "Ctrl+Alt+Space" }],
  "settings": { "linkEnabled": true },
  "actions": [ /* Action, below */ ],
  "writtenAt": "2026-10-05T10:00:00Z"
}
```

**Action**

| Field | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | required | `look.preview`, `box:arcade.image.convert#webp` (`#preset` for Box presets) |
| `version` | int | 1 | Bumped on incompatible change; stored by callers (Wheel slots, Box pipelines) |
| `title` | string | required | The verb as the user wants it: "Quick Look", "Compress for sharing" |
| `verb` | string | "" | `preview`, `convert`, `send`, … |
| `accepts` | [type] | [] | Accept patterns (§5.2). Empty: takes no input |
| `produces` | [type] | [] | Output types |
| `effects` | [effect] | [] | §4.3 |
| `interactive` | bool | false | Opens UI; never runs one-shot |
| `privacy` | string | "local" | `local`, `network`, `cloud` |
| `platforms` | [os] | [] | `linux`, `windows`, `macos`; empty means all |
| `available` | bool | true | From cached probes; readers never run probes |
| `reason` | string | — | Why unavailable ("FFmpeg isn't installed") |
| `preset` | string | — | Box: the preset this action runs |
| `featuredFor` | [type] | — | Box: types for which peers show it inline (3–5 per type) |
| `maxBytes` | int | — | Largest accepted input; callers disable the entry above it, with the reason |
| `group` | string | — | Grouping label in long lists |

Rules:

- Optional schema-1 additions are `menu` (cached app-specific `MenuItem`
  extras, §4.3), `logs` (log-directory path), `docs: {version}` (bundled user
  documentation version), and `install` (the receipt method, §8.6, or
  `dev`). They never imply an action or grant permission.
- Tools may publish `settings.trayHost: {enabled, excluded:[arcade-id]}`.
  Missing or disabled means every app owns its icon. Hosting requires a
  running Tools endpoint and its live `tray.host` answer, not just this cache.

- Apps write their manifest on start (off the startup path) and when their
  capabilities change, atomically (temporary file + rename) and only if the
  content changed apart from `writtenAt`.
- With the app's "Connect with other Arcade apps" switch off, the manifest is
  still written (the app shows as installed) with `settings.linkEnabled:
  false` and no actions, and nothing listens.
- Readers ignore unknown fields, ignore files that don't parse, and ignore a
  manifest whose `executable` doesn't exist. Uninstallers and Arcade Tools
  remove the manifest.
- Readers cache manifests by modification time and refresh on a directory
  watch (inotify / FSEvents / ReadDirectoryChangesW, or
  `QFileSystemWatcher`) or on demand, off the UI thread. Opening a menu that
  shows peer entries does no disk or IPC work.

## 4. Transport and wire protocol

### 4.1 Endpoint

The resident instance listens on a local socket and writes
`<runtime>/<arcade-id>.endpoint` (mode 0600, atomic rename):

```json
{ "protocol": [1], "transport": "unix", "address": "/run/user/1000/arcade/arcade.look.sock",
  "pid": 4242, "startedAt": "2026-10-05T10:00:00Z", "token": "<64 hex chars>" }
```

- Unix: a domain socket `<runtime>/<arcade-id>.sock` (mode 0600). If that path
  exceeds 100 bytes (`sun_path` is 104 bytes on macOS), the socket goes in
  `/tmp/arcade-<uid>/` (0700, owned by the user) under a hashed name. Clients
  always read the address from the endpoint file.
- Windows: a named pipe `\\.\pipe\arcade-<hash>-<arcade-id>`, where the hash
  covers the user name and the runtime directory, with a DACL granting only
  the owner and SYSTEM (`D:P(A;;GA;;;SY)(A;;GA;;;OW)`).
- The token is 32 bytes from the OS random generator, sent in `hello`,
  compared in constant time. It is defense in depth on top of the directory
  and pipe permissions.
- An endpoint is dead if connecting fails or `hello` doesn't answer within
  150 ms. A starting app replaces a dead endpoint; it refuses to start a
  second server when a live one answers.
- Existing single-instance channels (Lens's TCP, Wheel's command socket,
  Look's single-instance plugin, Clipboard's D-Bus) stay; the Link endpoint
  is added alongside them.

### 4.2 Messages

One UTF-8 JSON object per line, at most 1 MiB including the newline. Every
message has `v` (the sender's protocol version, ≥ 1).

| Kind | Fields |
|---|---|
| Request | `v`, `id` (u64), `method`, `params` |
| Notification | `v`, `method`, `params` (no `id`) |
| Response | `v`, `id`, and exactly one of `result` / `error` |

`error` is `{ "code", "message", "reason"?, "limit"? }` (§6). Unknown fields
are ignored and never given authority.

```jsonc
→ {"v":1,"id":1,"method":"hello","params":{"token":"…","client":{"id":"arcade.lens","version":"0.2.0"},"protocol":[1]}}
← {"v":1,"id":1,"result":{"server":{"id":"arcade.box","version":"0.2.0"},"protocol":1}}
→ {"v":1,"id":2,"method":"invoke","params":{"action":"box:arcade.image.convert","preset":"webp",
     "inputs":[{"type":"file/image","path":"/…/handoff/7f3c…/region.png","owner":"arcade.lens"}],
     "options":{},"context":{"source":"arcade.lens","interactive":true,"reason":"user-click"}}}
← {"v":1,"id":2,"result":{"job":"j-41"}}
← {"v":1,"method":"job.progress","params":{"job":"j-41","fraction":0.4,"message":"Encoding"}}
← {"v":1,"method":"job.done","params":{"job":"j-41","status":"success",
     "outputs":[{"type":"file/image","path":"/home/u/Pictures/region.webp"}],"message":"Converted to WebP"}}
```

### 4.3 Methods

| Method | Params → result |
|---|---|
| `hello` | `{token, client:{id,version}, protocol:[…]}` → `{server:{id,version}, protocol}`. Must be the first request, within 2 s; anything else, or a wrong token, is answered `denied` and the connection closed. No common version → `version_mismatch` |
| `describe` | `{}` → `{actions:[Action], methods:[string]?}`, the live list and implemented optional methods |
| `invoke` | `{action, version?, preset?, inputs:[Content], options, context:{source, interactive, reason}}` → an `InvokeResult` `{outputs:[Content], message?, data?}` or `{job}` |
| `job.cancel` | `{job}` → `{cancelled: bool}`. Partial outputs are removed by the owner |
| `subscribe` | `{topics:["app.changed", "job.*"]}` → `{topics}`. Job notifications always go to the connection that started the job |
| `app.status` | `{}` → `{id, version, pid, protocol, busy, jobs, status}`. `status` is an app-defined object; it should include `mode`: `"background"` (started with `--background`, no window) or `"foreground"`, so a manager can relaunch the app the same way after an update |
| `app.activate` | `{}` → `{activated: true}` (bring the main window forward) |
| `app.quit` | `{force?}` → `{quitting: true}`, or `busy` while jobs run unless `force` |
| `app.settings` | `{}` → `{opened:true}`. Same behavior as `--settings` |
| `app.restart` | `{mode?}` → `{restarting:true}`. `mode` is `normal` (default) or `force`; `busy` while jobs run unless force. A successor waits for the old process to exit |
| `app.menu` | `{}` → `{items:[MenuItem]}`. App-specific tray extras only; Open, Open Settings, Restart and Quit are implied |
| `app.menu.invoke` | `{id}` → `{done:true}`. Uses the same validation and behavior as the app's own tray item |
| `app.shortcuts.set` | `{id, accelerator}` → `{applied:bool, via, hint?}`. Validates, applies live, persists and rewrites the manifest. `via` is `native`, `portal`, `hyprland-runtime` or `manual`; manual includes a helpful command/binding hint. Invalid or reserved: `denied`; cannot apply: `unavailable` |
| `app.settings.export` | `{}` → `{outputs:[{type:"file/any",path}]}`. Portable settings in a handoff file, excluding secrets and device identity |
| `app.settings.import` | `{inputs:[Content]}` → `{imported:bool,restartRequired:bool}`. Nonempty `file/any` inputs; validate, back up current settings, then apply |

These additions are optional in protocol 1. Feature-detect through
`describe.methods` or an `unsupported` error. An absent list advertises no
optional methods. An older server may instead answer `internal` with
`unknown method …`; Rust's `LinkError::is_unsupported()` recognizes that
legacy response. Existing methods and their result fields remain unchanged.

`MenuItem` is `{id,title,kind:"action"|"toggle"|"separator",checked?,enabled,
effects?}`. `enabled` is required; `checked` belongs to toggles; `effects`
uses the action vocabulary below. IDs are stable within an app. Examples:
Clipboard Private mode and Pause sync; Find Rescan index; Lens Capture text
and Pick color; Shelf New shelf; Box Open pipelines; Wheel Show wheel.
After a change send `app.changed {app,menu:true}` and refresh the manifest
cache. Cached items do not replace live invocation validation.

Notifications: `job.progress {job, fraction?, message}`, `job.done {job,
status: "success"|"error"|"cancelled", outputs, message?, data?, error?}`,
`app.changed {app,menu?:true}`, `tray.host {hosted:bool,restarting?:bool}`.
The `tray.host` topic sends an initial answer when subscribed and subsequent
changes, including a per-app `hosted:false` for exclusions (§8.3).

`app.status.status.tray` is `"own"`, `"hosted"` or `"none"` (no tray).
The app supplies it along with its other diagnostic status. Handler calls
run on connection workers, never the UI thread; dispatch UI changes through
the app's toolkit. Restart is acknowledged after readiness validation and
before the process may exit; apps perform all refusal checks in readiness.

A server sends `{job}` before any notification for that job. If the caller
disconnects, its running jobs are cancelled. A job that ends without a result
reports `internal`.

**Effects** (Lens's names, adopted by every app): `clipboard`, `writes-files`,
`overwrites-files`, `deletes-files`, `network`, `uploads-content`,
`sends-to-device`, `launch-apps`, `opens-ui`, `persists`,
`executes-commands`, `window-control`. **Privacy classes** (Box's):
`local`, `network`, `cloud`.

### 4.4 One-shot mode

`<exe> --arcade-invoke` reads one `invoke` request line from stdin (no
`hello`; the caller spawned it directly), writes `job.progress` lines and
then one response line with the same `id` to stdout, and exits (0 on
success). It must not start any UI, tray, shortcut, listener or manifest
write. Box (tools), Lens (`lens.recognize`) and Look (`look.inspect`)
implement it. Callers use it only for non-interactive actions of an app that
isn't running and whose manifest has `launch.invoke`.

## 5. Content

### 5.1 Types

| Type | Transport |
|---|---|
| `text/plain`, `text/url`, `text/rich` | `text` inline up to 256 KiB, else a handoff file in `path`. `text/rich` carries `html` plus `text` as the plain fallback; when the text goes in `path`, the HTML goes in a second handoff file in `htmlPath` |
| `file/<kind>`, `file/<kind>[]` | `path` (single) or `paths` (array) of existing files. Kinds: `image`, `video`, `audio`, `pdf`, `document`, `spreadsheet`, `presentation`, `archive`, `text`, `code`, `font`, `model`, `any` |
| `folder/reference` | `path` of a directory |
| `structured/<name>` | `data` (inline JSON): `color`, `barcode`, `findings`, `devices`, `table`, `file-info`, `pipelines`, `arcade-action` |
| `screen/region` | `data: {rect:{x,y,width,height}, monitor}` in physical pixels of the virtual desktop |

Other fields: `hints` (semantic detail the type loses, e.g. `["command"]`),
`owner` (the app that created a handoff file), `name` (display or suggested
file name), `size` (bytes).

The kind of a file comes from its extension (`spec/vectors/content.json`).

### 5.2 Matching

An action's `accepts` holds patterns; a value matches if any pattern does:

- `*` matches anything; `text/*`, `file/*`, `structured/*`, `screen/*` match
  their family; `file/any` matches any single file.
- `text/url` and `text/rich` also satisfy `text/plain`.
- An array pattern (`file/image[]`, `file/*[]`) also matches a single file
  (a batch of one). A single pattern never matches an array.
- `type;hint=name` additionally requires `name` in `hints`
  (`text/plain;hint=command`).

### 5.3 Handoff files

- The creator writes `<handoff>/<random hex>/<name>` (file 0600, directory
  0700) and sets `owner`.
- The receiver treats it as read-only, never moves or deletes it, and writes
  outputs to its own output location.
- The creator deletes the directory when the job finishes. Every app removes
  handoff directories older than 24 hours at startup (one directory listing,
  off the startup path).
- Existing user files are never copied; only their path travels.

### 5.4 Structured data

The `data` of each `structured/<name>` value. Readers ignore unknown fields;
writers never remove a field within protocol version 1.

| Name | `data` |
|---|---|
| `findings` | array of `{capability, text, summary, confidence, recognizer, details:[{label, value}]}`; `capability` is Lens's (`text`, `url`, `path`, `command`, `color`, `qr`, `email`, …) |
| `file-info` | `{path, name, type, kind, format, mime, size, modified, width?, height?, durationMs?, tags?, family?}` (`type` is the Link content type) |
| `devices` | array of `{name, platform, online}`; `platform` lower-case (`linux`, `windows`, `macos`, `android`, `ios`, `device`). Never keys, IDs or addresses |
| `pipelines` | array of `{id, name, version, accepts:[type], produces:[type], effects:[effect], interactive}`: `accepts` is the first node's input types (empty for a pipeline that takes no input), `interactive` is true when the first node is interactive |
| `arcade-action` | `{app, action, version, title, preset?, options?, input?}`: a runnable reference to another app's action, e.g. for "Add to Wheel". `options` are passed as the invoke's `options` (a pipeline: `{pipeline: id}`); `input` is a suggested input mode (`none`, `clipboard`, `lens-selection`, `file-selection`) |
| `region` | `{rect:{x,y,width,height}, monitor}` (the same as `screen/region`'s `data`) |
| `color` | `{hex, rgb:[r,g,b]}` |
| `barcode` | `{format, text}` |
| `table` | `{columns:[string], rows:[[string]]}` |

`box.pipeline.run` takes the pipeline as `options.pipeline` (its `id`) and the
first node's input in `inputs`; its effects are the union of its nodes'
effects. A pipeline's `{app:"arcade.box", action:"box.pipeline.run",
options.pipeline}` is what Wheel slots and Lens/Look entries store.

## 6. Errors

| Code | Meaning |
|---|---|
| `not_installed` | No manifest, or its executable is gone |
| `not_running` | No live endpoint; also "stopped while working" when a peer dies mid-job |
| `launch_failed` | Started the app but its endpoint didn't answer within 3 s |
| `timeout` | No answer in time |
| `unsupported_input` | The action doesn't take this input |
| `unsupported` | This server does not implement the optional method |
| `unavailable` | The action can't run here now; `reason` says why |
| `too_large` | `limit` gives the limit in bytes |
| `denied` | `reason`: `private_mode`, `secret`, `user_cancelled`, `disabled`, `token` |
| `busy` | Jobs are running |
| `cancelled` | The job was cancelled |
| `version_mismatch` | No common protocol or action version |
| `internal` | Anything else, including codes unknown to this version |

Standard messages, used verbatim (`{app}` is the failing app's name):

| Code | Message |
|---|---|
| `not_installed` | `{app} isn't installed.` |
| `not_running` | `{app} isn't running.` |
| `launch_failed` | `{app} didn't start.` |
| `timeout` | `{app} didn't respond in time.` |
| `unsupported_input` | `{app} can't open this kind of content.` |
| `unsupported` | `{app} doesn't support this method.` |
| `unavailable` | `{app} can't do this yet: {reason}.` / `{app} can't do this right now.` |
| `too_large` | Clipboard: `Too large to send to your devices (limit {N MB}).` Others: `Too large for {app} (limit {N MB}).` |
| `denied` | `private_mode`: `{app} is in Private mode.` · `secret`: `Not sent: this looks like a password or key.` · `user_cancelled`: `Cancelled.` · `disabled`: `{app} has connections to other Arcade apps turned off.` · other: `{app} declined this request.` |
| `busy` | `{app} is busy. Try again when its current job finishes.` |
| `cancelled` | `Cancelled.` |
| `version_mismatch` | `{app} needs an update to work with this app.` |
| `internal` | `{app} ran into a problem: {reason}.` / `{app} ran into a problem.` |

Limits are written `16 MB`, `1.5 MB`, `32 KB`, `512 bytes` (binary units).

## 7. Lifecycle

```
caller wants action A from app P
 ├─ P's manifest missing, executable missing, Link off, A unavailable,
 │  A not for this OS, or input not accepted       → don't show the entry
 ├─ endpoint alive → connect + hello (≤ 20 ms)     → invoke
 ├─ not running, A headless, P has launch.invoke   → one-shot process
 └─ not running, A interactive                     → spawn `P launch.background…`,
                                                     wait for the endpoint
      (spinner in the caller after 150 ms; give up after 3 s with launch_failed)
```

Connections are cheap enough to open per call. A caller may keep one open
while subscribed (a settings page, an open preview window).

Link overhead targets (the whole-app regression runner in `benchmarks/`
checks only part of these; it does not isolate Link allocations or prove
zero wakeups):

| Measure | Budget |
|---|---|
| Startup time added to an app | ≤ 5 ms on the critical path (manifest and listener after the first frame) |
| Idle cost | 0 timers, 0 polling, ≤ 1 accept thread blocked, ≤ 1 MB RSS |
| Registry read (5 apps) | ≤ 2 ms warm, ≤ 10 ms cold, never on a UI thread |
| connect + hello | ≤ 20 ms p95 |
| Headless invoke overhead | ≤ 30 ms p95 resident, ≤ 300 ms one-shot |

## 8. Consistency

### 8.1 Standard CLI

| Flag | Meaning |
|---|---|
| `--version` | Print the version |
| `--background` | Start resident without a window |
| `--settings` | Open settings |
| `--quit` | Quit the running instance |
| `--restart` | Restart the running instance; successor waits for its exit |
| `--install [--silent] [--no-launch] [--channel C]` | Install this Linux AppImage or macOS bundle; silent has the same defaults, no-launch suppresses starting it |
| `--uninstall [--silent] [--remove-data]` | Remove receipt-listed installation/integration; remove data only when requested |
| `--repair` | Rewrite launcher, CLI and recorded autostart from the receipt |
| `--integration-status` | JSON receipt, entries present, PATH status and hint; no side effects |
| `--arcade-manifest` | Print the manifest JSON; no side effects |
| `--arcade-invoke` | One-shot mode (§4.4), apps with headless actions |

Existing flags keep working. Apps implement the new flags; the library
provides the logic. Install dialogs and explicit install/uninstall operations
belong to the owning app or Tools, with the same silent-mode behavior.

### 8.2 Connected apps page

Every app has the same page:

- A master switch, **Connect with other Arcade apps**. Off: no listener, no
  manifest actions (§3).
- One row per Arcade app: glyph, name, state (*Running · v0.2.0* /
  *Installed* / *Not installed*) and a toggle **Use with <this app's full
  name>** ("Use with Arcade Lens"); off hides that peer's entries in this
  app only.
- For an app that isn't installed: one line on what it would add here, and
  **Get** (opens Arcade Tools if installed, otherwise the releases page).
  Promotion appears only on this page, never in palettes, menus or results.
- A diagnostics expander: registry path, endpoint state, last error.

### 8.3 Tray hosting and menu

Every app that stays resident has the same tray (menu bar) icon. A click opens
Settings (on macOS the click opens the menu). The menu is:

**Open <App>** · **Open Settings** · **Restart Arcade <App>** · separator ·
**Quit Arcade <App>**

"Open <App>" does the app's main thing (Lens: a capture; Wheel: the wheel;
Look, Box, Clipboard: their window). Extras use `app.menu` (§4.3).
Restart starts a successor that waits for
the old instance to exit. Start at login is a Settings switch, not a tray
item.

Tools hosts the apps' trays when running and enabled. Apps remain fully
functional without it. An app watches the registry using OS notifications;
when Tools is running with `settings.trayHost.enabled` and the app is not
excluded, it connects, authenticates with `hello`, and subscribes to
`tray.host`. Tools answers now and on every change, considering the peer's
Arcade ID. An excluded peer receives `hosted:false`.

- `hosted:true`: hide the app's icon, keeping the object constructed so it
  can be shown instantly. At startup defer creation until the first answer,
  for at most 300 ms; if Tools never answers show the icon at that deadline.
- `hosted:false` or EOF/crash: show the app's icon immediately, within 1 s
  (target ≤200 ms). No wait for registry cleanup or endpoint expiry.
- `restarting:true`: retain the hosted state for at most 5 s across Tools'
  restart. If a replacement host answers within the window no icon flashes;
  otherwise show the icon at the deadline. A false answer, exclusion, or
  Link-off always shows it immediately, overriding restart grace.
- With Connect with other Arcade apps off, always show the app's own tray.
  Apps with no tray report `none` and never hide a non-existent icon.

Tools quits by sending `hosted:false` to every subscriber before closing
the connections. Restart/self-update sends `restarting:true` first. Hosting
is not application lifecycle management: apps are independent processes.
The Rust watcher callback runs on a background thread; marshal it to the UI.
Qt's `ArcadeTrayHost` performs registry/socket work on a background thread
and delivers `startupReady` and `stateChanged` through queued signals on the
object's owning thread. Neither watcher's startup waits on the GUI thread.
Tools must attach its hosting helper before accepting subscriptions and
publishing an enabled manifest. After a failed subscription, a registry or
manifest change allows another attempt, including an identical manifest rewrite.
Idle uses blocked reads and OS watchers only, with no polling or timers.
The only hosting deadlines are startup 300 ms and restart 5 s.

Tools renders one submenu for each installed app in catalog order from the
manifest's cached `menu`; opening a menu never synchronously probes a peer.
An excluded app is labeled as keeping its own icon. Selecting an action sends
Link directly to its owner; Tools is never on any other app-to-app action path.
Closing the Tools window keeps its native core resident while hosting or the
cheat-sheet shortcut is enabled and destroys the webview. Clean shutdown sends
`hosted:false`; restart/self-update announces `restarting:true` before shutdown.
Linux uses StatusNotifierItem registration and must recover if the desktop tray
watcher appears late or restarts. OS icon paint and `status.tray` observations
are separate verification boundaries.

### 8.4 Naming

- Entries are named as the verb ("Quick Look", "Compress for sharing", "Send
  to my devices") with the owning app's monochrome glyph
  (`assets/glyphs/*.svg`). No "Powered by".
- Outbound actions carry ↗ and a payload preview.
- Integration surfaces use `assets/tokens.json` (per-app accent, neutrals,
  radius, motion). Existing UIs are not reskinned.

### 8.5 Shortcut data

Defaults change for new installs only; a saved shortcut is never changed.

| App | Linux | Windows | macOS |
|---|---|---|---|
| Box | Ctrl+Alt+Space | Ctrl+Alt+Space | Shift+Super+Space |
| Look | none (Space in GNOME Files) | Ctrl+Alt+Shift+Space | Ctrl+Alt+Space |
| Lens | Ctrl+Alt+Shift+L | Ctrl+Alt+Shift+L | Ctrl+Alt+Shift+L |
| Clipboard | Ctrl+Shift+Space | Ctrl+Alt+V | Shift+Super+V |
| Wheel | F8 | F8 | F8 |
| Shelf | Ctrl+Alt+S | Ctrl+Alt+S | Ctrl+Alt+S |
| Find | Ctrl+Alt+F | Ctrl+Alt+F | Ctrl+Alt+F |

Apps publish their effective shortcuts in `shortcuts`. A shortcut recorder
warns "Used by <app>" when another app already uses the accelerator,
comparing normalized accelerators (case-insensitive, modifier order ignored,
Control = Ctrl, Option = Alt, Cmd = Super; `spec/vectors/accelerators.json`),
from the registry, without IPC.

**Canonical notation.** Files use modifiers `Ctrl`, `Alt`, `Shift`, `Super`,
always in that order, then one key joined with `+`: `Ctrl+Shift+P`,
`Ctrl+Alt+Shift+L`, `Shift+Super+P`. `Super` means Cmd on macOS; `Alt` means
Option. Chord sequences use one space: `Ctrl+K Ctrl+S`. A single modifier
(`Super`) is a valid tap chord. Shifted symbols use the base key and Shift:
`Ctrl+Shift+Equal`, never `Ctrl++`.

Keys are `A`–`Z`, `0`–`9`, `F1`–`F24`, `Space`, `Enter`, `Tab`, `Escape`,
`Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Up`,
`Down`, `Left`, `Right`, `Minus`, `Equal`, `BracketLeft`, `BracketRight`,
`Backslash`, `Semicolon`, `Quote`, `Backquote`, `Comma`, `Period`, `Slash`,
`Print`, `Pause`, `ScrollLock`, `CapsLock`, `NumLock`, `Menu`, `Numpad0`–`Numpad9`,
`NumpadAdd`, `NumpadSubtract`, `NumpadMultiply`, `NumpadDivide`, `NumpadDecimal`,
`NumpadEnter`, `VolumeUp`, `VolumeDown`, `VolumeMute`, `MicMute`, `MediaPlayPause`,
`MediaNext`, `MediaPrevious`, `MediaStop`, `BrightnessUp`, `BrightnessDown`,
`MouseLeft`, `MouseRight`, `MouseMiddle`, `MouseBack`, `MouseForward`, `WheelUp`,
`WheelDown`, `WheelLeft`, `WheelRight`, and `Code<N>` (nonnegative decimal,
without leading zeros except `Code0`) for an unnamed raw keycode.

`normalize()` accepts case-insensitive names, any modifier order, and
whitespace around `+`, and returns canonical text or an error. Aliases:

- Control/Ctl → Ctrl; Option/Opt → Alt; Cmd/Command/Win/Windows/Meta/Logo/Mod4
  → Super. XKB Super_L/Super_R, Control_L/Control_R, Alt_L/Alt_R, Shift_L/Shift_R
  map to their modifier. A final modifier keysym may repeat that modifier
  as a tap key: `SUPER + SUPER_L` → `Super`; ordinary repeated modifiers
  and multi-modifier chords without a key are errors.
- `/ - = , . ; ' \` [ ] \\` map to the corresponding named punctuation;
  Plus → Equal, without an implied Shift. XKB slash/comma/period/minus/equal,
  grave/apostrophe/bracketleft/bracketright/backslash/semicolon are accepted.
- Return → Enter; Esc → Escape; Del → Delete; Ins → Insert; PgUp/Prior/Page_Up
  → PageUp; PgDn/Next/Page_Down → PageDown; ArrowUp/Down/Left/Right → directions;
  ISO_Left_Tab → Tab (without an implied Shift). KP_0–KP_9 and
  KP_Add/Subtract/Multiply/Divide/Decimal/Enter → Numpad equivalents.
- XF86AudioRaiseVolume/LowerVolume/Mute/MicMute/Play/Next/Prev/Stop →
  VolumeUp/VolumeDown/VolumeMute/MicMute/MediaPlayPause/MediaNext/MediaPrevious/MediaStop;
  XF86MonBrightnessUp/Down → BrightnessUp/Down.
- Hyprland mouse:272/273/274/275/276 → MouseLeft/Right/Middle/Back/Forward;
  mouse_up/down/left/right → WheelUp/Down/Left/Right; code:NN → CodeNN.
  No Hyprland modmask API is required; consumers of bind masks must use
  SHIFT=1, CAPS=2, CTRL=4, ALT=8, MOD2=16, MOD3=32, SUPER=64, MOD5=128.

Display uses macOS `⌃⌥⇧⌘` in modifier order followed by the key (`⇧⌘P`),
Windows `Ctrl+Alt+Shift+Win+P`, Linux `Ctrl+Alt+Shift+Super+P`, with friendly
punctuation and arrow glyphs (Slash `/`, Up `↑`, macOS Enter `↩`). Display
strings never go into files. `conflicts(a,b)` is true for equal normalized
sequences or a strict sequence prefix: `Ctrl+K` vs `Ctrl+K Ctrl+S`.
`Super` and `Super+Slash` alone do not conflict under this sequence rule.

**App documents.** `docs/user/shortcuts.json` uses
[`spec/shortcuts.schema.json`](spec/shortcuts.schema.json), draft 2020-12:

```json
{"schema":1,"app":"arcade.find","version":"0.3.0","groups":[
  {"title":"Global","context":"global","shortcuts":[
    {"id":"toggle","title":"Show or hide Find","keys":{"default":"Ctrl+Alt+F"},"rebindable":true}]},
  {"title":"Results","context":"overlay","shortcuts":[
    {"id":"pin","title":"Pin","keys":{"default":"Ctrl+Shift+P","macos":"Shift+Super+P"},"description":"Optional longer text."}]}]}
```

Schema is 1; shortcut IDs match `^[a-z0-9][a-z0-9.-]*$`, unique throughout
the file. Titles are required, nonempty, at most 80 characters. Context is
`global` or a kebab-case area. Keys may contain default/linux/windows/macos;
each recognized value is a canonical string, a nonempty array of alternatives,
or null (unavailable). At least one non-null binding is required overall.
An OS-specific key, including null, overrides default; otherwise use default.
Unknown fields are ignored; malformed recognized keys are errors.
`rebindable` defaults false and is allowed only in app global contexts;
when a manifest is supplied, true IDs must match its `shortcuts[].id`.
Conflicting bindings (including alternatives) in the same context for the
same OS are errors, across group boundaries as well as within a group.
Alternatives within each keys value must also be distinct and free of
prefix conflicts, even when OS overrides make that value ineffective.
Generate `shortcuts.md` as one Action | Linux | Windows | macOS table per
group, with display strings, `<br>` between alternatives and `—` unavailable.

**Third-party sheets.** Catalog `shortcuts/<id>.json` uses
[`spec/shortcut-sheet.schema.json`](spec/shortcut-sheet.schema.json). It has
the same groups but replaces app/version with kebab-case `id`, `name`,
`checkedVersion`, `sources` (at least one https URL), and `match` (at least
one OS: linux `{class:[…]}`, windows `{exe:[…]}`, macos `{bundle:[…]}`).
Matches are case-insensitive. `rebindable` is forbidden, including false.
In sheets, context `global` is reserved for `windows-system` and
`macos-system`; ordinary app-wide groups use `app`.
The Rust and Qt modules and stdlib-only `tools/validate_shortcuts.py` enforce the
same semantic rules in addition to schema shape validation.

### 8.6 Install standard

An install receipt `<installs>/<arcade-id>.json` is atomic and private
(0600 on Unix; per-user ACLs in the Windows installers). Readers ignore
unknown fields. See [`spec/receipt.schema.json`](spec/receipt.schema.json):

With `ARCADE_HOME` set on Windows, filesystem integration and PATH use an
isolated HKCU `Software\ArcadeLink\Isolated\<hash>` key. Rust, Qt and the
installer helpers use the same identity: convert backslashes to `/`, remove
trailing `/`, lowercase ASCII A–Z only, encode as UTF-8, compute FNV-1a with
offset `0xcbf29ce484222325` and prime `0x100000001b3` modulo 2^64, then format
the low 48 bits as 12 lowercase hexadecimal digits. This identity is not
path validation. Fixed vectors are in `spec/vectors/windows-path-identity.json`.
Native installs use HKCU `Environment` and are unaffected by this test key.

```json
{
  "schema":1,"id":"arcade.find","version":"0.3.0","channel":"stable",
  "method":"appimage","managedBy":"self",
  "path":"/home/u/Applications/Arcade/Arcade-Find.AppImage",
  "integration":{
    "desktopEntry":"/home/u/.local/share/applications/arcade-find.desktop",
    "icons":["/home/u/.local/share/icons/hicolor/256x256/apps/arcade-find.png"],
    "cli":"/home/u/.local/bin/arcade-find",
    "autostart":"/home/u/.config/autostart/arcade-find.desktop","uninstaller":null
  },
  "previous":{"version":"0.2.1","path":"/home/u/Applications/Arcade/.previous/Arcade-Find.AppImage"},
  "installedAt":"2026-10-12T10:00:00Z","updatedAt":"2026-10-12T10:00:00Z"
}
```

Methods: appimage, windows-installer, macos-bundle, tarball, manual, dev;
managedBy: self or tools (update owner). Paths are absolute. `previous` is
optional; timestamps are UTC RFC3339 seconds, updatedAt ≥ installedAt.
Portable parsing accepts absolute paths from other operating systems;
filesystem mutations require natively absolute paths without `..` traversal.
Integration paths must remain inside the explicitly configured roots.
Dev builds never write receipts, and advertise install `dev`: executable
under a `.git` directory/file ancestor, under target/build of a source
tree, or `ARCADE_DEV_BUILD=1`. Install detection reports runningFrom,
isAppImage (APPIMAGE with the running executable inside APPDIR), receipt,
installedPath, and NotInstalled/InstalledHere/InstalledElsewhere{version}/DevBuild.

**Linux.** Self-installing AppImages copy (or move on the same filesystem)
to `~/Applications/Arcade/<Name>.AppImage`, stable name without a version,
mode 0755. Install hicolor 16–512 PNGs and scalable SVG, a `.desktop` entry
with Settings/Quit/Uninstall actions and `X-Arcade-Id=<id>`, and a
`~/.local/bin/arcade-<app>` symlink. Rewrite an existing autostart main Exec;
create it only when requested, preserving other options such as Hidden.
Keep a replaced binary in `.previous/` and record it. Write the receipt
after integration succeeds; on failure restore the prior installation.
Only replace receipt-owned or validated app/legacy entries. Leave foreign
files and links intact and report them in `integration.skipped` as
`{path, reason}`; these diagnostic entries are never owned or removed.
Reuse or remove a previous binary only when the old receipt records it;
otherwise choose an unused path. Rollback restores saved links atomically.
Recheck launcher, autostart and CLI ownership on update, repair and uninstall;
a receipt is not proof that a current entry still belongs to the app. Repair
detaches conflicting entries and records them as skipped. Uninstall refuses
before any deletion when a recorded entry has changed ownership, with a path
in the error; repair can detach it before retrying. Integration status reports
these conflicts without writing. Atomic writes replace the final directory
entry itself and never follow a final symlink.
Desktop/icon cache refresh commands, when available, are non-fatal.
Install, repair and uninstall hold an exclusive advisory lock on
`<installs>/<id>.lock` for their whole duration. Relaunch the installed path
with original args only when launch policy requests it: from an AppImage start
it detached with the user's environment (AppImage runtime variables and mount
paths removed) and exit, elsewhere on Unix exec it. "Remove the downloaded
file" applies only to files in the Downloads folder and never fails a
committed install. Launchers written by pre-installer releases under the app's
own desktop or autostart id, running an AppImage named after the app, are the
app's to replace. Uninstall reverses receipt paths only, plus caller-supplied
app data folders if removeData is requested. Repair uses receipt paths and
an explicit moved path, never guesses. Integration status is read-only and
offers a fish/bash/zsh PATH hint; it never edits shell profiles.

Each caller passes its existing desktop ID: Box `dev.arcadebox.app`, Wheel
`com.arcadewheel.ArcadeWheel`, Shelf `arcade.shelf`, Lens `arcade-lens`, Look
`arcade-look`, Find `arcade-find`, Clipboard `dev.arcade.clipboard`.
Autostart IDs may differ (Wheel `arcade-wheel`). Remove stale duplicate
launchers with X-Arcade-Id or a caller-listed legacy desktop ID that point
at a different executable; preserve unrelated entries.

**First run.** Use shared verbatim strings in
[`assets/strings/install.json`](assets/strings/install.json), with
{app}/{version}/{from}/{to} placeholders:

- NotInstalled: “Install {app}?”; adds it to the app launcher and apps
  folder. Options Start at login and Remove the downloaded file; buttons
  Install, Just run it once, Don't ask again for this file.
- InstalledElsewhere, older: “Update {app} {from} → {to}?”; Update or Run
  this copy once.
- InstalledElsewhere, same/newer: “{app} {version} is already installed.”;
  Open installed or Run this copy once.
- Managed by Tools: open Tools to update; InstalledHere: no install prompt.

Never prompt for dev builds or with --background, --arcade-invoke,
--arcade-manifest, --version, ARCADE_NO_INSTALL_PROMPT=1, an explicit
install/uninstall/repair/status request, or a saved dismissal for this file.
The app owns version comparison, saved dismissal and the UI. Installation
I/O runs off the UI thread; toolkit operations are dispatched to it.

**Windows.** Per-user installers use `%LOCALAPPDATA%\Programs\<Name>`;
Start menu entry, optional desktop shortcut and start at login, a shared
`%LOCALAPPDATA%\Arcade\bin\arcade-<app>.cmd` shim, and a receipt. Add the
bin folder once to HKCU user Path, preserving other entries and the registry
string kind, then broadcast WM_SETTINGCHANGE. Remove it when the last shim
is gone. Never write machine Path. Publisher is qa-p1, homepage and Apps &
features metadata are supplied. `/VERYSILENT` (Inno) and `/S` (NSIS) have
interactive install parity; shared includes and sample verification live
in [`packaging/windows`](packaging/windows/README.md). Their ARCADE_HOME
test override writes an isolated registry key, never the login-critical Path.

**macOS.** Offer move-to-Applications on first run (`~/Applications`, or
`/Applications` when explicitly chosen and writable), detect translocation
and whether already there, write a receipt and offer a CLI symlink. Preserve
bundle attributes and quarantine; never remove quarantine or bypass macOS
security. Native bundle moves and Windows PATH helpers are explicit APIs;
the app/installer owns platform packaging and launch consent.

## 9. Versioning

- The protocol version is negotiated in `hello`. Version 1 is frozen; a
  breaking change becomes version 2 and implementations then support N and
  N−1.
- Each action has its own `version`. Stored references (Wheel slots, Box
  pipelines) keep `{app, action, version}`; an incompatible change marks them
  "needs repair" instead of breaking them.
- Manifests carry `schema: 1`; readers accept later schemas and ignore
  unknown fields.

## 10. Security model

The Link authenticates "the same OS user", not "a genuine Arcade app": the
same trust level as each app's CLI. Directory permissions (0700), socket and
endpoint permissions (0600), the Windows pipe DACL, and the random token
keep other users out. Every app applies its own safety rules to Link
requests exactly as to its own UI (§1.5), and nothing exposes history or
clipboard contents without the user acting in the owner's UI.

## 11. Action catalog

| Action | App | Accepts | Produces | Effects | Interactive | One-shot |
|---|---|---|---|---|---|---|
| `look.preview` | Look | `file/*`, `file/*[]`, `folder/reference`, `text/url` | — | opens-ui | yes | no |
| `look.inspect` | Look | `file/*`, `folder/reference` | `structured/file-info` | — | no | yes |
| `look.preview_selection` | Look | — | `file/*[]` with `options.resolveOnly` (the selection, no UI) | opens-ui | yes | no |
| `lens.capture` | Lens | — | `file/image`, `screen/region` | opens-ui | yes | no |
| `lens.capture_and_act` | Lens | — | — | opens-ui | yes | no |
| `lens.analyze` | Lens | `file/image` | — | opens-ui | yes | no |
| `lens.recognize` | Lens | `file/image`, `text/plain` | `structured/findings`, `text/plain` | — | no | yes |
| `lens.pin` | Lens | `file/image` | — | opens-ui | yes | no |
| `box:<tool-id>` (`#preset`) | Box | per catalog | per catalog | per catalog | no | yes |
| `box.open` | Box | `*` | — | opens-ui | yes | no |
| `box.pipeline.run` | Box | per pipeline | per pipeline | union of nodes | first node may be | if the first node isn't |
| `box.pipelines` | Box | — | `structured/pipelines` | — | no | yes |
| `clipboard.add` | Clipboard | `text/*`, `file/image`, `file/any[]` | — | sends-to-device | no | no |
| `clipboard.pick` | Clipboard | — | `text/*`, `file/*` | opens-ui | yes | no |
| `clipboard.devices` | Clipboard | — | `structured/devices` | — | no | no |
| `wheel.add_action` | Wheel | `text/url`, `text/plain;hint=command`, `file/*`, `structured/arcade-action` | — | persists, opens-ui | yes | no |
| `wheel.show` | Wheel | — | — | opens-ui | yes | no |
| `shelf.add` | Shelf | `file/*`, `file/*[]`, `folder/reference`, `text/plain`, `text/url`, `text/rich` | — | persists | no | no |
| `shelf.show` | Shelf | — | — | opens-ui | yes | no |
| `shelf.pick` | Shelf | — | `file/*[]`, `folder/reference`, `text/plain`, `text/url` | opens-ui | yes | no |
| `find.search` | Find | `text/plain` (the query), or `options.query` | `file/*[]`, `folder/reference` | — | no | yes |
| `find.show` | Find | —, `text/plain`, `file/*`, `folder/reference` | — | opens-ui | yes | no |
| `tools.install` | Tools | `text/plain` (an app ID), or `options.app` | — | opens-ui (the user confirms in Tools) | yes | no |

Each app documents its exposed actions in its own repository. Shelf omits
`launch.invoke`: stopped noninteractive additions use `launch.background` and
are persisted by the resident process. Find's `find.search` answers one-shot
from its saved index (no frecency, no content search); `find.show` opens Find
with a query, a folder scope (`in:`), or one file selected ("Reveal in Find").

## 12. Conformance

`spec/vectors/` holds wire messages and version negotiation (`wire.json`),
content matching and file kinds (`content.json`), standard error messages
(`errors.json`), manifests (`manifest.json`) and accelerator normalization
(`accelerators.json`). Additions cover optional requests/results/errors
(`methods.json`), tray-host crash/restart/startup/exclusion/Link-off sequences
(`trayhost.json`), valid/invalid receipts (`receipts.json`), shortcut documents,
sheets and Markdown (`shortcuts.json`), and Windows isolated PATH identity
(`windows-path-identity.json`). Rust (`cargo test`) and Qt (`qt/tests`) run
all of them; the stdlib Python validator runs the shortcut/accelerator vectors.
Qt also tests native receipt/install helpers, bidirectional optional-method
interop with Rust, and its watcher against the Rust fake Tools host. Installer
CI exercises Inno and NSIS silently, including uninstalling an old copy after
a successor install. These tests use temporary paths and isolated registry keys.
`arcade-link mock` is a scriptable fake app for testing a consumer without
building any other app.
