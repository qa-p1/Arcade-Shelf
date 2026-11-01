# Ecosystem onboarding

Shelf and Arcade Find joined the shared Arcade repositories on 2026-10-10.
Shelf's own source never needed these changes to work: peers find it through
its manifest. The shared catalogs make it a first-class family member
(Connected apps rows, glyph, accent, Tools installs, Wheel slots).

| Repository | Change | Merged |
|---|---|---|
| Arcade Link | Shelf and Find IDs, names, pitches and releases pages (Rust and Qt), glyphs, accents, SPEC catalog rows, fixtures, real-binary groups in `tools/e2e.py`. Tagged **`v0.2.0`** (`bedee71`); protocol v1 and manifest schema 1 unchanged. | [#1](https://github.com/qa-p1/Arcade-Link/pull/1) |
| Arcade Tools | Install, update, repair, launch and remove Shelf and Find; data folders and login entries; Link `v0.2.0`. | [#1](https://github.com/qa-p1/Arcade-tools/pull/1) |
| Arcade Wheel | Connected apps rows and slot actions (`shelf.show`, `shelf.add`, `find.show`); Link Qt module re-vendored from `v0.2.0`. | [#5](https://github.com/qa-p1/Arcade-wheel/pull/5) |
| Arcade Box | Link `v0.2.0`: Shelf and Find in Connected apps, glyphs, accents (0.1.1). | [#1](https://github.com/qa-p1/Arcade-box/pull/1) |
| Arcade Lens | Link `v0.2.0`; **Add to Shelf** for captures, text, links, code and paths, and **Search in Find** (0.1.1). | [#1](https://github.com/qa-p1/Arcade-lens/pull/1) |
| Arcade Look | Link `v0.2.0`; **Add to Shelf** in the `A` strip for the previewed file (0.1.1). | [#1](https://github.com/qa-p1/Arcade-look/pull/1) |
| Arcade Clipboard | Link `v0.2.0`: Shelf and Find in Connected apps, glyphs, accents. | [#1](https://github.com/qa-p1/Arcade-clipboard/pull/1) |

Each consumer picks the peer actions it shows from its own list, so the pin
alone adds Shelf to Connected apps, not to menus. Lens and Look add an
explicit **Add to Shelf** entry; Box and Clipboard list Shelf without one.

Shelf vendors the Link Qt module from `v0.2.0` too
([provenance](../vendor/arcade-link/provenance.json),
`scripts/check-vendor.py`).

## Verification before merging

| Change | Checks | Result |
|---|---|---|
| Link | `cargo fmt --all --check`; clippy `-D warnings`; `cargo test --workspace --locked`; Qt module CTest | Clean; all pass; CI 6/6 on Linux, Windows and macOS |
| Link ecosystem runner, real binaries | `tools/e2e.py --only shelf` and `--only find` | shelf 3/3, find 3/3 |
| Tools | `cargo test -p arcade-tools-core` (onboarding, releases, platform logic, Link contract); lifecycle suites through Link's isolated runner; the Tauri shell `cargo check` | 15/15; 4/4 and 3/3; clean; CI 3/3 |
| Wheel | Full CTest with Qt 6.8.3 offscreen; `packaging/check-link-vendor.py --source` | 9/9 in CI; vendored files match `v0.2.0` |

Tools' fixtures don't install real Windows or macOS packages, and Wheel's
tests don't drive its native Wayland overlay. Real installs through Tools use
the apps' published GitHub releases (each with `arcade-release.json`).
