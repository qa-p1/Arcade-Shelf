# Ecosystem verification

Verified on 2026-10-09 and, for Find and the onboarding branches, again on
2026-10-10. These results distinguish
real peer executables, reference protocol clients, mocks, and source inspection.
An offscreen Qt run exercises the real Shelf service and SQLite persistence; it
does not prove desktop drag-and-drop, focus, compositor behavior, or platform UI.

## Source snapshots

| Repository | Inspected commit | Relevant verified state |
|---|---|---|
| Arcade Link main | `92aa03c1c062628757a48a1838cb54c3916a017c` | Main includes documentation/tool changes after the frozen release. |
| Arcade Link v0.1.0 | `337b85fe85fca3e68ec472a6337aa9ebd77fc7ef` | Immutable code baseline used for the Rust CLI and reference tests below. |
| Arcade Wheel | `550efe1a56270ed85ad1bb1fe080b37005e65c84` | Qt Link implementation and app-owned bounded invocation reference. |
| Arcade Look | `3e7ae7e26389385e0a92861d4e0360185f008308` | Preview and inspection contracts inspected in source. |
| Arcade Lens | `26b353207ec99adf946ab5dd894f2b4aafcc5f99` | Capture, recognition, analysis and pin contracts inspected in source. |
| Arcade Box | `3418932e772c94ed38600eda8671f703f63030d4` | Main and arcade/link coincide; shared native tool runtime and Link handlers inspected. |
| Arcade Clipboard | `da848cdc46a31e2337138c2fa493eaf71ddeee4f` | Main and arcade/link coincide; Private mode, 16 MiB limit, picker and handoff behavior inspected. |
| Arcade Tools | `16b5bb0b69729f3eb95b1eed722933aba9e85bdb` | Main; installer validation, lifecycle, release schema and fixed application catalog inspected. |
| Arcade Find feature/find-v0.1 | `9a9c9892e72a3fb8acceabdaaee2794e19dbdd30` | Resident Link service with `find.search` (also one-shot) and `find.show`; generic peer offers. |

Shared onboarding changes were reviewed as separate pull requests, now merged
([ONBOARDING.md](ONBOARDING.md)); `v0.1.0` was not changed, and Link `v0.2.0` adds Shelf and Find.
The baseline results below came from a detached checkout of the release code,
independent of those patches.

## Frozen Rust Link client against Shelf

The actual `arcade-link` Rust CLI was built from the immutable v0.1.0 checkout.
The actual `build/arcade-shelf` executable ran with `QT_QPA_PLATFORM=offscreen`,
`QT_QUICK_BACKEND=software`, private `ARCADE_HOME`, `ARCADE_SHELF_HOME`, XDG data,
config, cache and runtime directories, and generated test content. No real
clipboard, login items, user files, or live desktop were used.

**14 checks passed; 0 failed:**

1. Single-file addition stores an original-path reference with `owned=0`.
2. Multiple files arrive in a Link file array and persist.
3. Mixed file arrays and individual folder references persist.
4. A duplicate-only request succeeds with `data.skipped[].reason=duplicate`.
5. Missing paths alongside valid paths produce partial success and structured skips.
6. Only missing paths return `unsupported_input` and the standard human message.
7. Plain text, URLs and rich text with a plain fallback are accepted.
8. A symlink remains stored by its unresolved original path.
9. A 300,013-byte text handoff is copied into Shelf-owned storage before success
   and remains intact after its creator deletes the handoff.
10. `shelf.show` targets the named Quick Shelf collection.
11. Caller cancellation sends `job.cancel`, ends the pending picker, returns the
    standard cancellation error, and leaves zero running picker jobs.
12. A second Shelf launch forwards to the original resident; its PID is unchanged.
13. The reference client starts a stopped Shelf through `launch.background`,
    successfully adds content, and observes prior SQLite items after the restart.
    The published manifest contains no `launch.invoke`.
14. Disabled integration publishes `linkEnabled=false` and zero actions, removes
    the public endpoint, refuses the reference client, and retains private
    single-instance `--quit` handling.

The full 14-check harness was rerun successfully against the final current build
on 2026-10-09. An additional lifecycle check verified that foreground and
background launches both report their original `status.mode` after `shelf.show`;
revealing a background instance does not change its startup mode for Tools.

The exact development harness and result log in this workspace are
`/workspace/scratch/verify_shelf_reference_cli.py` and
`/workspace/scratch/ecosystem-cli-verification.json`. Shipping verification uses
the repository tests and the shared isolated runner; scratch paths are not build
dependencies.

For reproduction with independently built binaries:

```sh
# In an immutable Arcade Link v0.1.0 checkout:
cargo test --workspace --all-features --locked
cargo build -p arcade-link-cli --locked

# In separate terminals with the same private environment:
export ARCADE_HOME=/absolute/test-profile/link
export ARCADE_SHELF_HOME=/absolute/test-profile/shelf
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/arcade-shelf --background
arcade-link describe arcade.shelf --json
arcade-link invoke arcade.shelf shelf.add --file /absolute/test-profile/example.txt --json
arcade-link invoke arcade.shelf shelf.add --input-json \
  '{"type":"file/any[]","paths":["/absolute/test-profile/a.png","/absolute/test-profile/b.pdf"]}' \
  --input-json '{"type":"folder/reference","path":"/absolute/test-profile/assets"}' --json
arcade-link invoke arcade.shelf shelf.show --option shelf=quick --json
arcade-link quit arcade.shelf
```

Create the generated example files first; isolate XDG directories as in
[TESTING.md](TESTING.md). Native runs can omit the offscreen variables.

## Reference and mock tests

The immutable Rust release passed `cargo test --workspace --all-features --locked`
with **42 passed, 0 failed, 1 ignored**. The ignored case downloads Tesseract and
was deliberately not executed. Passing groups include the library, authenticated
transport and jobs, all five conformance-vector groups, and four actual CLI
subprocess tests. The CLI build also succeeded. Toolchain: rustc 1.99.0, Linux
x86_64; code remains constrained by its declared Rust 1.88 minimum.

Shelf's `tst_PeerHub` passed **9 QtTest rows** (7 behavioral cases plus setup and
teardown), with Qt 6.8.2 and GCC 14.2. Tests use real local sockets and explicitly
scripted mock peers. They verify cached capability filtering, array guards,
Clipboard's one-Content limit, master and per-peer switches, platform and
availability checks, byte limits, progress, cancellation when a peer never
sends `job.done`, deadlines, a killed peer, malformed replies, real file-size
revalidation, stopped-peer background launch, and Box pipeline effect contracts.
This is consumer protocol evidence, not a claim that native peer UIs ran.

## Real Find against real Shelf

Added 2026-10-10, with both apps built from their feature branches (Find
`feature/find-v0.1`, Shelf `feature/shelf-v0.1`, Qt 6.8.3), each in an
isolated profile:

- **Find's end-to-end script** (`scripts/e2e-linux.sh` in Arcade-Find, with
  `ARCADE_SHELF_BIN` pointing at this repository's `build/arcade-shelf`):
  49/49 checks in headless Sway (layer shell) and Xvfb. Driving Find's real
  overlay with the keyboard, a 4-item mixed selection → Tab → "Add to Shelf"
  lands in Shelf's SQLite store as references (`owned=0`) and Find stays open
  with Shelf's message; with Shelf stopped, Find launches it through
  `launch.background` and the add still lands; Shelf's exact `find.show`
  request for one file opens Find with that file selected.
- **Link's ecosystem runner** (onboarding branch, `tools/e2e.py --only find`):
  3/3. `find.search` answers resident and, after `quit`, one-shot through
  `launch.invoke` without leaving a resident; `find.show` for a file selects
  it and for a folder scopes the search (`in:`); Find's `find.search` outputs
  sent to the real `shelf.add` are all added by reference.
- **`tst_PeerHub::findManifestOffersShowAndSearch`** (this repository): with
  Find's real manifest (`tests/fixtures/arcade.find.manifest.json`, from
  `arcade-find --arcade-manifest`), Shelf offers `find.show` for one file,
  folder or text item and `find.search` for a text item, neither for a
  multi-selection, and its `find.show` payload passes pre-flight validation.
- **`tst_Ui::actionsMenuDrivesFind`** (this repository): the real QML window,
  clicked like a user, against a Link peer serving Find's real manifest.
  Selecting a file, Actions → "Search in Find" sends `find.show` v1 with that
  file (`context.source` `arcade.shelf`) and shows Find's reply; on a text
  item, Actions → "Find matching files" sends `find.search` with the text and
  the returned file is added to the shelf by reference. Offscreen and xcb.
- Find's file kinds and Link's (Rust and the Qt module Shelf validates with)
  were compared entry by entry: all 142 extensions agree, so Shelf never
  rejects a Find result as `type_mismatch`.

Not covered: drag and drop between the two, and real desktops.

## Actual native Box CLI

The actual Box native CLI built successfully with
`cargo build -p arcadebox --bin arcade-box --locked -j2`, using its pinned
Rust 1.95 toolchain, from the Box commit above. No Tauri frontend or webview was
built or installed for this check.

Its real `--arcade-invoke` handler accepted the Link v1 action
`box:arcade.text.case` with `options.mode=upper` and transformed generated input
`hello arcade` into a `text/plain` output containing `HELLO ARCADE`. The returned
Content was then added successfully to the actual Shelf executable through the
frozen Rust reference client. The input and output stayed within a private test
profile. Evidence: `/workspace/scratch/box-oneshot-shelf-verification.json`.

This verifies the shared native Box transformation handler and Shelf's import of
its real output. It does not claim that Shelf's GUI selection/menu or Box's
interactive desktop shell were exercised against each other.

## Shared onboarding candidate checks

The Link, Tools and Wheel branches now register Find as implemented as well
as Shelf. Their checks, rerun on 2026-10-10, are recorded with the exact heads
in [ONBOARDING.md](ONBOARDING.md#verification-before-merging):
Link Rust 36 and Qt 2/2; the ecosystem runner's shelf 3/3 and find 3/3 with
real binaries; Tools 15/15 focused plus lifecycle 4/4 and 3/3 through the
isolated runner; Wheel's full CTest 9/9. Tools' checks use generated fixtures
rather than real platform installers; Wheel's use protocol mocks rather than
a native slot session.

## Remaining native ecosystem checks

Find runs against Shelf for real (above), and Box's native CLI output was
imported for real. Look, Lens, Clipboard and Wheel contracts are implemented
through their real capabilities and verified by source plus protocol mocks;
their native UI flows have not been executed as real peers. The released
Tools manager recognizes Shelf only after the onboarding change and a
synchronized dependency ship; writing a Shelf manifest does not update its
fixed app list.

Final validation still needs real Hyprland/Nautilus transfers, peer capture and
preview windows, native Clipboard Private mode/device transfers, Wheel slot
assignment, Tools installer/update lifecycle, and corresponding Windows/macOS
interactive runs. Successful protocol checks do not replace those desktop checks.
