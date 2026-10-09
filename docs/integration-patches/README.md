# Reviewable ecosystem onboarding changes

These three patches are implemented and tested in isolated local branches.
They are copied into Shelf so the review can include the changes required in
other repositories. They have **not** been pushed, merged, tagged or released.
Shelf itself keeps the frozen Link `v0.1.0` Qt module unchanged.

| Repository | Base | Candidate / local branch | Patch |
|---|---|---|---|
| Arcade Link | `92aa03c1c062628757a48a1838cb54c3916a017c` | `6997428a4dece7e1283e368cf755ed0b3a4f8efe` / `feature/shelf-find-onboarding` | [01-arcade-link.patch](01-arcade-link.patch) |
| Arcade Tools | `16b5bb0b69729f3eb95b1eed722933aba9e85bdb` | `d2fab05620b25addb1d1ba07834b6696c60da940` / `feature/shelf-onboarding` | [02-arcade-tools.patch](02-arcade-tools.patch) |
| Arcade Wheel | `550efe1a56270ed85ad1bb1fe080b37005e65c84` (active `arcade/link`) | `8f91423d3de87aefee109bc28e889fee3f2449cb` / `feature/shelf-action-onboarding` | [03-arcade-wheel.patch](03-arcade-wheel.patch) |

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
| Link Rust candidate | `cargo test -p arcade-link` | 19 unit + 12 IPC integration + 5 conformance tests passed |
| Link Qt candidate | Build `qt/`; `QT_QPA_PLATFORM=offscreen ctest --test-dir build-qt --output-on-failure` | 2/2 CTest targets passed, including new Shelf/Find metadata assertions |
| Real Shelf isolated runner | `ARCADE_SHELF_REPO=/path/to/Arcade-Shelf python3 tools/e2e.py --only shelf` | 3/3 passed: mixed reference additions/skips, background relaunch/recovery, picker Escape cancellation |
| Tools candidate | `cargo test -p arcade-tools-core --test shelf_onboarding --test releases --test platform_logic --test link_contract` | 12/12 passed, including local release-source selection, archive validation, replacement rollback and login ownership |
| Wheel candidate | Build `test_WheelLink test_ArcadeLinkProvider`; run CTest for those two targets with the Link mock CLI on `PATH` | 2/2 CTest targets passed; named Shelf options and input modes are preserved |
| Wheel vendor integrity | `python3 packaging/check-link-vendor.py --source /path/to/Arcade-Link` | 8 pinned files still match immutable `v0.1.0` |
| Patch whitespace | `git diff --check` before commits | Passed in all three repositories |

Wheel's Linux test configuration used `ARCADE_REQUIRE_LAYERSHELLQT=OFF` and
offscreen rendering. This does not verify Wheel's native Wayland overlay.
Tools tests exercise isolated fixtures and paths; they do not install a real
Windows or macOS package. The full multi-app ecosystem suite was not run.
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
