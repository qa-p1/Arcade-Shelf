# Ecosystem verification

Verified in the Codex Cloud workspace on 2026-10-09. These results distinguish
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
| Arcade Find feature/find-v0.1 | `5700d0d948decbe47d9365d98851d6991ef7e33d` | Handoff documentation update over first implementation snapshot `810ac1df69fab7f7152601ec97cb9efe5d9c11e7`. |

Shared onboarding changes are isolated patches, described in
[integration-patches/README.md](integration-patches/README.md). The frozen tag was not changed.
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

## Find evidence and boundary

`cargo test -p find-core --locked` passed **47 tests, 0 failed** on the actual Find
branch above. The subsequent `cargo test --workspace --locked -j2` also completed
successfully: **64 passed, 0 failed**, comprising 17 app/model/rendering/instance
tests and 47 core tests. The app tests finished in 0.06 seconds and the core
tests in 0.66 seconds; these are test execution durations, not app performance
measurements. The full command log is
`/workspace/scratch/find-workspace-test.log`.

Its `crates/arcade-find/src/main.rs` still has `fn main() {}`; no
`link.rs` or `service.rs` exists. `os.rs` is present but is not included by that
main module. These facts agree with the handoff's CODE/DESIGNED distinction.

Real Find-to-Shelf invocation and optional Shelf-to-`find.show` cannot be verified
with this snapshot: Find has no runnable resident service or advertised Link
actions. Shelf's verified incoming payloads follow the frozen handoff, and its
generic peer discovery will offer `find.show` when a real Find manifest advertises
it. Find is not registered using its empty placeholder executable.

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

These results are from the separately reviewable integration branches and
patches; they do not describe synchronized published app releases. Exact base
and candidate commits and application commands are preserved in
[integration-patches/README.md](integration-patches/README.md).

| Candidate check | Actual result |
|---|---|
| Link Rust, `cargo test -p arcade-link` | 36 passed: 19 unit, 12 IPC integration, 5 conformance tests. |
| Link Qt module | 2/2 CTest targets passed, including Shelf/Find metadata assertions. |
| Shared isolated Shelf e2e group | 3/3 checks passed with the real Shelf binary under private Xvfb/D-Bus: mixed additions/skips, stopped-app background relaunch/recovery, picker Escape cancellation. |
| Tools focused native core tests | 12/12 passed across `shelf_onboarding`, `releases`, `platform_logic` and `link_contract`, including release-source selection, archive validation, rollback and login ownership. |
| Wheel focused integration tests | 2/2 CTest targets passed: `test_WheelLink` and `test_ArcadeLinkProvider`, with the Link mock CLI on PATH; named Shelf options and input modes were preserved. |

The Tools checks use generated fixtures rather than executing real platform
installers. The Wheel checks use explicit protocol mocks rather than proving a
native slot-assignment session with Shelf. Full native installer and peer UI
validation remains open.

## Remaining native ecosystem checks

Look, Lens, Clipboard and Wheel contracts are implemented through their real
capabilities and verified by source plus protocol mocks. Their native UI flows
have not been executed as real peers in this verification stream. Box's native
CLI and the full Find workspace results are recorded above. Tools requires the isolated onboarding patch and synchronized consumer
dependencies before the released manager can recognize Shelf; merely writing a
Shelf manifest does not update its fixed application list.

Final validation still needs real Hyprland/Nautilus transfers, peer capture and
preview windows, native Clipboard Private mode/device transfers, Wheel slot
assignment, Tools installer/update lifecycle, and corresponding Windows/macOS
interactive runs. Successful protocol checks do not replace those desktop checks.
