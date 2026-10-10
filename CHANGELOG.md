# Changelog

## 0.1.0 — development milestone

- Floating capsule and expanded native Qt Quick surface with dark/light themes,
  collection filtering, multi-selection, menus, keyboard controls and preferences.
- Files and folders staged by reference; text, raw images and temporary handoffs
  held in bounded private owned storage.
- SQLite persistence, multiple collections, ordering, pinning, retention,
  deduplication, crash recovery and Undo for the last addition.
- Native copy-only drag-out, URI/text/HTML drag-in, internal item reordering,
  resizing, edge tabs and compositor-aware placement adapters.
- Arcade Link v1 `shelf.add`, `shelf.show`, and explicit interactive `shelf.pick`;
  capability-driven peer operations with progress and cancellation.
- Isolated tests, cross-platform build/package workflows, dependency provenance,
  and reviewable shared ecosystem onboarding patches.
- Corrected search focus, native drag-source lifetime, and rapid-add GUI overhead;
  untrusted display strings remain literal text.
- Edge hover peeking without focus theft, inward dock expansion and persistent
  tab return; native move-completion snapping and fullscreen capsule policies.
- Platform backdrop adapters with explicit compositor and transparent-window
  fallbacks; two-worker thumbnail decoding with a configurable bounded cache.
- Protected asynchronous preference edits, storage replacement and special-file
  guards, and preflighted Link response limits before committing additions.
- Relocatable Linux runtime launch, required SQLite-only driver deployment,
  per-user Windows packaging and bounded CI failure diagnostics.

This milestone is not a stable release. Platform interaction and ecosystem
verification boundaries are recorded in [status](docs/STATUS.md).
