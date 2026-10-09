# Arcade Link

Canonical ID: `arcade.shelf`. Protocol 1 and manifest schema 1. The Qt source is
vendored unchanged from Arcade Link `v0.1.0`; `scripts/check-vendor.py` verifies
its hashes. The actual Find handoff governs payloads and lifecycle.

## Public actions

| Action | Inputs | Output | Effects | Interactive |
|---|---|---|---|---|
| `shelf.add` v1 | `file/*`, `file/*[]`, `folder/reference`, `text/plain`, `text/url`, `text/rich` | Message and structured added/skipped metadata | `persists` | No |
| `shelf.show` v1 | None; optional `options.shelf` | Showing message | `opens-ui` | Yes |
| `shelf.pick` v1 | None; optional `options.multiple`, `options.types` | Explicitly selected files, folders, plain text and URLs | `opens-ui` | Yes |

There is no `shelf.open`. The manifest advertises
`launch.background=["--background"]` and deliberately omits `launch.invoke`.
Stopped noninteractive adds therefore start the persistent resident service.

### Add

Options are `{ "shelf"?: "collection-id", "reveal"?: true }`. Defaults are the
active collection and visible capsule feedback. The action answers synchronously
after the SQLite transaction commits. It does not activate the window or expand
the panel. Owner UI offers Undo for the last addition.

```json
{
  "action": "shelf.add",
  "version": 1,
  "inputs": [
    {"type": "file/any[]", "paths": ["/home/u/logo.png", "/home/u/brief.pdf"]},
    {"type": "folder/reference", "path": "/home/u/assets"}
  ],
  "options": {},
  "context": {"source": "arcade.find", "interactive": true, "reason": "user-click"}
}
```

```json
{
  "message": "Added 2 items to Quick Shelf · 1 already there",
  "data": {
    "shelf": {"id": "quick", "name": "Quick Shelf"},
    "added": [{"item": "it_…", "path": "/home/u/logo.png"}],
    "skipped": [{"path": "/home/u/brief.pdf", "reason": "duplicate"}]
  }
}
```

The shortened `added` list above illustrates field names; real responses include
every added item. Mixed files and folders use multiple Content entries because
Link has no ordered mixed array type. Reference additions never read, execute,
move, or delete original file content. Invalid values do not gain authority from
`context.source`, `interactive`, or `owner`.

Partial successes remain successes. No usable content returns
`unsupported_input`; excess owned data returns `too_large` with `limit`.
Unsupported action versions return `version_mismatch`. Other errors use Link's
standard codes and human-readable messages.

### Show and pick

Show accepts an optional collection ID or exact name. Pick is a cancellable job
while Shelf's selection UI is open. Multiple selection defaults to false.
Filters use standard Link accept patterns. An existing collection is never
automatically returned. Escape returns `denied` with `reason=user_cancelled`;
caller cancellation/disconnect cancels the outstanding job.

Pick returns file references, individual folders, and bounded inline text. Large
or aggregate text spills into private Shelf-owned handoffs. Rich text uses its
plain fallback because the pick manifest promises `text/plain`. Outputs are
bounded to the 1 MiB wire envelope. An exported owned image gets a durable
24-hour temporary snapshot so removal cannot break the receiver's copy.

## Ownership and limits

Same-user IPC does not authenticate an application identity or genuine gesture.
Shelf validates count, strings, absolute paths, actual filesystem types, byte
sizes, and quotas. File kinds use Link's extension table. Non-UTF-8 Unix paths
cannot be represented lossily. Symlinks remain unresolved references.

Only a valid owner-tagged file inside the actual Link handoff root is copied as
temporary content. An owner field on a durable Box output is provenance and the
path remains a reference. A handoff symlink escaping the handoff root is rejected.
Large text paths must be valid private handoffs. Necessary bytes are copied before
the incoming add acknowledges success.

Defaults: 1,000 values per request; 256 KiB inline content; 16 MiB text handoff;
512 MiB durable owned quota; a separate 512 MiB temporary export budget. Cleanup
runs at startup and checks Shelf ownership markers. Active peer calls retain
selected owned data until completion. Originals are never cleaned up by Shelf.

## Peer consumption

Registry updates come from filesystem notifications on a worker. Menus use a
cached snapshot. Every offer checks the master switch, peer preference,
installation, executable, platform, action availability, all input types, batch
support, and maximum size. Blocking IPC and launching happen on workers.
Progress, bounded deadlines, cancellation, and peer failure are handled.

| Peer | Implemented consumer behavior | Verification boundary |
|---|---|---|
| Look | Preview and inspect compatible selections; display inspection data | Source contracts and mocks; native peer UI not yet run |
| Lens | Capture source, image recognition/analysis/pin offers; persist image handoffs | Source contracts and mocks; capture desktop needs testing |
| Box | Manifest tools and cached running-peer pipeline catalog; progress/cancel; retain outputs | Protocol mocks and source; real engine flows pending |
| Clipboard | Explicit picker and compatible device-send action; one-Content and size guards | Private mode/device behavior delegated to peer; native tests pending |
| Wheel | `shelf.show` public action and explicit named-shelf action reference | Shared app allowlist requires onboarding patch |
| Find | Generic incoming file/folder payloads; optional advertised `find.show` | Actual Find snapshot has no Link service yet |
| Tools | Get entry invokes `tools.install` when available; release URL fallback | Shared catalog/install mappings require onboarding patch |

Effects with persistence, file writes, clipboard changes, or outbound transfer
show a payload confirmation in Shelf. Peer-owned conversion engines and viewers
are not reimplemented. Look advertises URL input but its verified implementation
handles file URLs; ordinary web links open through the desktop browser.

## Lifecycle and preferences

The master Connected apps switch writes an installed disabled manifest with no
actions and stops the public listener. A separate private per-profile lifecycle
endpoint remains available for `--show`, `--settings`, `--toggle`, `--status`, and
`--quit`. It uses the same Link authentication/transport, is not published as an
ecosystem capability, and provides no bypass to disabled public actions.

`ARCADE_HOME` isolates Link roots; `ARCADE_SHELF_HOME` separately isolates Shelf
data and its instance lock. The effective global shortcut is in the manifest.
Per-peer toggles hide and deny outbound calls without changing peer preferences.

See [real client and mock evidence](ECOSYSTEM_VERIFICATION.md),
[onboarding patch status](INTEGRATION_PATCHES.md), and [test commands](TESTING.md).
