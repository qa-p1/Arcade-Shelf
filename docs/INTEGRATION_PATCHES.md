# Shared onboarding patches

Shelf's primary source is self-contained and uses unchanged frozen Link code.
The ecosystem still has explicit application catalogs, so manifests alone do
not complete Wheel/Tools onboarding.

Reviewable format-patches, base commits, test evidence and application instructions
are in [integration-patches/README.md](integration-patches/README.md).

| Repository | Local candidate commit | Purpose |
|---|---|---|
| Arcade Link | `6997428a4dece7e1283e368cf755ed0b3a4f8efe` | Shelf/Find identity metadata, glyphs, catalog and isolated Shelf runner |
| Arcade Tools | `d2fab05620b25addb1d1ba07834b6696c60da940` | Shelf catalog and safe per-platform install/data/lifecycle mapping |
| Arcade Wheel | `8f91423d3de87aefee109bc28e889fee3f2449cb` | Compatible Shelf action references and connected-app metadata |

These changes preserve protocol v1 and the existing immutable tag. They were
tested locally and captured inside this repository; they were not pushed,
merged, released or tagged. Find's fixture is explicitly designed-only metadata,
not a claim that Find's resident integration exists.

Current consumers can add/reference Shelf through generic Link discovery when
their capabilities permit it. Released fixed allowlists need these patches and
the corresponding review/dependency synchronization. Publish updated shared
packages only through the owner's normal release process.
