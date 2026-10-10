# Reviewable ecosystem onboarding changes

These three changes are implemented and tested in isolated feature branches,
pushed without force, and opened as public **draft** pull requests on 2026-10-09.
They are also copied into Shelf for offline review. No changes have been merged,
tagged or released. Shelf keeps the frozen Link `v0.1.0` Qt module unchanged.

| Repository | Base | Published head / branch | Draft review / offline patch |
|---|---|---|---|
| Arcade Link | `92aa03c1c062628757a48a1838cb54c3916a017c` | `c4c755ffddd4b69e7de3577f024dbd7e3ae40864` / `feature/shelf-find-onboarding` | [Arcade-Link #1](https://github.com/qa-p1/Arcade-Link/pull/1) / [01-arcade-link.patch](01-arcade-link.patch) |
| Arcade Tools | `16b5bb0b69729f3eb95b1eed722933aba9e85bdb` | `126e09b8f5c7be79213a12cebbcf63a8c51adebd` / `feature/shelf-onboarding` | [Arcade-tools #1](https://github.com/qa-p1/Arcade-tools/pull/1) / [02-arcade-tools.patch](02-arcade-tools.patch) |
| Arcade Wheel | `550efe1a56270ed85ad1bb1fe080b37005e65c84` (active `arcade/link`) | `8f91423d3de87aefee109bc28e889fee3f2449cb` / `feature/shelf-action-onboarding` | [Arcade-wheel #5](https://github.com/qa-p1/Arcade-wheel/pull/5) / [03-arcade-wheel.patch](03-arcade-wheel.patch) |

The original implementation commits are Link `6997428a4dece7e1283e368cf755ed0b3a4f8efe`
and Tools `d2fab05620b25addb1d1ba07834b6696c60da940`. Their public branch heads
add formatting-only commits required by existing Rust CI. The bases were verified
against unchanged remote main heads before publication; the Link tag object and
peeled code remain `5f531b3dbfedbdb859b6882361a77747dcf9b834` and
`337b85fe85fca3e68ec472a6337aa9ebd77fc7ef`.

Full machine-readable pins are in [provenance.json](provenance.json). Apply
each patch only in its own clean repository checkout, starting at that base:

```sh
git switch -c review/shelf-onboarding BASE_COMMIT
git am /absolute/path/to/Arcade-Shelf/docs/integration-patches/PATCH_FILE
```

The Link patch registers `arcade.shelf` and `arcade.find` in Rust and Qt
metadata, with names, pitches, release URLs, accents and monochrome glyphs.
It adds Shelf's action/shortcut catalog entries and mock fixtures, metadata
tests, a Shelf benchmark group, and a real resident Shelf e2e group. The Find
fixture is explicitly **DESIGNED**, matching the handoff inspected at Find
`feature/find-v0.1` commit `5700d0d948decbe47d9365d98851d6991ef7e33d`.
No Find implementation or real Find-to-Shelf success is asserted. Wire and
manifest version 1 remain unchanged; no protocol tag is moved.

The Tools patch keeps its existing immutable Link dependency and introduces
small compatibility metadata for Shelf. Shelf appears in its app list and
`tools.install` allowlist with correct release and installation paths. It
adds Linux runtime-bundle selection and bounded extraction: no absolute
paths, traversal, links or special files, at most 2 GiB / 100,000 entries,
exactly one expected package root, and no executable installation script.
Installation participates in Tools' existing replacement/rollback flow.
Data paths match Qt `AppLocalDataLocation` under `qa-p1/ArcadeShelf`; original
user files are outside Tools' removal targets. Login entries match Shelf's
native desktop/VBS/LaunchAgent filenames and ownership marker; unrelated
entries are refused. Find installation is deferred until its actual runtime
and package mappings are available.

The Wheel patch keeps its vendored Link module and pin unchanged. Shelf
compatibility metadata adds Connected apps and Get rows, a glyph, peer
toggles, standard errors and accepted structured action drafts. A slot can
store `shelf.show` with `options.shelf` and `shelf.add` with the existing
selection input modes. No special wire format or duplicate `shelf.open`
action is introduced. Drafts still require normal Settings confirmation.

## Verification executed — 2026-10-09

| Change | Actual checks | Result |
|---|---|---|
| Link Rust published head | `cargo test -p arcade-link --locked` | 19 unit + 12 IPC integration + 5 conformance tests passed |
| Link Rust CI checks | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --all-features --locked -- -D warnings` | Passed at published head |
| Link public CI | [Published-head run](https://github.com/qa-p1/Arcade-Link/actions/runs/37991839822) | Rust and Qt build/test matrices passed on Linux, Windows and macOS: 6/6 jobs |
| Link Qt candidate | Build `qt/`; `QT_QPA_PLATFORM=offscreen ctest --test-dir build-qt --output-on-failure` | 2/2 CTest targets passed, including new Shelf/Find metadata assertions |
| Real Shelf isolated runner | `ARCADE_SHELF_REPO=/path/to/Arcade-Shelf python3 tools/e2e.py --only shelf` | 3/3 passed: mixed reference additions/skips, background relaunch/recovery, picker Escape cancellation |
| Tools published head | `cargo test -p arcade-tools-core --test shelf_onboarding --test releases --test platform_logic --test link_contract --locked` | 12/12 passed, including local release-source selection, archive validation, replacement rollback and login ownership |
| Tools focused Rust CI checks | `cargo fmt --all --check`; `cargo clippy -p arcade-tools-core --all-targets --locked -- -D warnings` | Passed at published head |
| Wheel candidate | Build `test_WheelLink test_ArcadeLinkProvider`; run CTest for those two targets with the Link mock CLI on `PATH` | 2/2 CTest targets passed; named Shelf options and input modes are preserved |
| Wheel vendor integrity | `python3 packaging/check-link-vendor.py --source /path/to/Arcade-Link` | 8 pinned files still match immutable `v0.1.0` |
| Patch whitespace | `git diff --check` before commits | Passed in all three repositories |

Link Qt and resident Shelf runner checks ran on the implementation commit before
its formatting-only follow-up. Rust checks were rerun at published Link/Tools
heads using Rust 1.95.0. Wheel's Linux test configuration used
`ARCADE_REQUIRE_LAYERSHELLQT=OFF` and
offscreen rendering. This does not verify Wheel's native Wayland overlay.
Tools tests exercise isolated fixtures and paths; they do not install a real
Windows or macOS package or validate the Tauri desktop build. The full multi-app
ecosystem suite was not run. Public CI results remain separate from these local
checks and should be reviewed on each draft pull request.
The benchmark group is implemented but has no measured baseline in this
change. Additional Shelf verification is documented in Shelf's testing and
ecosystem verification documents.

## Remaining synchronization and release work

Review these branches in their owner repositories and run the full affected
consumer suites. Publish a new immutable Link tag only through the owner's
release workflow, then synchronize consuming metadata/pins as appropriate;
the current tag must remain unchanged. Review and ship Tools and Wheel
compatibility changes, then test actual Shelf installation, update, repair
and uninstall against final checksum-verified Linux/Windows/macOS assets.
Find's Link consumer must first exist and advertise `find.show` before its
real bidirectional flow can be tested. Shelf's registry-based operation and
standalone application do not depend on those shared patches being shipped.
