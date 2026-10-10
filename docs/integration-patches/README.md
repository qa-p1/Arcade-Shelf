# Reviewable ecosystem onboarding changes

Shelf and Find are registered in the shared Arcade repositories on isolated
feature branches with public **draft** pull requests. Copies are kept here
for offline review. Nothing has been merged, tagged or released, and Shelf
keeps the frozen Link `v0.1.0` Qt module unchanged.

| Repository | Base | Head / branch | Draft review / offline patch |
|---|---|---|---|
| Arcade Link | `92aa03c1c062628757a48a1838cb54c3916a017c` | `b4befb6c2ddea521133685d24f1ce21ac0bc300d` / `feature/shelf-find-onboarding` | [Arcade-Link #1](https://github.com/qa-p1/Arcade-Link/pull/1) / [01-arcade-link.patch](01-arcade-link.patch) |
| Arcade Tools | `16b5bb0b69729f3eb95b1eed722933aba9e85bdb` | `91caecfb4cb11148282d6559a60d77d22fa7d1b7` / `feature/shelf-onboarding` | [Arcade-tools #1](https://github.com/qa-p1/Arcade-tools/pull/1) / [02-arcade-tools.patch](02-arcade-tools.patch) |
| Arcade Wheel | `550efe1a56270ed85ad1bb1fe080b37005e65c84` (active `arcade/link`) | `488a1ce9d670e4b8a267fd9d47888b9db67b37c6` / `feature/shelf-action-onboarding` | [Arcade-wheel #5](https://github.com/qa-p1/Arcade-wheel/pull/5) / [03-arcade-wheel.patch](03-arcade-wheel.patch) |

Machine-readable pins are in [provenance.json](provenance.json). Apply each
patch only in its own clean checkout of the base:

```sh
git switch -c review/onboarding BASE_COMMIT
git am /absolute/path/to/Arcade-Shelf/docs/integration-patches/PATCH_FILE
```

## What each change does

**Link.** Registers `arcade.shelf` and `arcade.find` in the Rust and Qt
metadata (IDs, names, pitches, releases URLs), adds their glyphs
(`assets/glyphs/arcade.shelf.svg`, `arcade.find.svg`) and accents
(Shelf `#94A8FF`, Find `#22C55E`), the action and shortcut catalog rows for
both apps, mock fixtures (`fixtures/shelf.json`, and `fixtures/find.json` from
Find's real manifest), and real-binary groups for Shelf and Find in
`tools/e2e.py` plus benchmark entries. Wire and manifest version 1 are
unchanged; no tag is moved.

**Tools.** Keeps its immutable Link dependency and adds compatibility
metadata for Shelf and Find (app list and `tools.install` allowlist). Shelf:
Linux runtime-bundle selection with bounded extraction (no absolute paths,
traversal, links or special files; at most 2 GiB / 100,000 entries; one
package root; no install script), data under Qt's `qa-p1/ArcadeShelf`, and
Shelf-owned login entries. Find: AppImage, per-user Inno installer and
universal DMG; data folders matching Find's own paths; the same login entries
Find's Settings toggle writes (`arcade-find.desktop`, Run value `ArcadeFind`,
`arcade.find.plist`). The desktop shell now watches every managed app's
endpoint.

**Wheel.** Keeps its vendored Link module and pin. App-local metadata for
Shelf and Find adds Connected apps rows, glyphs, accents, per-peer toggles,
standard errors and accepted structured action drafts (`shelf.show` with
`options.shelf`, `shelf.add` with selection input modes, `find.show` with
clipboard or file selection). No special wire format and no `shelf.open`.

## Verification — 2026-10-10

| Change | Checks | Result |
|---|---|---|
| Link | `cargo fmt --all --check`; `cargo clippy --workspace --all-targets --all-features --locked -- -D warnings`; `cargo test -p arcade-link --locked` | Clean; 19 unit + 12 IPC + 5 conformance passed |
| Link Qt module | Build `qt/` with Qt 6.8.3; offscreen CTest | 2/2, including Shelf and Find metadata |
| Link ecosystem runner, real binaries | `ARCADE_SHELF_REPO=… python3 tools/e2e.py --only shelf` and `--only find` (Find at `../Arcade-Find`) | shelf 3/3 (three consecutive runs), find 3/3 |
| Tools | `cargo test -p arcade-tools-core --test find_onboarding --test shelf_onboarding --test releases --test platform_logic --test link_contract --locked`; lifecycle suites through Link's isolated runner; clippy `-D warnings`; the Tauri shell `cargo check` | 15/15; lifecycle 4/4 and Link lifecycle 3/3; clean |
| Wheel | Full CTest with Qt 6.8.3 offscreen, `ARCADE_REQUIRE_LAYERSHELLQT=OFF`, the Link mock CLI on `PATH`; `packaging/check-link-vendor.py` | 9/9; vendored Link files match `v0.1.0` |

Public CI on each pull request is the record for Windows and macOS. Tools'
fixtures don't install real Windows or macOS packages, and Wheel's tests
don't exercise its native Wayland overlay.

## Remaining synchronization and release work

Review the three branches in their repositories. Publishing a new Link tag,
merging, synchronizing consumers' pins (Box, Look, Lens and Clipboard list
apps from Link) and releasing are the owner's decisions. Then test real
installation, update, repair and removal of Shelf and Find with Tools against
checksum-verified release assets on Linux, Windows and macOS. Shelf and Find
work with each other and with other apps through their manifests alone in
the meantime.
