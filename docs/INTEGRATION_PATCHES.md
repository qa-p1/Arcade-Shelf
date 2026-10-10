# Shared onboarding patches

Shelf's own source is self-contained and uses unchanged frozen Link code.
The ecosystem still has explicit app catalogs, so manifests alone don't
complete Wheel and Tools onboarding.

Reviewable format-patches, base commits, test evidence and how to apply them
are in [integration-patches/README.md](integration-patches/README.md).

| Repository | Head | Draft review | Purpose |
|---|---|---|---|
| Arcade Link | `b4befb6c2ddea521133685d24f1ce21ac0bc300d` | [Arcade-Link #1](https://github.com/qa-p1/Arcade-Link/pull/1) | Shelf and Find identity metadata, glyphs, accents, catalog, fixtures, and real-binary e2e groups for both |
| Arcade Tools | `91caecfb4cb11148282d6559a60d77d22fa7d1b7` | [Arcade-tools #1](https://github.com/qa-p1/Arcade-tools/pull/1) | Shelf and Find in the catalog with safe per-platform install, data and login mappings |
| Arcade Wheel | `488a1ce9d670e4b8a267fd9d47888b9db67b37c6` | [Arcade-wheel #5](https://github.com/qa-p1/Arcade-wheel/pull/5) | Shelf and Find action references and Connected apps metadata |

The branches were pushed without force; they keep protocol v1, the immutable
`v0.1.0` tag, the current main branches and consumers' frozen dependencies.
Nothing was merged, released or tagged. Find's entries describe what Find
ships (`feature/find-v0.1`), not a design.

Consumers already add and reference Shelf and Find through generic Link
discovery where their capabilities allow. Fixed allowlists need these
changes plus a Link tag and dependency synchronization, published through
the owner's normal release process.
