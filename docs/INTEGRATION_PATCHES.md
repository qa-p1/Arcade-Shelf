# Shared onboarding patches

Shelf's primary source is self-contained and uses unchanged frozen Link code.
The ecosystem still has explicit application catalogs, so manifests alone do
not complete Wheel/Tools onboarding.

Reviewable format-patches, base commits, test evidence and application instructions
are in [integration-patches/README.md](integration-patches/README.md).

| Repository | Published feature head | Draft review | Purpose |
|---|---|---|---|
| Arcade Link | `c4c755ffddd4b69e7de3577f024dbd7e3ae40864` | [Arcade-Link #1](https://github.com/qa-p1/Arcade-Link/pull/1) | Shelf/Find identity metadata, glyphs, catalog and isolated Shelf runner |
| Arcade Tools | `126e09b8f5c7be79213a12cebbcf63a8c51adebd` | [Arcade-tools #1](https://github.com/qa-p1/Arcade-tools/pull/1) | Shelf catalog and safe per-platform install/data/lifecycle mapping |
| Arcade Wheel | `8f91423d3de87aefee109bc28e889fee3f2449cb` | [Arcade-wheel #5](https://github.com/qa-p1/Arcade-wheel/pull/5) | Compatible Shelf action references and connected-app metadata |

These isolated feature branches were pushed without force and opened as draft
pull requests on 2026-10-09. They preserve protocol v1, the immutable `v0.1.0`
tag, current remote main branches and consumers' frozen dependencies. No changes
were merged, released or tagged. Link and Tools include formatting-only follow-up
commits so their required Rust formatting checks pass. Find's fixture is explicitly
designed-only metadata; Find's resident integration still needs implementation.

Current consumers can add/reference Shelf through generic Link discovery when
their capabilities permit it. Released fixed allowlists need these patches and
the corresponding review/dependency synchronization. Publish updated shared
packages only through the owner's normal release process.
